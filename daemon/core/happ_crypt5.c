/* crypt5 is the one happ format that is not a plain rsa block: the payload
   names its own key, carries a chacha20-poly1305 nonce and an rsa wrapped
   content key, and shuffles bytes at three separate points. the layout below
   follows the public happ-decryptor implementation the crypt1-4 keys already
   came from */
#include "happ.h"
#include "b64.h"

#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "happ_crypt5_keys.inc"

#define CRYPT5_NONCE_LEN 12
#define CRYPT5_SALT_LEN  8
#define CRYPT5_KEY_LEN   32
#define CRYPT5_TAG_LEN   16

/* declared in happ.c; the url-safe alphabet and the missing padding are the
   same problem in every part of this format */
int happ_b64_decode_ex(const char *in, size_t in_len,
                       unsigned char *out, size_t cap, size_t *out_len);

/* AB -> BA over each pair. applied to the rsa plaintext and again to the
   chacha plaintext, both times before the text is read as base64 */
static void swap_adjacent(unsigned char *b, size_t n) {
    size_t i;
    for (i = 0; i + 1 < n; i += 2) {
        unsigned char t = b[i];
        b[i] = b[i + 1];
        b[i + 1] = t;
    }
}

/* ABCD -> CDAB over each complete four byte block; its own inverse */
static void swap_block_halves(unsigned char *b, size_t n) {
    size_t i;
    size_t full = n - (n % 4);
    for (i = 0; i < full; i += 4) {
        unsigned char t0 = b[i], t1 = b[i + 1];
        b[i] = b[i + 2];
        b[i + 1] = b[i + 3];
        b[i + 2] = t0;
        b[i + 3] = t1;
    }
}

static const char *key_for_marker(const char marker[8]) {
    size_t i;
    size_t total = sizeof kHappCrypt5Keys / sizeof kHappCrypt5Keys[0];
    for (i = 0; i < total; ++i) {
        if (memcmp(kHappCrypt5Keys[i].marker, marker, 8) == 0)
            return kHappCrypt5Keys[i].pkcs8_b64;
    }
    return NULL;
}

/* the table stores the der body only, so the pem envelope is rebuilt here */
static EVP_PKEY *load_pkcs8_b64(const char *b64) {
    BIO *bio;
    EVP_PKEY *pkey = NULL;
    char *pem;
    size_t blen, cap, o = 0, i;

    if (!b64) return NULL;
    blen = strlen(b64);
    cap = blen + blen / 64 + 128;
    pem = (char *)malloc(cap);
    if (!pem) return NULL;

    o += (size_t)snprintf(pem + o, cap - o, "-----BEGIN PRIVATE KEY-----\n");
    for (i = 0; i < blen && o + 66 < cap; i += 64) {
        size_t chunk = blen - i;
        if (chunk > 64) chunk = 64;
        memcpy(pem + o, b64 + i, chunk);
        o += chunk;
        pem[o++] = '\n';
    }
    o += (size_t)snprintf(pem + o, cap - o, "-----END PRIVATE KEY-----\n");

    bio = BIO_new_mem_buf(pem, (int)o);
    if (bio) {
        pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        BIO_free(bio);
    }
    free(pem);
    return pkey;
}

/* rsa pkcs#1 v1.5, which is what node-forge's default decrypt does */
static int rsa_open(EVP_PKEY *pkey, const unsigned char *in, size_t in_len,
                    unsigned char **out, size_t *out_len) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, NULL);
    unsigned char *buf = NULL;
    size_t need = 0;
    if (!ctx) return -1;
    if (EVP_PKEY_decrypt_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0 ||
        EVP_PKEY_decrypt(ctx, NULL, &need, in, in_len) <= 0 || need == 0) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }
    buf = (unsigned char *)malloc(need);
    if (!buf) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }
    if (EVP_PKEY_decrypt(ctx, buf, &need, in, in_len) <= 0) {
        free(buf);
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }
    EVP_PKEY_CTX_free(ctx);
    *out = buf;
    *out_len = need;
    return 0;
}

/* the tag is the last sixteen bytes of the ciphertext and there is no aad */
static int chacha_open(const unsigned char *key, const unsigned char *nonce,
                       const unsigned char *ct, size_t ct_len,
                       unsigned char *out, size_t *out_len) {
    EVP_CIPHER_CTX *ctx;
    int len = 0, total = 0;
    if (ct_len <= CRYPT5_TAG_LEN) return -1;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, CRYPT5_NONCE_LEN, NULL) != 1 ||
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1 ||
        EVP_DecryptUpdate(ctx, out, &len, ct, (int)(ct_len - CRYPT5_TAG_LEN)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    total = len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, CRYPT5_TAG_LEN,
                            (void *)(uintptr_t)(ct + ct_len - CRYPT5_TAG_LEN)) != 1 ||
        EVP_DecryptFinal_ex(ctx, out + total, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    total += len;
    EVP_CIPHER_CTX_free(ctx);
    *out_len = (size_t)total;
    return 0;
}

/* the body is nonce, an optional salt, a decimal segment length, then the
   chacha ciphertext and the rsa wrapped key. older links leave the salt out,
   which is why the caller tries both readings */
static int open_body(const unsigned char *body, size_t blen, EVP_PKEY *pkey,
                     int salted, char *out, size_t out_cap) {
    const unsigned char *nonce = body;
    const unsigned char *salt = NULL;
    size_t length_start = CRYPT5_NONCE_LEN;
    size_t length_end, segment, packed_len, i;
    const unsigned char *packed;
    unsigned char *rsa_ct = NULL, *rsa_pt = NULL, *ct = NULL, *inter = NULL;
    unsigned char key[CRYPT5_KEY_LEN];
    unsigned char wrapped[CRYPT5_KEY_LEN];
    size_t rsa_ct_len = 0, rsa_pt_len = 0, ct_len = 0, inter_len = 0;
    size_t wrapped_len = 0, plain_len = 0;
    int rc = -1;

    if (blen < 13) return -1;
    if (salted) {
        if (blen < 22) return -1;
        salt = body + 14;
        length_start = 22;
    }

    length_end = length_start;
    while (length_end < blen && body[length_end] >= '0' && body[length_end] <= '9')
        ++length_end;
    if (length_end == length_start || length_end - length_start > 9) return -1;
    segment = 0;
    for (i = length_start; i < length_end; ++i)
        segment = segment * 10 + (size_t)(body[i] - '0');

    packed = body + length_end;
    packed_len = blen - length_end;
    if (packed_len == 0 || segment + 1 > packed_len - 1) return -1;

    /* the wrapped content key follows the ciphertext text, both base64 */
    rsa_ct_len = b64_decoded_maxlen(packed_len - segment - 1);
    rsa_ct = (unsigned char *)malloc(rsa_ct_len ? rsa_ct_len : 1);
    ct_len = b64_decoded_maxlen(segment);
    ct = (unsigned char *)malloc(ct_len ? ct_len : 1);
    if (!rsa_ct || !ct) goto done;
    if (happ_b64_decode_ex((const char *)packed + segment + 1,
                           packed_len - segment - 1,
                           rsa_ct, rsa_ct_len, &rsa_ct_len) != 0)
        goto done;
    if (rsa_open(pkey, rsa_ct, rsa_ct_len, &rsa_pt, &rsa_pt_len) != 0)
        goto done;
    swap_adjacent(rsa_pt, rsa_pt_len);
    wrapped_len = sizeof wrapped;
    if (happ_b64_decode_ex((const char *)rsa_pt, rsa_pt_len,
                           wrapped, sizeof wrapped, &wrapped_len) != 0 ||
        wrapped_len != CRYPT5_KEY_LEN)
        goto done;
    memcpy(key, wrapped, CRYPT5_KEY_LEN);
    if (salt) {
        for (i = 0; i < CRYPT5_KEY_LEN; ++i)
            key[i] ^= salt[i % CRYPT5_SALT_LEN];
    }

    if (happ_b64_decode_ex((const char *)packed + 1, segment, ct, ct_len, &ct_len) != 0)
        goto done;
    inter = (unsigned char *)malloc(ct_len ? ct_len : 1);
    if (!inter) goto done;
    if (chacha_open(key, nonce, ct, ct_len, inter, &inter_len) != 0)
        goto done;

    swap_adjacent(inter, inter_len);
    plain_len = out_cap - 1;
    if (happ_b64_decode_ex((const char *)inter, inter_len,
                           (unsigned char *)out, out_cap - 1, &plain_len) != 0)
        goto done;
    out[plain_len] = '\0';
    rc = plain_len > 0 ? 0 : -1;

done:
    memset(key, 0, sizeof key);
    memset(wrapped, 0, sizeof wrapped);
    if (rsa_pt) { memset(rsa_pt, 0, rsa_pt_len); free(rsa_pt); }
    free(rsa_ct);
    free(ct);
    free(inter);
    return rc;
}

int happ_crypt5_unwrap(const char *payload, char *out, size_t out_cap) {
    unsigned char *shuffled = NULL;
    char marker[8];
    const char *key_b64;
    EVP_PKEY *pkey = NULL;
    size_t n;
    int prefer_salted, rc = -1;

    if (!payload || !out || out_cap < 2) return -1;
    out[0] = '\0';
    n = strlen(payload);
    if (n < 9) return -1;

    shuffled = (unsigned char *)malloc(n);
    if (!shuffled) return -1;
    memcpy(shuffled, payload, n);
    swap_block_halves(shuffled, n);

    memcpy(marker, shuffled, 4);
    memcpy(marker + 4, shuffled + n - 4, 4);
    key_b64 = key_for_marker(marker);
    if (!key_b64) goto done;
    pkey = load_pkcs8_b64(key_b64);
    if (!pkey) goto done;

    /* a digit where the salted layout puts its salt means the older layout */
    prefer_salted = (n - 8) > 12 &&
                    !(shuffled[4 + 12] >= '0' && shuffled[4 + 12] <= '9');
    if (open_body(shuffled + 4, n - 8, pkey, prefer_salted, out, out_cap) == 0 ||
        open_body(shuffled + 4, n - 8, pkey, !prefer_salted, out, out_cap) == 0)
        rc = 0;

done:
    if (pkey) EVP_PKEY_free(pkey);
    free(shuffled);
    if (rc != 0) out[0] = '\0';
    return rc;
}
