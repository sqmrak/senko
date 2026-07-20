#ifndef REALITY_CRYPTO_H
#define REALITY_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC_X25519_KEYLEN  32 /* keep x25519 keys at the wire size */
#define RC_SHARED_LEN     32 /* keep the ecdh output at hash size */
#define RC_GCM_TAGLEN     16
#define RC_GCM_IVLEN      12

typedef enum {
    RC_OK        =  0,
    RC_ERR_ARG   = -1,
    RC_ERR_CRYPTO= -2 /* report a failed crypto primitive */
} rc_status_t;

rc_status_t rc_x25519_keygen(uint8_t priv[RC_X25519_KEYLEN],
                             uint8_t pub[RC_X25519_KEYLEN]);

rc_status_t rc_x25519_public(const uint8_t priv[RC_X25519_KEYLEN],
                             uint8_t pub[RC_X25519_KEYLEN]);

/* derive the shared secret used to authenticate the published server key */
rc_status_t rc_x25519_shared(const uint8_t our_priv[RC_X25519_KEYLEN],
                             const uint8_t their_pub[RC_X25519_KEYLEN],
                             uint8_t shared[RC_SHARED_LEN]);

/* derive output bytes while allowing empty salt and info */
rc_status_t rc_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                           const uint8_t *salt, size_t salt_len,
                           const uint8_t *info, size_t info_len,
                           uint8_t *out, size_t out_len);

/* expose extract separately because tls 1.3 chains multiple schedule steps */
rc_status_t rc_hkdf_extract(const uint8_t *salt, size_t salt_len,
                            const uint8_t *ikm, size_t ikm_len,
                            uint8_t prk[32]);

/* expand a hash-sized prk within the hkdf output limit */
rc_status_t rc_hkdf_expand(const uint8_t prk[32],
                           const uint8_t *info, size_t info_len,
                           uint8_t *out, size_t out_len);

/* seal plaintext and authenticate optional aad with aes-128-gcm */
rc_status_t rc_aes128gcm_seal(const uint8_t key[16], const uint8_t iv[RC_GCM_IVLEN],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *pt, size_t pt_len,
                              uint8_t *ct, uint8_t tag[RC_GCM_TAGLEN]);

/* open aes-128-gcm only after the tag verifies */
rc_status_t rc_aes128gcm_open(const uint8_t key[16], const uint8_t iv[RC_GCM_IVLEN],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ct, size_t ct_len,
                              const uint8_t tag[RC_GCM_TAGLEN],
                              uint8_t *pt);

/* use aes-256-gcm because reality derives a 32-byte authentication key */
rc_status_t rc_aes256gcm_seal(const uint8_t key[32], const uint8_t iv[RC_GCM_IVLEN],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *pt, size_t pt_len,
                              uint8_t *ct, uint8_t tag[RC_GCM_TAGLEN]);

rc_status_t rc_aes256gcm_open(const uint8_t key[32], const uint8_t iv[RC_GCM_IVLEN],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ct, size_t ct_len,
                              const uint8_t tag[RC_GCM_TAGLEN],
                              uint8_t *pt);

/* compute the server proof used by reality authentication */
rc_status_t rc_hmac_sha512(const uint8_t *key, size_t key_len,
                           const uint8_t *msg, size_t msg_len,
                           uint8_t out[64]);

/* compute the finished mac used by the tls 1.3 transcript */
rc_status_t rc_hmac_sha256(const uint8_t *key, size_t key_len,
                           const uint8_t *msg, size_t msg_len,
                           uint8_t out[32]);

/* support the second tls 1.3 aead without changing the sha-256 schedule */
rc_status_t rc_chacha20poly1305_seal(const uint8_t key[32], const uint8_t iv[RC_GCM_IVLEN],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *pt, size_t pt_len,
                                     uint8_t *ct, uint8_t tag[RC_GCM_TAGLEN]);

rc_status_t rc_chacha20poly1305_open(const uint8_t key[32], const uint8_t iv[RC_GCM_IVLEN],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *ct, size_t ct_len,
                                     const uint8_t tag[RC_GCM_TAGLEN],
                                     uint8_t *pt);

#ifdef __cplusplus
}
#endif

#endif /* reality_crypto_h */
