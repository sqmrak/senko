#include "tls13_handshake.h"
#include "reality_crypto.h"

#include <string.h>

tls13_status_t tls13_finished_key(const uint8_t traffic_secret[TLS13_HASH_LEN],
                                  uint8_t finished_key[TLS13_HASH_LEN]) {
    if (!traffic_secret || !finished_key) return TLS13_ERR_ARG;
    return tls13_expand_label(traffic_secret, "finished", NULL, 0,
                              finished_key, TLS13_HASH_LEN);
}

tls13_status_t tls13_finished_mac(const uint8_t finished_key[TLS13_HASH_LEN],
                                  const uint8_t transcript_hash[TLS13_HASH_LEN],
                                  uint8_t verify_data[TLS13_FINISHED_LEN]) {
    if (!finished_key || !transcript_hash || !verify_data) return TLS13_ERR_ARG;
    if (rc_hmac_sha256(finished_key, TLS13_HASH_LEN,
                       transcript_hash, TLS13_HASH_LEN, verify_data) != RC_OK)
        return TLS13_ERR_CRYPTO;
    return TLS13_OK;
}

static int ct_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t d = 0;
    for (size_t i = 0; i < n; ++i) d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}

tls13_status_t tls13_finished_verify(const uint8_t traffic_secret[TLS13_HASH_LEN],
                                     const uint8_t transcript_hash[TLS13_HASH_LEN],
                                     const uint8_t received[TLS13_FINISHED_LEN]) {
    if (!traffic_secret || !transcript_hash || !received) return TLS13_ERR_ARG;
    uint8_t fkey[TLS13_HASH_LEN], expect[TLS13_FINISHED_LEN];
    tls13_status_t s = tls13_finished_key(traffic_secret, fkey);
    if (s != TLS13_OK) return s;
    s = tls13_finished_mac(fkey, transcript_hash, expect);
    if (s != TLS13_OK) { memset(fkey, 0, sizeof fkey); return s; }
    int eq = ct_equal(expect, received, TLS13_FINISHED_LEN);
    memset(fkey, 0, sizeof fkey);
    memset(expect, 0, sizeof expect);
    return eq ? TLS13_OK : TLS13_ERR_AUTH;
}

/* parser stays narrow because reality only needs serverhello fields */

typedef struct { const uint8_t *p; size_t len; size_t off; int err; } rd_t;

static uint8_t rd_u8(rd_t *r) {
    if (r->err || r->off + 1 > r->len) { r->err = 1; return 0; }
    return r->p[r->off++];
}
static uint16_t rd_u16(rd_t *r) {
    if (r->err || r->off + 2 > r->len) { r->err = 1; return 0; }
    uint16_t v = (uint16_t)((r->p[r->off] << 8) | r->p[r->off + 1]);
    r->off += 2;
    return v;
}
static const uint8_t *rd_bytes(rd_t *r, size_t n) {
    if (r->err || r->off + n > r->len) { r->err = 1; return NULL; }
    const uint8_t *q = r->p + r->off;
    r->off += n;
    return q;
}
static void rd_skip(rd_t *r, size_t n) {
    if (r->err || r->off + n > r->len) { r->err = 1; return; }
    r->off += n;
}

tls13_sh_status_t tls13_parse_serverhello(const uint8_t *msg, size_t len,
                                          tls13_serverhello_t *out) {
    if (!msg || !out) return TLS13_SH_ERR_ARG;
    memset(out, 0, sizeof *out);

    rd_t r = { msg, len, 0, 0 };

    if (rd_u8(&r) != 0x02) return TLS13_SH_ERR_TYPE;
    uint32_t hs_len = (uint32_t)rd_u8(&r) << 16;
    hs_len |= (uint32_t)rd_u8(&r) << 8;
    hs_len |= rd_u8(&r);
    if (r.err) return TLS13_SH_ERR_PARSE;
    if (r.off + hs_len != len) return TLS13_SH_ERR_PARSE;

    out->version = rd_u16(&r);
    rd_skip(&r, 32);
    uint8_t sid_len = rd_u8(&r);
    rd_skip(&r, sid_len);
    out->cipher_suite = rd_u16(&r);
    rd_skip(&r, 1);
    if (r.err) return TLS13_SH_ERR_PARSE;

    uint16_t ext_total = rd_u16(&r);
    if (r.err) return TLS13_SH_ERR_PARSE;
    size_t ext_end = r.off + ext_total;
    if (ext_end > len) return TLS13_SH_ERR_PARSE;

    while (r.off < ext_end && !r.err) {
        uint16_t etype = rd_u16(&r);
        uint16_t elen  = rd_u16(&r);
        if (r.err || r.off + elen > ext_end) return TLS13_SH_ERR_PARSE;
        size_t next = r.off + elen;

        if (etype == 0x002b) { /* supported_versions: the real version */
            if (elen >= 2) {
                out->version = (uint16_t)((r.p[r.off] << 8) | r.p[r.off + 1]);
            }
        } else if (etype == 0x0033) { /* key_share */
            rd_t k = { r.p + r.off, elen, 0, 0 };
            uint16_t group = rd_u16(&k);
            uint16_t klen  = rd_u16(&k);
            const uint8_t *key = rd_bytes(&k, klen);
            if (!k.err && group == 0x001d && klen == 32 && key) {
                memcpy(out->server_x25519, key, 32);
                out->have_key_share = 1;
            }
        }
        r.off = next;
    }
    if (r.err) return TLS13_SH_ERR_PARSE;
    if (!out->have_key_share) return TLS13_SH_ERR_NO_KEYSHARE;
    return TLS13_SH_OK;
}
