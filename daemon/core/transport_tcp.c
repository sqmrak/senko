#include "transport.h"

#include <errno.h>
#include <unistd.h>

/* encode the fd in the handle and offset it so fd zero is not null */
static void *tcp_open(int fd, const transport_tls_cfg_t *cfg) {
    (void)cfg; /* plain tcp ignores tls config */
    if (fd < 0) return (void *)0;
    return (void *)(intptr_t)(fd + 1);
}

static int unbox(void *h) {
    return (int)((intptr_t)h) - 1;
}

static int errno_to_transport(void) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return TRANSPORT_WANT_READ;
    if (errno == EINTR) return TRANSPORT_WANT_READ; /* retry */
    return TRANSPORT_ERR;
}

static int tcp_read(void *h, uint8_t *buf, size_t len) {
    int fd = unbox(h);
    ssize_t n = read(fd, buf, len);
    if (n > 0) return (int)n;
    if (n == 0) return TRANSPORT_EOF;
    return errno_to_transport();
}

static int tcp_write(void *h, const uint8_t *buf, size_t len) {
    int fd = unbox(h);
    ssize_t n = write(fd, buf, len);
    if (n >= 0) return (int)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return TRANSPORT_WANT_WRITE;
    if (errno == EINTR) return TRANSPORT_WANT_WRITE;
/* treat enotconn during nonblocking connect as retryable backpressure */
    if (errno == ENOTCONN || errno == EINPROGRESS) return TRANSPORT_WANT_WRITE;
    return TRANSPORT_ERR;
}

static void tcp_close(void *h) {
    (void)h; /* the daemon owns the fd, we never opened anything */
}

const transport_vt_t transport_tcp = {
    tcp_open, tcp_read, tcp_write, tcp_write, tcp_close, NULL
};
