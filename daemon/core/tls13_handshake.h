#ifndef TLS13_HANDSHAKE_H
#define TLS13_HANDSHAKE_H

#include <stddef.h>
#include <stdint.h>

#include "tls13_kdf.h" /* share hash size and status definitions */

#ifdef __cplusplus
extern "C" {
#endif

#define TLS13_FINISHED_LEN 32 /* keep finished data at sha-256 size */

tls13_status_t tls13_finished_key(const uint8_t traffic_secret[TLS13_HASH_LEN],
                                  uint8_t finished_key[TLS13_HASH_LEN]);

tls13_status_t tls13_finished_mac(const uint8_t finished_key[TLS13_HASH_LEN],
                                  const uint8_t transcript_hash[TLS13_HASH_LEN],
                                  uint8_t verify_data[TLS13_FINISHED_LEN]);

/* accept finished only when the transcript proof matches in constant time */
tls13_status_t tls13_finished_verify(const uint8_t traffic_secret[TLS13_HASH_LEN],
                                     const uint8_t transcript_hash[TLS13_HASH_LEN],
                                     const uint8_t received[TLS13_FINISHED_LEN]);

typedef struct {
    uint16_t cipher_suite; /* retain the negotiated aead suite */
    uint16_t version; /* retain the negotiated tls version */
    int      have_key_share;
    uint8_t  server_x25519[32]; /* retain the server x25519 share */
} tls13_serverhello_t;

typedef enum {
    TLS13_SH_OK         =  0,
    TLS13_SH_ERR_ARG    = -1,
    TLS13_SH_ERR_PARSE  = -2, /* reject malformed or truncated input */
    TLS13_SH_ERR_TYPE   = -3, /* reject messages other than serverhello */
    TLS13_SH_ERR_NO_KEYSHARE = -4 /* reject a server without x25519 */
} tls13_sh_status_t;

/* require the serverhello x25519 share used by the client */
tls13_sh_status_t tls13_parse_serverhello(const uint8_t *msg, size_t len,
                                          tls13_serverhello_t *out);

#ifdef __cplusplus
}
#endif

#endif /* tls13_handshake_h */
