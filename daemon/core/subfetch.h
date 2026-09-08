#ifndef SUBFETCH_H
#define SUBFETCH_H

#include <stddef.h>
#include <stdint.h>

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*subfetch_dial_fn)(void *ctx, const char *host, uint16_t port);

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
    const char *request_header; /* only for the subscription HTTP request */
    int max_redirects; /* zero uses five hops */
} subfetch_cfg_t;

typedef struct {
    uint64_t expire;
    uint64_t upload;
    uint64_t download;
    uint64_t total;
    char description[256];
    char support_url[512];
/* set when the panel answered with a device gated placeholder profile instead
   of the real node list; gate_reason carries the panel's own wording */
    int  gated;
    char gate_reason[256];
} subfetch_info_t;

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

#ifdef SENKO_HOST_TEST
/* the userinfo header parser is worth pinning: an off by one here silently
   turns a valid expiry into a date in the past */
uint64_t subfetch_userinfo_expire_for_test(const char *value);
uint64_t subfetch_userinfo_value_for_test(const char *value, const char *wanted);

/* device gating is decided from response headers alone, so the whole decision
   is reachable from a fixture without a socket */
#include "http.h"
void subfetch_parser_info_for_test(const http_parser_t *hp, subfetch_info_t *info);
#endif

#endif /* subfetch_h */
