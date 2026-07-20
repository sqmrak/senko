#include "tls13_keysched.h"
#include "reality_crypto.h"

#include <string.h>

#include <openssl/evp.h>

/* derive the empty transcript hash so the schedule matches tls 1.3 */
static int empty_hash(uint8_t out[TLS13_HASH_LEN]) {
    unsigned int n = 0;
    if (EVP_Digest((const unsigned char *)"", 0, out, &n, EVP_sha256(), NULL) != 1)
        return -1;
    return (n == TLS13_HASH_LEN) ? 0 : -1;
}

tls13_status_t tls13_early_secret(const uint8_t *psk, size_t psk_len,
                                  uint8_t out[TLS13_HASH_LEN]) {
    if (!out) return TLS13_ERR_ARG;
    uint8_t zero[TLS13_HASH_LEN];
    memset(zero, 0, sizeof zero);

    const uint8_t *ikm = psk;
    size_t ikm_len = psk_len;
    if (!psk || psk_len == 0) { ikm = zero; ikm_len = TLS13_HASH_LEN; }

    if (rc_hkdf_extract(zero, TLS13_HASH_LEN, ikm, ikm_len, out) != RC_OK)
        return TLS13_ERR_CRYPTO;
    return TLS13_OK;
}

tls13_status_t tls13_handshake_secret(const uint8_t early[TLS13_HASH_LEN],
                                      const uint8_t *ecdhe, size_t ecdhe_len,
                                      uint8_t out[TLS13_HASH_LEN]) {
    if (!early || !ecdhe || ecdhe_len == 0 || !out) return TLS13_ERR_ARG;

    uint8_t eh[TLS13_HASH_LEN], derived[TLS13_HASH_LEN];
    if (empty_hash(eh) != 0) return TLS13_ERR_CRYPTO;
    if (tls13_derive_secret(early, "derived", eh, derived) != TLS13_OK)
        return TLS13_ERR_CRYPTO;

    if (rc_hkdf_extract(derived, TLS13_HASH_LEN, ecdhe, ecdhe_len, out) != RC_OK)
        return TLS13_ERR_CRYPTO;
    return TLS13_OK;
}

tls13_status_t tls13_master_secret(const uint8_t handshake[TLS13_HASH_LEN],
                                   uint8_t out[TLS13_HASH_LEN]) {
    if (!handshake || !out) return TLS13_ERR_ARG;

    uint8_t eh[TLS13_HASH_LEN], derived[TLS13_HASH_LEN];
    if (empty_hash(eh) != 0) return TLS13_ERR_CRYPTO;
    if (tls13_derive_secret(handshake, "derived", eh, derived) != TLS13_OK)
        return TLS13_ERR_CRYPTO;

    uint8_t zero[TLS13_HASH_LEN];
    memset(zero, 0, sizeof zero);
    if (rc_hkdf_extract(derived, TLS13_HASH_LEN, zero, TLS13_HASH_LEN, out) != RC_OK)
        return TLS13_ERR_CRYPTO;
    return TLS13_OK;
}

tls13_status_t tls13_traffic_secret(const uint8_t secret[TLS13_HASH_LEN],
                                    const char *label,
                                    const uint8_t transcript_hash[TLS13_HASH_LEN],
                                    uint8_t out[TLS13_HASH_LEN]) {
    if (!secret || !label || !transcript_hash || !out) return TLS13_ERR_ARG;
    return tls13_derive_secret(secret, label, transcript_hash, out);
}

tls13_status_t tls13_traffic_keys_len(const uint8_t secret[TLS13_HASH_LEN],
                                      uint8_t *key, size_t key_len,
                                      uint8_t iv[TLS13_IV_LEN]) {
    if (!secret || !key || !iv) return TLS13_ERR_ARG;
/* derive record keys with the exact tls 1.3 labels */
    if (tls13_expand_label(secret, "key", NULL, 0, key, key_len) != TLS13_OK)
        return TLS13_ERR_CRYPTO;
    if (tls13_expand_label(secret, "iv", NULL, 0, iv, TLS13_IV_LEN) != TLS13_OK)
        return TLS13_ERR_CRYPTO;
    return TLS13_OK;
}

tls13_status_t tls13_traffic_keys(const uint8_t secret[TLS13_HASH_LEN],
                                  uint8_t key[TLS13_KEY_LEN],
                                  uint8_t iv[TLS13_IV_LEN]) {
    return tls13_traffic_keys_len(secret, key, TLS13_KEY_LEN, iv);
}
