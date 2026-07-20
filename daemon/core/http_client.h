#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include "vless.h"

typedef enum {
    HC_ST_INIT = 0,
    HC_ST_REQ_SENT,
    HC_ST_OPEN,
    HC_ST_ERROR
} hc_state_t;

typedef enum {
    HC_OK = 0,
    HC_ERR_ARG = -1,
    HC_NEED_MORE = -2,
    HC_ERR_PROTO = -3,
    HC_ERR_AUTH = -4
} hc_status_t;

typedef struct {
    hc_state_t   state;
    char         user[64];
    char         pass[64];
    vless_dest_t dest;
    uint8_t      rbuf[1024];
    size_t       rbuf_len;
} http_client_t;

void hc_init(http_client_t *c, const char *user, const char *pass, const vless_dest_t *dest);
int hc_make_request(http_client_t *c, uint8_t *out, size_t cap, size_t *out_len);
int hc_feed(http_client_t *c, const uint8_t *in, size_t in_len, size_t *consumed);

#endif /* http_client_h */
