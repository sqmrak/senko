#ifndef SOCKS5_CLIENT_H
#define SOCKS5_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include "vless.h"

typedef enum {
    S5C_ST_INIT = 0,
    S5C_ST_GREETING_SENT,
    S5C_ST_AUTH_SENT,
    S5C_ST_REQ_SENT,
    S5C_ST_OPEN,
    S5C_ST_ERROR
} s5c_state_t;

typedef enum {
    S5C_OK = 0,
    S5C_ERR_ARG = -1,
    S5C_NEED_MORE = -2,
    S5C_ERR_PROTO = -3,
    S5C_ERR_AUTH = -4
} s5c_status_t;

typedef struct {
    s5c_state_t  state;
    char         user[64];
    char         pass[64];
    vless_dest_t dest;
} socks5_client_t;

void s5c_init(socks5_client_t *c, const char *user, const char *pass, const vless_dest_t *dest);
int s5c_make_request(socks5_client_t *c, uint8_t *out, size_t cap, size_t *out_len);
int s5c_feed(socks5_client_t *c, const uint8_t *in, size_t in_len, size_t *consumed, uint8_t *out, size_t cap, size_t *out_len);

#endif /* socks5_client_h */
