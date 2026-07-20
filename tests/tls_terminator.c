#define _DEFAULT_SOURCE

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

#include <openssl/ssl.h>
#include <openssl/err.h>

static int connect_backend(const char *host, const char *port) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res) return -1;
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

/* relay between a tls conn (ssl) and a plaintext backend fd until either ends.
   simple blocking-ish poll loop; fine for a one-shot smoke test*/
static void relay(SSL *ssl, int sfd, int bfd) {
    struct pollfd pfd[2];
    pfd[0].fd = sfd;    /* the tls socket (we poll the raw fd for readability) */
    pfd[1].fd = bfd;
    uint8_t buf[8192];
    for (;;) {
        pfd[0].events = POLLIN;
        pfd[1].events = POLLIN;
        if (poll(pfd, 2, 30000) <= 0) break;

        if (pfd[0].revents & POLLIN) {
            int n = SSL_read(ssl, buf, sizeof buf);
            if (n > 0) {
                if (write(bfd, buf, (size_t)n) < 0) break;
            } else {
                int e = SSL_get_error(ssl, n);
                if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) break;
            }
        }
        if (pfd[1].revents & POLLIN) {
            ssize_t n = read(bfd, buf, sizeof buf);
            if (n > 0) {
                if (SSL_write(ssl, buf, (int)n) <= 0) break;
            } else if (n == 0) {
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        }
        if ((pfd[0].revents | pfd[1].revents) & (POLLHUP | POLLERR)) break;
    }
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s <listen_port> <backend_host> <backend_port> <cert> <key>\n",
                argv[0]);
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);

    int lport = atoi(argv[1]);
    const char *bhost = argv[2], *bport = argv[3];
    const char *cert = argv[4], *key = argv[5];

    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) { ERR_print_errors_fp(stderr); return 1; }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    if (SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        return 1;
    }

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)lport);
    if (bind(lfd, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind"); return 1; }
    listen(lfd, 16);
    fprintf(stderr, "tls_terminator on 127.0.0.1:%d -> %s:%s\n", lport, bhost, bport);

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) { if (errno == EINTR) continue; break; }

        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, cfd);
        if (SSL_accept(ssl) != 1) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl); close(cfd);
            continue;
        }

        int bfd = connect_backend(bhost, bport);
        if (bfd < 0) { SSL_free(ssl); close(cfd); continue; }

        relay(ssl, cfd, bfd);

        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(bfd);
        close(cfd);
    }
    close(lfd);
    SSL_CTX_free(ctx);
    return 0;
}
