#include "reality_crypto.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/hmac.h>
#include <openssl/core_names.h>
#include <openssl/params.h>

rc_status_t rc_x25519_keygen(uint8_t priv[RC_X25519_KEYLEN],
                             uint8_t pub[RC_X25519_KEYLEN]) {
    if (!priv || !pub) return RC_ERR_ARG;

    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!pctx) return RC_ERR_CRYPTO;

    EVP_PKEY *pkey = NULL;
    rc_status_t rc = RC_ERR_CRYPTO;
    if (EVP_PKEY_keygen_init(pctx) == 1 && EVP_PKEY_keygen(pctx, &pkey) == 1) {
        size_t l = RC_X25519_KEYLEN;
        if (EVP_PKEY_get_raw_private_key(pkey, priv, &l) == 1 && l == RC_X25519_KEYLEN) {
            l = RC_X25519_KEYLEN;
            if (EVP_PKEY_get_raw_public_key(pkey, pub, &l) == 1 && l == RC_X25519_KEYLEN)
                rc = RC_OK;
        }
    }
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(pctx);
    return rc;
}

rc_status_t rc_x25519_public(const uint8_t priv[RC_X25519_KEYLEN],
                             uint8_t pub[RC_X25519_KEYLEN]) {
    if (!priv || !pub) return RC_ERR_ARG;

    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                   priv, RC_X25519_KEYLEN);
    size_t len = RC_X25519_KEYLEN;
    rc_status_t rc = RC_ERR_CRYPTO;
    if (pkey && EVP_PKEY_get_raw_public_key(pkey, pub, &len) == 1 &&
        len == RC_X25519_KEYLEN)
        rc = RC_OK;
    EVP_PKEY_free(pkey);
    return rc;
}

rc_status_t rc_x25519_shared(const uint8_t our_priv[RC_X25519_KEYLEN],
                             const uint8_t their_pub[RC_X25519_KEYLEN],
                             uint8_t shared[RC_SHARED_LEN]) {
    if (!our_priv || !their_pub || !shared) return RC_ERR_ARG;

    EVP_PKEY *priv = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
                                                  our_priv, RC_X25519_KEYLEN);
    EVP_PKEY *pub  = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL,
                                                 their_pub, RC_X25519_KEYLEN);
    rc_status_t rc = RC_ERR_CRYPTO;
    if (priv && pub) {
        EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(priv, NULL);
        if (dctx) {
            size_t l = RC_SHARED_LEN;
            if (EVP_PKEY_derive_init(dctx) == 1 &&
                EVP_PKEY_derive_set_peer(dctx, pub) == 1 &&
                EVP_PKEY_derive(dctx, shared, &l) == 1 && l == RC_SHARED_LEN) {
                rc = RC_OK;
            }
            EVP_PKEY_CTX_free(dctx);
        }
    }
    EVP_PKEY_free(priv);
    EVP_PKEY_free(pub);
    return rc;
}

rc_status_t rc_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                           const uint8_t *salt, size_t salt_len,
                           const uint8_t *info, size_t info_len,
                           uint8_t *out, size_t out_len) {
    if (!ikm || !out) return RC_ERR_ARG;

    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (!kdf) return RC_ERR_CRYPTO;
    EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) return RC_ERR_CRYPTO;

/* assemble params: digest, key(ikm), salt, info. empty salt/info are fine to pass as zero-length */
    OSSL_PARAM params[5];
    int p = 0;
    params[p++] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,
                                                   (char *)"SHA256", 0);
    params[p++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                                    (void *)ikm, ikm_len);
    if (salt && salt_len)
        params[p++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                                        (void *)salt, salt_len);
    if (info && info_len)
        params[p++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
                                                        (void *)info, info_len);
    params[p] = OSSL_PARAM_construct_end();

    rc_status_t rc = (EVP_KDF_derive(kctx, out, out_len, params) == 1)
                     ? RC_OK : RC_ERR_CRYPTO;
    EVP_KDF_CTX_free(kctx);
    return rc;
}

rc_status_t rc_hkdf_extract(const uint8_t *salt, size_t salt_len,
                            const uint8_t *ikm, size_t ikm_len,
                            uint8_t prk[32]) {
    if (!ikm || !prk) return RC_ERR_ARG;

    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (!kdf) return RC_ERR_CRYPTO;
    EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) return RC_ERR_CRYPTO;

/* feed a dummy byte because openssl rejects a null zero-length ikm */
    static const uint8_t empty[1] = { 0 };
    const uint8_t *key = (ikm_len == 0 && !ikm) ? empty : ikm;

    int mode = EVP_KDF_HKDF_MODE_EXTRACT_ONLY;
    OSSL_PARAM params[5];
    int p = 0;
    params[p++] = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode);
    params[p++] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,
                                                   (char *)"SHA256", 0);
    params[p++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                                    (void *)key, ikm_len);
    if (salt) /* pass zero-length salt when the caller provides one */
        params[p++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                                        (void *)salt, salt_len);
    params[p] = OSSL_PARAM_construct_end();

    rc_status_t rc = (EVP_KDF_derive(kctx, prk, 32, params) == 1)
                     ? RC_OK : RC_ERR_CRYPTO;
    EVP_KDF_CTX_free(kctx);
    return rc;
}

rc_status_t rc_hkdf_expand(const uint8_t prk[32],
                           const uint8_t *info, size_t info_len,
                           uint8_t *out, size_t out_len) {
    if (!prk || !out) return RC_ERR_ARG;

    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (!kdf) return RC_ERR_CRYPTO;
    EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) return RC_ERR_CRYPTO;

    int mode = EVP_KDF_HKDF_MODE_EXPAND_ONLY;
    OSSL_PARAM params[5];
    int p = 0;
    params[p++] = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode);
    params[p++] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,
                                                   (char *)"SHA256", 0);
    params[p++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                                    (void *)prk, 32);
    if (info && info_len)
        params[p++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
                                                        (void *)info, info_len);
    params[p] = OSSL_PARAM_construct_end();

    rc_status_t rc = (EVP_KDF_derive(kctx, out, out_len, params) == 1)
                     ? RC_OK : RC_ERR_CRYPTO;
    EVP_KDF_CTX_free(kctx);
    return rc;
}

rc_status_t rc_aes128gcm_seal(const uint8_t key[16], const uint8_t iv[RC_GCM_IVLEN],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *pt, size_t pt_len,
                              uint8_t *ct, uint8_t tag[RC_GCM_TAGLEN]) {
    if (!key || !iv || !tag || (pt_len && (!pt || !ct))) return RC_ERR_ARG;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return RC_ERR_CRYPTO;

    rc_status_t rc = RC_ERR_CRYPTO;
    int len = 0;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, RC_GCM_IVLEN, NULL) != 1) break;
        if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) break;
        if (aad && aad_len &&
            EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) break;
        if (pt_len && EVP_EncryptUpdate(ctx, ct, &len, pt, (int)pt_len) != 1) break;
        if (EVP_EncryptFinal_ex(ctx, ct + len, &len) != 1) break; /* gcm: no extra out */
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, RC_GCM_TAGLEN, tag) != 1) break;
        rc = RC_OK;
    } while (0);

    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

rc_status_t rc_aes128gcm_open(const uint8_t key[16], const uint8_t iv[RC_GCM_IVLEN],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ct, size_t ct_len,
                              const uint8_t tag[RC_GCM_TAGLEN],
                              uint8_t *pt) {
    if (!key || !iv || !tag || (ct_len && (!ct || !pt))) return RC_ERR_ARG;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return RC_ERR_CRYPTO;

    rc_status_t rc = RC_ERR_CRYPTO;
    int len = 0;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, RC_GCM_IVLEN, NULL) != 1) break;
        if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) break;
        if (aad && aad_len &&
            EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) break;
        if (ct_len && EVP_DecryptUpdate(ctx, pt, &len, ct, (int)ct_len) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, RC_GCM_TAGLEN,
                                (void *)tag) != 1) break;
        if (EVP_DecryptFinal_ex(ctx, pt + len, &len) != 1) break; /* tag mismatch -> fail */
        rc = RC_OK;
    } while (0);

    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

rc_status_t rc_aes256gcm_seal(const uint8_t key[32], const uint8_t iv[RC_GCM_IVLEN],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *pt, size_t pt_len,
                              uint8_t *ct, uint8_t tag[RC_GCM_TAGLEN]) {
    if (!key || !iv || !tag || (pt_len && (!pt || !ct))) return RC_ERR_ARG;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return RC_ERR_CRYPTO;

    rc_status_t rc = RC_ERR_CRYPTO;
    int len = 0;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, RC_GCM_IVLEN, NULL) != 1) break;
        if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) break;
        if (aad && aad_len &&
            EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) break;
        if (pt_len && EVP_EncryptUpdate(ctx, ct, &len, pt, (int)pt_len) != 1) break;
        if (EVP_EncryptFinal_ex(ctx, ct + len, &len) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, RC_GCM_TAGLEN, tag) != 1) break;
        rc = RC_OK;
    } while (0);

    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

rc_status_t rc_aes256gcm_open(const uint8_t key[32], const uint8_t iv[RC_GCM_IVLEN],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ct, size_t ct_len,
                              const uint8_t tag[RC_GCM_TAGLEN],
                              uint8_t *pt) {
    if (!key || !iv || !tag || (ct_len && (!ct || !pt))) return RC_ERR_ARG;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return RC_ERR_CRYPTO;

    rc_status_t rc = RC_ERR_CRYPTO;
    int len = 0;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, RC_GCM_IVLEN, NULL) != 1) break;
        if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) break;
        if (aad && aad_len &&
            EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) break;
        if (ct_len && EVP_DecryptUpdate(ctx, pt, &len, ct, (int)ct_len) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, RC_GCM_TAGLEN,
                                (void *)tag) != 1) break;
        if (EVP_DecryptFinal_ex(ctx, pt + len, &len) != 1) break;
        rc = RC_OK;
    } while (0);

    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

rc_status_t rc_chacha20poly1305_seal(const uint8_t key[32], const uint8_t iv[RC_GCM_IVLEN],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *pt, size_t pt_len,
                                     uint8_t *ct, uint8_t tag[RC_GCM_TAGLEN]) {
    if (!key || !iv || !tag || (pt_len && (!pt || !ct))) return RC_ERR_ARG;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return RC_ERR_CRYPTO;

    rc_status_t rc = RC_ERR_CRYPTO;
    int len = 0;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, key, iv) != 1) break;
        if (aad && aad_len &&
            EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) break;
        if (pt_len && EVP_EncryptUpdate(ctx, ct, &len, pt, (int)pt_len) != 1) break;
        if (EVP_EncryptFinal_ex(ctx, ct + len, &len) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, RC_GCM_TAGLEN, tag) != 1) break;
        rc = RC_OK;
    } while (0);

    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

rc_status_t rc_chacha20poly1305_open(const uint8_t key[32], const uint8_t iv[RC_GCM_IVLEN],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *ct, size_t ct_len,
                                     const uint8_t tag[RC_GCM_TAGLEN],
                                     uint8_t *pt) {
    if (!key || !iv || !tag || (ct_len && (!ct || !pt))) return RC_ERR_ARG;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return RC_ERR_CRYPTO;

    rc_status_t rc = RC_ERR_CRYPTO;
    int len = 0;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, key, iv) != 1) break;
        if (aad && aad_len &&
            EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) break;
        if (ct_len && EVP_DecryptUpdate(ctx, pt, &len, ct, (int)ct_len) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, RC_GCM_TAGLEN,
                                (void *)tag) != 1) break;
        if (EVP_DecryptFinal_ex(ctx, pt + len, &len) != 1) break;
        rc = RC_OK;
    } while (0);

    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

rc_status_t rc_hmac_sha512(const uint8_t *key, size_t key_len,
                           const uint8_t *msg, size_t msg_len,
                           uint8_t out[64]) {
    if (!key || !out || (msg_len && !msg)) return RC_ERR_ARG;
    unsigned int outlen = 0;
    if (!HMAC(EVP_sha512(), key, (int)key_len, msg, msg_len, out, &outlen))
        return RC_ERR_CRYPTO;
    return (outlen == 64) ? RC_OK : RC_ERR_CRYPTO;
}

rc_status_t rc_hmac_sha256(const uint8_t *key, size_t key_len,
                           const uint8_t *msg, size_t msg_len,
                           uint8_t out[32]) {
    if (!key || !out || (msg_len && !msg)) return RC_ERR_ARG;
    unsigned int outlen = 0;
    if (!HMAC(EVP_sha256(), key, (int)key_len, msg, msg_len, out, &outlen))
        return RC_ERR_CRYPTO;
    return (outlen == 32) ? RC_OK : RC_ERR_CRYPTO;
}

/* bind reality authentication to the clienthello random and server key */
