#ifndef TLS13_KDF_H
#define TLS13_KDF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS13_HASH_LEN 32 /* keep schedule hashes at sha-256 size */

typedef enum {
    TLS13_OK        =  0,
    TLS13_ERR_ARG   = -1,
    TLS13_ERR_CRYPTO= -2,
    TLS13_ERR_AUTH  = -3 /* reject a failed mac or finished proof */
} tls13_status_t;

/* centralize tls 1.3 label formatting for schedule parity */
tls13_status_t tls13_expand_label(const uint8_t secret[TLS13_HASH_LEN],
                                  const char *label,
                                  const uint8_t *context, size_t context_len,
                                  uint8_t *out, size_t out_len);

/* derive a secret from the transcript hash */
tls13_status_t tls13_derive_secret(const uint8_t secret[TLS13_HASH_LEN],
                                   const char *label,
                                   const uint8_t transcript_hash[TLS13_HASH_LEN],
                                   uint8_t out[TLS13_HASH_LEN]);

/* expose raw labels so vector tests pin the wire bytes */
tls13_status_t tls13_build_hkdf_label(uint16_t length, const char *label,
                                      const uint8_t *context, size_t context_len,
                                      uint8_t *out, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* tls13_kdf_h */
