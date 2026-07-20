#define _DEFAULT_SOURCE

#include "dialer.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

void dialer_set_target(dialer_ctx_t *d, const char *host, int port) {
    if (!d || !host) return;
    size_t hl = strlen(host);
    if (hl >= sizeof d->host) hl = sizeof d->host - 1;
    memcpy(d->host, host, hl);
    d->host[hl] = '\0';
    snprintf(d->port, sizeof d->port, "%d", port);
}

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int dialer_connect(void *ctx) {
    dialer_ctx_t *d = (dialer_ctx_t *)ctx;
    if (!d || !d->host[0]) return -1;

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
/* routing bypass is ipv4-only */
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(d->host, d->port, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET) continue;
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;

        set_nonblock(fd);
        int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (r == 0 || errno == EINPROGRESS) {
/* low-latency for many short tls streams (safari) */
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}
