#ifndef SUBFETCH_H
#define SUBFETCH_H

#include <stddef.h>
#include <stdint.h>

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* keep dialing outside the fetcher so tests and daemon transports share it */
typedef int (*subfetch_dial_fn)(void *ctx, const char *host, uint16_t port);

/* pump the daemon loop so socks-riding fetches cannot deadlock */
typedef void (*subfetch_pump_fn)(void *ctx);

typedef enum {
    SUBFETCH_OK         =  0,
    SUBFETCH_ERR_ARG    = -1,
    SUBFETCH_ERR_URL    = -2, /* url didn't parse */
    SUBFETCH_ERR_DIAL   = -3, /* couldn't connect */
    SUBFETCH_ERR_TRANSPORT = -4,/* transport open / io failed */
    SUBFETCH_ERR_HTTP   = -5, /* bad response / non-2xx */
    SUBFETCH_ERR_TOOBIG = -6, /* body bigger than the caller buffer */
    SUBFETCH_ERR_REDIRECT = -7 /* too many redirects or bad location */
} subfetch_status_t;

typedef struct {
    subfetch_dial_fn dial;
    void            *dial_ctx;
    subfetch_pump_fn pump;
    void            *pump_ctx;
    const transport_vt_t *tcp;
    const transport_vt_t *tls;
    int max_redirects; /* 0 -> a sane default of 5 is used */
} subfetch_cfg_t;

/* fetch a body with a bounded timeout and optional loop pumping */
subfetch_status_t subfetch_get(const subfetch_cfg_t *cfg, const char *url,
                               uint8_t *body_buf, size_t body_cap,
                               size_t *body_len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* subfetch_h */
