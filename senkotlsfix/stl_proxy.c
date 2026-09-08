#define _DEFAULT_SOURCE

#include "stl_proxy.h"
#include "stl_log.h"
#include "fishhook.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef C_PROXY_STATE
#define C_PROXY_STATE "/var/run/senko-c-proxy"
#endif
#define C_PROXY_TIMEOUT_SEC 12

static int (*orig_connect)(int, const struct sockaddr *, socklen_t);

/* stl_connect runs on every outbound connection, so the state file is read at
   most once a second instead of once per connect */
static int cached_port;
static time_t cached_at;

static int read_proxy_port_uncached(void) {
    struct stat st;
    if (lstat(C_PROXY_STATE, &st) != 0 || !S_ISREG(st.st_mode) ||
#if defined(SENKO_HOST_TEST)
        st.st_uid != geteuid() ||
#else
        st.st_uid != 0 ||
#endif
        (st.st_mode & 0022) != 0)
        return 0;

    FILE *f = fopen(C_PROXY_STATE, "r");
    if (!f) return 0;
    char line[64];
    int port = 0;
    if (fgets(line, sizeof line, f) &&
        sscanf(line, "SENKO-C-PROXY-V1 %d", &port) == 1 &&
        port > 0 && port <= 65535) {
        fclose(f);
        return port;
    }
    fclose(f);
    return 0;
}

static int read_proxy_port(void) {
    time_t now = time(NULL);
    if (now == cached_at) return cached_port;
    cached_port = read_proxy_port_uncached();
    cached_at = now;
    return cached_port;
}

static int bypass_v4(const struct in_addr *addr) {
    uint32_t ip = ntohl(addr->s_addr);
    return (ip >> 24) == 0 || (ip >> 24) == 10 || (ip >> 24) == 127 ||
           (ip >> 16) == 0xa9fe || (ip >> 20) == 0xac1 ||
           (ip >> 16) == 0xc0a8 || (ip >> 28) == 0xe || ip == 0xffffffffu;
}

static int bypass_v6(const struct in6_addr *addr) {
    const uint8_t *p = (const uint8_t *)addr;
    static const uint8_t loopback[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    };
    int unspecified = 1;
    for (size_t i = 0; i < 16; ++i) if (p[i] != 0) unspecified = 0;
    return unspecified || memcmp(p, loopback, sizeof loopback) == 0 ||
           (p[0] & 0xfe) == 0xfc || (p[0] == 0xfe && (p[1] & 0xc0) == 0x80) ||
           p[0] == 0xff;
}

static int send_all(int fd, const uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int recv_all(int fd, uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = recv(fd, buf + off, len - off, 0);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int socks_open(int fd, const struct sockaddr *dest) {
    static const uint8_t greeting[] = { 5, 1, 0 };
    uint8_t reply[262];
    uint8_t req[4 + 16 + 2];
    size_t req_len;

    if (send_all(fd, greeting, sizeof greeting) != 0 ||
        recv_all(fd, reply, 2) != 0 || reply[0] != 5 || reply[1] != 0)
        return -1;

    req[0] = 5;
    req[1] = 1;
    req[2] = 0;
    if (dest->sa_family == AF_INET) {
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)dest;
        req[3] = 1;
        memcpy(req + 4, &v4->sin_addr, 4);
        memcpy(req + 8, &v4->sin_port, 2);
        req_len = 10;
    } else if (dest->sa_family == AF_INET6) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)dest;
        req[3] = 4;
        memcpy(req + 4, &v6->sin6_addr, 16);
        memcpy(req + 20, &v6->sin6_port, 2);
        req_len = 22;
    } else {
        return -1;
    }

    if (send_all(fd, req, req_len) != 0 || recv_all(fd, reply, 4) != 0 ||
        reply[0] != 5 || reply[1] != 0 || reply[2] != 0)
        return -1;

    if (reply[3] == 1)
        return recv_all(fd, reply + 4, 6) == 0 ? 0 : -1;
    if (reply[3] == 4)
        return recv_all(fd, reply + 4, 18) == 0 ? 0 : -1;
    if (reply[3] == 3) {
        if (recv_all(fd, reply + 4, 1) != 0) return -1;
        return recv_all(fd, reply + 5, (size_t)reply[4] + 2) == 0 ? 0 : -1;
    }
    return -1;
}

static int stl_connect(int fd, const struct sockaddr *addr, socklen_t addr_len) {
    if (!orig_connect) {
        errno = ENOSYS;
        return -1;
    }
    if (!addr ||
        (addr->sa_family != AF_INET && addr->sa_family != AF_INET6))
        return orig_connect(fd, addr, addr_len);
    if ((addr->sa_family == AF_INET && addr_len < sizeof(struct sockaddr_in)) ||
        (addr->sa_family == AF_INET6 && addr_len < sizeof(struct sockaddr_in6))) {
        errno = EINVAL;
        return -1;
    }

    int type = 0;
    socklen_t type_len = sizeof type;
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_len) != 0 ||
        type != SOCK_STREAM)
        return orig_connect(fd, addr, addr_len);
    if ((addr->sa_family == AF_INET &&
         bypass_v4(&((const struct sockaddr_in *)addr)->sin_addr)) ||
        (addr->sa_family == AF_INET6 &&
         bypass_v6(&((const struct sockaddr_in6 *)addr)->sin6_addr)))
        return orig_connect(fd, addr, addr_len);

    int port = read_proxy_port();
    if (port == 0) return orig_connect(fd, addr, addr_len);

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return orig_connect(fd, addr, addr_len);
    struct timeval old_rcv, old_snd;
    socklen_t tv_len = sizeof old_rcv;
    int have_rcv = getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &old_rcv, &tv_len) == 0;
    tv_len = sizeof old_snd;
    int have_snd = getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &old_snd, &tv_len) == 0;
    struct timeval timeout = { C_PROXY_TIMEOUT_SEC, 0 };

    if (flags & O_NONBLOCK) (void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

    struct sockaddr_in local;
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons((uint16_t)port);
    int rc = orig_connect(fd, (const struct sockaddr *)&local, sizeof local);
    if (rc == 0) rc = socks_open(fd, addr);

    if (have_rcv) (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &old_rcv, sizeof old_rcv);
    if (have_snd) (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &old_snd, sizeof old_snd);
    if (flags & O_NONBLOCK) (void)fcntl(fd, F_SETFL, flags);
    if (rc == 0) return 0;
    (void)shutdown(fd, SHUT_RDWR);
    errno = ECONNREFUSED;
    return -1;
}

/* the daemon only falls back to this in process proxy when the system has no
   firewall tool at all. pf is the discriminator: ios 7 gained /dev/pf, and
   every system that has it gets routed by pfctl or by ipfw instead. without
   this check the connect hook is rebound in every uikit process on the device,
   which is a lot of risk for a path those systems never take */
int stl_proxy_supported_system(void) {
    static int cached = -1;
    if (cached >= 0) return cached;
    struct stat st;
    cached = (stat("/dev/pf", &st) != 0) ? 1 : 0;
    return cached;
}

void stl_proxy_install_hooks(void) {
    struct rebinding hooks[] = {
        { "connect", (void *)stl_connect, (void **)&orig_connect }
    };
    if (rebind_symbols(hooks, sizeof hooks / sizeof hooks[0]) == 0)
        stl_log("c backend application proxy hook installed");
}

#ifdef SENKO_HOST_TEST
int stl_proxy_active_port_for_test(void) {
    /* skip the one second cache so a test can change the file and look again */
    return read_proxy_port_uncached();
}

int stl_proxy_connect_for_test(int fd, const struct sockaddr *addr, socklen_t addr_len) {
    orig_connect = connect;
    return stl_connect(fd, addr, addr_len);
}
#endif
