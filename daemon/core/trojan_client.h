#ifndef TROJAN_CLIENT_H
#define TROJAN_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "vless.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TROJAN_HEX_LEN 56

typedef enum {
    TR_OK        =  0,
    TR_ERR_ARG   = -1,
    TR_ERR_PROTO = -2
} tr_status_t;

typedef struct {
    char         password[64];
    char         hex_hash[TROJAN_HEX_LEN + 1];
    vless_dest_t dest;
    int          initialized;
} trojan_client_t;

int trojan_hash_password(const char *password, char hex_out[TROJAN_HEX_LEN + 1]);

void trojan_client_init(trojan_client_t *c, const char *password, const vless_dest_t *dest);

int trojan_client_build_request(const trojan_client_t *c,
                                const uint8_t *payload, size_t payload_len,
                                uint8_t *out, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* TROJAN_CLIENT_H */
