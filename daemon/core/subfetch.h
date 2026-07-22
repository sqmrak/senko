#ifndef SUBFETCH_H
#define SUBFETCH_H

#include <stddef.h>
#include <stdint.h>

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* pass dialing in from the caller */
typedef int (*subfetch_dial_fn)(void *ctx, const char *host, uint16_t port);

/* pump the daemon loop during fetch */
typedef void (*subfetch_pump_fn)(void *ctx);

typedef enum {
    SUBFETCH_OK         =  0,
    SUBFETCH_ERR_ARG    = -1,
    SUBFETCH_ERR_URL    = -2, /* bad url */
    SUBFETCH_ERR_DIAL   = -3, /* dial failed */
    SUBFETCH_ERR_TRANSPORT = -4,/* transport failed */
    SUBFETCH_ERR_HTTP   = -5, /* bad response */
    SUBFETCH_ERR_TOOBIG = -6, /* body is too large */
    SUBFETCH_ERR_REDIRECT = -7 /* redirect failed */
} subfetch_status_t;

typedef struct {
    subfetch_dial_fn dial;
    void            *dial_ctx;
    subfetch_pump_fn pump;
    void            *pump_ctx;
    const transport_vt_t *tcp;
    const transport_vt_t *tls;
    int max_redirects; /* zero uses five hops */
} subfetch_cfg_t;

typedef struct {
    uint64_t expire;
} subfetch_info_t;

/* fetch a body with a timeout */
subfetch_status_t subfetch_get(const subfetch_cfg_t *cfg, const char *url,
                               uint8_t *body_buf, size_t body_cap,
                               size_t *body_len, int timeout_ms);

subfetch_status_t subfetch_get_info(const subfetch_cfg_t *cfg, const char *url,
                                    uint8_t *body_buf, size_t body_cap,
                                    size_t *body_len, int timeout_ms,
                                    subfetch_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* subfetch_h */
