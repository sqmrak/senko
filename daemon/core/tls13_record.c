#include "tls13_record.h"
#include "reality_crypto.h"

#include <string.h>
#include <stdio.h>

/* match tls 1.3 nonce construction without mutating the base iv */
static void build_nonce(const uint8_t iv[TLS13_REC_IV_LEN], uint64_t seq,
                        uint8_t nonce[TLS13_REC_IV_LEN]) {
    memcpy(nonce, iv, TLS13_REC_IV_LEN);
    for (int i = 0; i < 8; ++i) {
        nonce[TLS13_REC_IV_LEN - 1 - i] ^= (uint8_t)(seq >> (8 * i));
    }
}

/* keep aad tied to the fixed tls 1.3 outer record header */
static void build_aad(uint8_t aad[5], size_t ciphertext_len) {
    aad[0] = 0x17; /* application_data, always, in 1.3 */
    aad[1] = 0x03; aad[2] = 0x03; /* legacy_record_version */
    aad[3] = (uint8_t)(ciphertext_len >> 8);
    aad[4] = (uint8_t)(ciphertext_len & 0xff);
}

tls13_rec_status_t tls13_record_seal_suite(tls13_aead_t aead,
                                     const uint8_t *key, const uint8_t iv[TLS13_REC_IV_LEN],
                                     uint64_t seq,
                                     uint8_t content_type,
                                     const uint8_t *content, size_t content_len,
                                     uint8_t *out, size_t cap, size_t *out_len) {
    if (!key || !iv || !out || !out_len) return TLS13_REC_ERR_ARG;
    if (content_len && !content) return TLS13_REC_ERR_ARG;

    size_t inner_len = content_len + 1;
    size_t cipher_len = inner_len + TLS13_REC_TAG_LEN; /* + aead tag */
    size_t total = TLS13_REC_HDR_LEN + cipher_len;
    if (cap < total) return TLS13_REC_ERR_SPACE;

    uint8_t inner[16 * 1024 + 1];
    if (inner_len > sizeof inner) return TLS13_REC_ERR_SPACE;
    if (content_len) memcpy(inner, content, content_len);
    inner[content_len] = content_type;

    uint8_t nonce[TLS13_REC_IV_LEN];
    build_nonce(iv, seq, nonce);

    uint8_t aad[5];
    build_aad(aad, cipher_len);

    memcpy(out, aad, TLS13_REC_HDR_LEN);
    uint8_t *ct = out + TLS13_REC_HDR_LEN;
    uint8_t tag[TLS13_REC_TAG_LEN];

    rc_status_t cr;
    if (aead == TLS13_AEAD_CHACHA20)
        cr = rc_chacha20poly1305_seal(key, nonce, aad, TLS13_REC_HDR_LEN,
                                      inner, inner_len, ct, tag);
    else
        cr = rc_aes128gcm_seal(key, nonce, aad, TLS13_REC_HDR_LEN,
                               inner, inner_len, ct, tag);
    if (cr != RC_OK) return TLS13_REC_ERR_CRYPTO;
    memcpy(ct + inner_len, tag, TLS13_REC_TAG_LEN);

    *out_len = total;
    return TLS13_REC_OK;
}

tls13_rec_status_t tls13_record_seal(const uint8_t key[TLS13_REC_KEY_LEN],
                                     const uint8_t iv[TLS13_REC_IV_LEN],
                                     uint64_t seq,
                                     uint8_t content_type,
                                     const uint8_t *content, size_t content_len,
                                     uint8_t *out, size_t cap, size_t *out_len) {
    return tls13_record_seal_suite(TLS13_AEAD_AES128GCM, key, iv, seq,
                                   content_type, content, content_len,
                                   out, cap, out_len);
}

tls13_rec_status_t tls13_record_open_suite(tls13_aead_t aead,
                                     const uint8_t *key, const uint8_t iv[TLS13_REC_IV_LEN],
                                     uint64_t seq,
                                     const uint8_t *record, size_t record_len,
                                     uint8_t *out, size_t cap, size_t *out_len,
                                     uint8_t *out_type) {
    if (!key || !iv || !record || !out || !out_len) return TLS13_REC_ERR_ARG;
    if (record_len < TLS13_REC_HDR_LEN + TLS13_REC_TAG_LEN) return TLS13_REC_ERR_AUTH;

    size_t cipher_len = record_len - TLS13_REC_HDR_LEN;
    size_t claimed = ((size_t)record[3] << 8) | record[4];
    if (claimed != cipher_len) return TLS13_REC_ERR_AUTH;

    size_t inner_len = cipher_len - TLS13_REC_TAG_LEN;
    if (cap < inner_len) return TLS13_REC_ERR_SPACE;

    const uint8_t *ct = record + TLS13_REC_HDR_LEN;
    const uint8_t *tag = ct + inner_len;

    uint8_t nonce[TLS13_REC_IV_LEN];
    build_nonce(iv, seq, nonce);

    uint8_t inner[16 * 1024 + 256];
    if (inner_len > sizeof inner) return TLS13_REC_ERR_SPACE;

    rc_status_t cr;
    if (aead == TLS13_AEAD_CHACHA20)
        cr = rc_chacha20poly1305_open(key, nonce, record, TLS13_REC_HDR_LEN,
                                      ct, inner_len, tag, inner);
    else
        cr = rc_aes128gcm_open(key, nonce, record, TLS13_REC_HDR_LEN,
                               ct, inner_len, tag, inner);
    if (cr != RC_OK) return TLS13_REC_ERR_AUTH;

    size_t i = inner_len;
    while (i > 0 && inner[i - 1] == 0x00) --i;
    if (i == 0) return TLS13_REC_ERR_AUTH;

    uint8_t type = inner[i - 1];
    size_t content_len = i - 1;

    memcpy(out, inner, content_len);
    *out_len = content_len;
    if (out_type) *out_type = type;
    return TLS13_REC_OK;
}

tls13_rec_status_t tls13_record_open(const uint8_t key[TLS13_REC_KEY_LEN],
                                     const uint8_t iv[TLS13_REC_IV_LEN],
                                     uint64_t seq,
                                     const uint8_t *record, size_t record_len,
                                     uint8_t *out, size_t cap, size_t *out_len,
                                     uint8_t *out_type) {
    return tls13_record_open_suite(TLS13_AEAD_AES128GCM, key, iv, seq,
                                   record, record_len, out, cap, out_len, out_type);
}
