#define _DEFAULT_SOURCE

#include "core/vless.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int read_n(int fd, uint8_t *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, buf + off, n - off);
        if (r > 0) { off += (size_t)r; continue; }
        if (r == 0) return -1;          /* eof early */
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int dial_dest(const vless_dest_t *d) {
    char portstr[8];
    snprintf(portstr, sizeof portstr, "%u", d->port);

    char hostbuf[256];
    if (d->atyp == VLESS_ADDR_DOMAIN) {
        snprintf(hostbuf, sizeof hostbuf, "%s", d->domain);
    } else if (d->atyp == VLESS_ADDR_IPV4) {
        inet_ntop(AF_INET, d->host_addr, hostbuf, sizeof hostbuf);
    } else {
        inet_ntop(AF_INET6, d->host_addr, hostbuf, sizeof hostbuf);
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostbuf, portstr, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* read the vless request header off the client fd, fill dest, return the
   number of leftover payload bytes already read into `spill` */
static int read_request(int fd, vless_dest_t *dest, uint8_t *spill, size_t *spill_len) {
    uint8_t hdr[1 + 16 + 1];
    if (read_n(fd, hdr, sizeof hdr) != 0) { fprintf(stderr, "mock: short header\n"); return -1; }
    if (hdr[0] != 0x00) return -1;              /* version */
    uint8_t addons = hdr[17];

    uint8_t skip[256];
    if (addons && read_n(fd, skip, addons) != 0) return -1;

    uint8_t cmd_port_atyp[1 + 2 + 1];
    if (read_n(fd, cmd_port_atyp, sizeof cmd_port_atyp) != 0) return -1;
    uint16_t port = (uint16_t)((cmd_port_atyp[1] << 8) | cmd_port_atyp[2]);
    uint8_t atyp = cmd_port_atyp[3];

    memset(dest, 0, sizeof *dest);
    dest->port = port;
    if (atyp == VLESS_ADDR_IPV4) {
        dest->atyp = VLESS_ADDR_IPV4;
        if (read_n(fd, dest->host_addr, 4) != 0) return -1;
    } else if (atyp == VLESS_ADDR_IPV6) {
        dest->atyp = VLESS_ADDR_IPV6;
        if (read_n(fd, dest->host_addr, 16) != 0) return -1;
    } else if (atyp == VLESS_ADDR_DOMAIN) {
        dest->atyp = VLESS_ADDR_DOMAIN;
        uint8_t dlen;
        if (read_n(fd, &dlen, 1) != 0) return -1;
        if (read_n(fd, (uint8_t *)dest->domain, dlen) != 0) return -1;
        dest->domain[dlen] = '\0';
    } else {
        return -1;
    }

    set_nonblock(fd);
    ssize_t r = read(fd, spill, 4096);
    *spill_len = (r > 0) ? (size_t)r : 0;
    return 0;
}

static void relay(int a, int b) {
    set_nonblock(a);
    set_nonblock(b);
    struct pollfd pfd[2];
    pfd[0].fd = a; pfd[1].fd = b;
    uint8_t buf[8192];
    for (;;) {
        pfd[0].events = POLLIN;
        pfd[1].events = POLLIN;
        int r = poll(pfd, 2, 30000);
        if (r <= 0) break;
        if (pfd[0].revents & POLLIN) {
            ssize_t n = read(a, buf, sizeof buf);
            if (n <= 0 && !(errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n > 0 && write(b, buf, (size_t)n) < 0) break;
        }
        if (pfd[1].revents & POLLIN) {
            ssize_t n = read(b, buf, sizeof buf);
            if (n <= 0 && !(errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n > 0 && write(a, buf, (size_t)n) < 0) break;
        }
        if ((pfd[0].revents | pfd[1].revents) & (POLLHUP | POLLERR)) break;
    }
}

static void handle(int cfd) {
    vless_dest_t dest;
    uint8_t spill[4096]; size_t spill_len = 0;
    if (read_request(cfd, &dest, spill, &spill_len) != 0) {
        fprintf(stderr, "mock: read_request failed\n");
        close(cfd); return;
    }

    char dbg[256];
    if (dest.atyp == VLESS_ADDR_DOMAIN) snprintf(dbg, sizeof dbg, "%s", dest.domain);
    else inet_ntop(dest.atyp == VLESS_ADDR_IPV4 ? AF_INET : AF_INET6,
                   dest.host_addr, dbg, sizeof dbg);
    fprintf(stderr, "mock: req dst=%s:%u spill=%zu\n", dbg, dest.port, spill_len);

    int dfd = dial_dest(&dest);
    if (dfd < 0) { fprintf(stderr, "mock: dial_dest failed\n"); close(cfd); return; }
    fprintf(stderr, "mock: dialed origin ok\n");

    uint8_t resp[2] = {0x00, 0x00};
    if (write(cfd, resp, 2) != 2) { close(cfd); close(dfd); return; }

    if (spill_len) {
        size_t off = 0;
        while (off < spill_len) {
            ssize_t w = write(dfd, spill + off, spill_len - off);
            if (w <= 0) { close(cfd); close(dfd); return; }
            off += (size_t)w;
        }
    }

    relay(cfd, dfd);
    close(cfd);
    close(dfd);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <port> [bind_addr]\n", argv[0]); return 2; }
    setvbuf(stderr, NULL, _IONBF, 0);   /* logs must survive a kill, no buffering */
    signal(SIGPIPE, SIG_IGN);

    int port = atoi(argv[1]);
    /* optional bind addr. default loopback (smoke tests rely on it); pass an
       explicit addr to expose it on the lan for device e2e*/
    const char *bind_addr = (argc >= 3) ? argv[2] : "127.0.0.1";
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = inet_addr(bind_addr);
    a.sin_port = htons((uint16_t)port);
    if (bind(lfd, (struct sockaddr *)&a, sizeof a) != 0) {
        perror("bind"); return 1;
    }
    listen(lfd, 16);
    fprintf(stderr, "mock vless server on %s:%d\n", bind_addr, port);

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) { if (errno == EINTR) continue; break; }
        fprintf(stderr, "mock: accepted client fd=%d\n", cfd);
        handle(cfd);
    }
    close(lfd);
    return 0;
}
