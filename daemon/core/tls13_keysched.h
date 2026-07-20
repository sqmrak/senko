#ifndef TLS13_KEYSCHED_H
#define TLS13_KEYSCHED_H

#include <stddef.h>
#include <stdint.h>

#include "tls13_kdf.h" /* share hash size and status definitions */

#ifdef __cplusplus
extern "C" {
#endif

#define TLS13_KEY_LEN  16 /* keep the default aead key size explicit */
#define TLS13_IV_LEN   12 /* keep the tls nonce size explicit */

/* derive the early secret while allowing the no-psk handshake */
tls13_status_t tls13_early_secret(const uint8_t *psk, size_t psk_len,
                                  uint8_t out[TLS13_HASH_LEN]);

/* derive the handshake secret and perform the intermediate derived step */
tls13_status_t tls13_handshake_secret(const uint8_t early[TLS13_HASH_LEN],
                                      const uint8_t *ecdhe, size_t ecdhe_len,
                                      uint8_t out[TLS13_HASH_LEN]);

tls13_status_t tls13_master_secret(const uint8_t handshake[TLS13_HASH_LEN],
                                   uint8_t out[TLS13_HASH_LEN]);

/* derive traffic secrets from the transcript and protocol label */
tls13_status_t tls13_traffic_secret(const uint8_t secret[TLS13_HASH_LEN],
                                    const char *label,
                                    const uint8_t transcript_hash[TLS13_HASH_LEN],
                                    uint8_t out[TLS13_HASH_LEN]);

tls13_status_t tls13_traffic_keys(const uint8_t secret[TLS13_HASH_LEN],
                                  uint8_t key[TLS13_KEY_LEN],
                                  uint8_t iv[TLS13_IV_LEN]);

/* derive suite-sized key material while keeping the legacy wrapper stable */
tls13_status_t tls13_traffic_keys_len(const uint8_t secret[TLS13_HASH_LEN],
                                      uint8_t *key, size_t key_len,
                                      uint8_t iv[TLS13_IV_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* tls13_keysched_h */
