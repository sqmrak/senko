#ifndef SHADOWSOCKS_CLIENT_H
#define SHADOWSOCKS_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "vless.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SS_CIPHER_UNKNOWN = 0,
    SS_CIPHER_AES_256_GCM,
    SS_CIPHER_AES_128_GCM,
    SS_CIPHER_CHACHA20_POLY1305
} ss_cipher_t;

typedef enum {
    SS_OK            =  0,
    SS_ERR_ARG       = -1,
    SS_ERR_CIPHER    = -2,
    SS_ERR_CRYPTO    = -3,
    SS_ERR_BAD_TAG   = -4,
    SS_NEED_MORE     = -5
} ss_status_t;

typedef struct {
    ss_cipher_t  cipher;
    size_t       key_len;
    size_t       salt_len;

    uint8_t      master_key[32];

    uint8_t      enc_salt[32];
    uint8_t      enc_subkey[32];
    uint8_t      enc_nonce[12];
    int          enc_salt_sent;

    uint8_t      dec_salt[32];
    uint8_t      dec_subkey[32];
    uint8_t      dec_nonce[12];
    int          dec_salt_received;

    vless_dest_t dest;
    int          handshake_done;

    /* Staging buffer for partial incoming AEAD frames */
    uint8_t      dec_stage[68 * 1024];
    size_t       dec_stage_len;
} shadowsocks_client_t;

ss_cipher_t ss_cipher_from_name(const char *name);
const char *ss_cipher_to_name(ss_cipher_t c);

void ss_client_init(shadowsocks_client_t *c, const char *method,
                    const char *password, const vless_dest_t *dest);

int ss_client_build_request(shadowsocks_client_t *c,
                            const uint8_t *payload, size_t payload_len,
                            uint8_t *out, size_t cap, size_t *out_len);

int ss_client_encrypt_chunk(shadowsocks_client_t *c,
                            const uint8_t *plain, size_t plain_len,
                            uint8_t *out, size_t cap, size_t *out_len);

int ss_client_feed_downstream(shadowsocks_client_t *c,
                              const uint8_t *in, size_t in_len,
                              uint8_t *plain_out, size_t plain_cap,
                              size_t *plain_len);

#ifdef __cplusplus
}
#endif

#endif /* SHADOWSOCKS_CLIENT_H */
