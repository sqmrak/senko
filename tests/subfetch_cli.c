#define _DEFAULT_SOURCE

#include "core/subfetch.h"
#include "core/transport.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

static int real_dial(void *ctx, const char *host, uint16_t port) {
    (void)ctx;
    char portstr[8];
    snprintf(portstr, sizeof portstr, "%u", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);    /* transport expects nonblock */
            break;
        }
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <http-url>\n", argv[0]); return 2; }

    subfetch_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.dial = real_dial;
    cfg.tcp = &transport_tcp;
    cfg.tls = &transport_tls;
    cfg.max_redirects = 5;

    static uint8_t body[256 * 1024];
    size_t blen = 0;
    subfetch_status_t r = subfetch_get(&cfg, argv[1], body, sizeof body, &blen, 10000);
    if (r != SUBFETCH_OK) {
        fprintf(stderr, "subfetch failed: %d\n", r);
        return 1;
    }
    fwrite(body, 1, blen, stdout);
    return 0;
}
