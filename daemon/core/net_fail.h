#ifndef NET_FAIL_H
#define NET_FAIL_H

#include <errno.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* why the way to a server failed, as precisely as the layer that saw it.
   core/net_diag.c turns it into the sentence a user reads */
typedef enum {
    NET_FAIL_NONE = 0,
    NET_FAIL_SYSTEM,            /* local socket or resource failure */
    NET_FAIL_NO_ROUTE,          /* the device has no network path there */
    NET_FAIL_REFUSED,           /* nothing listens on the port */
    NET_FAIL_TIMEOUT,           /* the connection attempt got no answer */
    NET_FAIL_RESET,             /* the connection was reset during the handshake */
    NET_FAIL_CLOSED,            /* the server closed the connection during the handshake */
    NET_FAIL_NOT_TLS,           /* the port answered with something that is not tls */
    NET_FAIL_CERT_NAME,         /* the certificate is for another name */
    NET_FAIL_CERT_UNTRUSTED,    /* self signed or an unknown issuer */
    NET_FAIL_CERT_EXPIRED,
    NET_FAIL_CERT_NOT_YET_VALID,
    NET_FAIL_TRANSPORT,         /* the chosen transport did not establish */
    NET_FAIL_TLS,               /* another tls failure */
    NET_FAIL_REALITY,           /* the server answered as its camouflage site */
    NET_FAIL_HTTP_STATUS,       /* value: the http status of the upgrade or stream */
    NET_FAIL_GRPC_STATUS        /* value: the grpc status */
} net_fail_kind_t;

typedef struct {
    net_fail_kind_t kind;
    int             value;
} net_fail_t;

/* bounded http/1 status parsing keeps diagnostic codes out of sscanf overflow */
static inline int net_fail_http_status(const char *buf, size_t len) {
    if (!buf || len < 13) return -1;
    if (memcmp(buf, "HTTP/1.", 7) != 0 || (buf[7] != '0' && buf[7] != '1') ||
        buf[8] != ' ' || buf[9] < '1' || buf[9] > '5' ||
        buf[10] < '0' || buf[10] > '9' || buf[11] < '0' || buf[11] > '9' ||
        (buf[12] != ' ' && buf[12] != '\r')) return -1;
    return (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + buf[11] - '0';
}

/* a failed connect(): no route, nobody listening, or nobody answering */
static inline net_fail_kind_t net_fail_from_errno(int err) {
    switch (err) {
    case ECONNREFUSED: return NET_FAIL_REFUSED;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ENETDOWN:
    case EADDRNOTAVAIL: return NET_FAIL_NO_ROUTE;
    case ECONNRESET: return NET_FAIL_RESET;
    case EPIPE: return NET_FAIL_CLOSED;
    case ETIMEDOUT: return NET_FAIL_TIMEOUT;
    default: return NET_FAIL_SYSTEM;
    }
}

/* the first failure wins: a tls alert seen under a websocket is more precise
   than the websocket's own "handshake failed" that follows it */
static inline void net_fail_note(net_fail_t *f, net_fail_kind_t kind, int value) {
    if (!f || f->kind != NET_FAIL_NONE) return;
    f->kind = kind;
    f->value = value;
}

#ifdef __cplusplus
}
#endif

#endif /* NET_FAIL_H */
