#include "tls13_kdf.h"
#include "reality_crypto.h"

#include <string.h>

tls13_status_t tls13_build_hkdf_label(uint16_t length, const char *label,
                                      const uint8_t *context, size_t context_len,
                                      uint8_t *out, size_t cap, size_t *out_len) {
    if (!label || !out || !out_len) return TLS13_ERR_ARG;
    if (context_len && !context) return TLS13_ERR_ARG;

    static const char prefix[] = "tls13 ";
    size_t plen = sizeof prefix - 1; /* 6, no nul */
    size_t llen = strlen(label);
    size_t full_label = plen + llen;
    if (full_label < 7 || full_label > 255) return TLS13_ERR_ARG; /* label<7..255> */
    if (context_len > 255) return TLS13_ERR_ARG; /* context<0..255> */

    size_t need = 2 + 1 + full_label + 1 + context_len;
    if (cap < need) return TLS13_ERR_ARG;

    size_t o = 0;
    out[o++] = (uint8_t)(length >> 8); /* uint16 length, big-endian */
    out[o++] = (uint8_t)(length & 0xff);
    out[o++] = (uint8_t)full_label; /* label length prefix */
    memcpy(out + o, prefix, plen); o += plen;
    memcpy(out + o, label, llen);  o += llen;
    out[o++] = (uint8_t)context_len; /* context length prefix */
    if (context_len) { memcpy(out + o, context, context_len); o += context_len; }

    *out_len = o;
    return TLS13_OK;
}

tls13_status_t tls13_expand_label(const uint8_t secret[TLS13_HASH_LEN],
                                  const char *label,
                                  const uint8_t *context, size_t context_len,
                                  uint8_t *out, size_t out_len) {
    if (!secret || !label || !out) return TLS13_ERR_ARG;

    uint8_t info[514];
    size_t info_len = 0;
    tls13_status_t s = tls13_build_hkdf_label((uint16_t)out_len, label,
                                              context, context_len,
                                              info, sizeof info, &info_len);
    if (s != TLS13_OK) return s;

    if (rc_hkdf_expand(secret, info, info_len, out, out_len) != RC_OK)
        return TLS13_ERR_CRYPTO;
    return TLS13_OK;
}

tls13_status_t tls13_derive_secret(const uint8_t secret[TLS13_HASH_LEN],
                                   const char *label,
                                   const uint8_t transcript_hash[TLS13_HASH_LEN],
                                   uint8_t out[TLS13_HASH_LEN]) {
    if (!secret || !label || !transcript_hash || !out) return TLS13_ERR_ARG;
    return tls13_expand_label(secret, label,
                              transcript_hash, TLS13_HASH_LEN,
                              out, TLS13_HASH_LEN);
}
