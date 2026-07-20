#include "tls_clienthello.h"

#include <string.h>

/* writer helpers keep length backpatches local to one buffer */
typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
    int      err;
} w_t;

static void w_u8(w_t *w, uint8_t v) {
    if (w->err) return;
    if (w->pos + 1 > w->cap) { w->err = 1; return; }
    w->buf[w->pos++] = v;
}
static void w_u16(w_t *w, uint16_t v) {
    w_u8(w, (uint8_t)(v >> 8));
    w_u8(w, (uint8_t)(v & 0xff));
}
static void w_bytes(w_t *w, const uint8_t *b, size_t n) {
    if (w->err) return;
    if (w->pos + n > w->cap) { w->err = 1; return; }
    if (n) memcpy(w->buf + w->pos, b, n);
    w->pos += n;
}
static size_t w_mark_u16(w_t *w) { size_t at = w->pos; w_u16(w, 0); return at; }
static void w_patch_u16(w_t *w, size_t at) {
    if (w->err) return;
    size_t len = w->pos - (at + 2);
    w->buf[at] = (uint8_t)(len >> 8);
    w->buf[at + 1] = (uint8_t)(len & 0xff);
}
static size_t w_mark_u24(w_t *w) { size_t at = w->pos; w_u8(w,0); w_u8(w,0); w_u8(w,0); return at; }
static void w_patch_u24(w_t *w, size_t at) {
    if (w->err) return;
    size_t len = w->pos - (at + 3);
    w->buf[at] = (uint8_t)(len >> 16);
    w->buf[at + 1] = (uint8_t)(len >> 8);
    w->buf[at + 2] = (uint8_t)(len & 0xff);
}

/* grease must be stable within one hello but vary per connection */
typedef struct { uint32_t s; } prng_t;
static void prng_seed(prng_t *p, const uint8_t seed[4]) {
    p->s = ((uint32_t)seed[0] << 24) | ((uint32_t)seed[1] << 16) |
           ((uint32_t)seed[2] << 8) | seed[3];
    if (p->s == 0) p->s = 0x9e3779b9;
}
static uint32_t prng_next(prng_t *p) {
    p->s ^= p->s << 13; p->s ^= p->s >> 17; p->s ^= p->s << 5;
    return p->s;
}
static uint16_t prng_grease(prng_t *p) {
    static const uint16_t v[] = {0x0a0a,0x1a1a,0x2a2a,0x3a3a,0x4a4a,0x5a5a,
        0x6a6a,0x7a7a,0x8a8a,0x9a9a,0xaaaa,0xbaba,0xcaca,0xdada,0xeaea,0xfafa};
    return v[prng_next(p) & 15];
}

static void ext_sni(w_t *w, const char *sni) {
    size_t sl = strlen(sni);
    w_u16(w, 0x0000);
    size_t ext = w_mark_u16(w);
      size_t list = w_mark_u16(w);
        w_u8(w, 0x00);
        w_u16(w, (uint16_t)sl);
        w_bytes(w, (const uint8_t *)sni, sl);
      w_patch_u16(w, list);
    w_patch_u16(w, ext);
}
static void ext_ems(w_t *w)   { w_u16(w, 0x0017); w_u16(w, 0x0000); }
static void ext_reneg(w_t *w) { w_u16(w, 0xff01); w_u16(w, 0x0001); w_u8(w, 0x00); }
static void ext_ec_points(w_t *w) { w_u16(w, 0x000b); w_u16(w, 0x0002); w_u8(w, 0x01); w_u8(w, 0x00); }
static void ext_session_ticket(w_t *w) { w_u16(w, 0x0023); w_u16(w, 0x0000); }
static void ext_psk_modes(w_t *w) { w_u16(w, 0x002d); w_u16(w, 0x0002); w_u8(w, 0x01); w_u8(w, 0x01); }
static void ext_sct(w_t *w) { w_u16(w, 0x0012); w_u16(w, 0x0000); }

static void ext_alpn(w_t *w) {
    w_u16(w, 0x0010);
    size_t ext = w_mark_u16(w);
      size_t list = w_mark_u16(w);
        w_u8(w, 2); w_bytes(w, (const uint8_t *)"h2", 2);
        w_u8(w, 8); w_bytes(w, (const uint8_t *)"http/1.1", 8);
      w_patch_u16(w, list);
    w_patch_u16(w, ext);
}
static void ext_status_request(w_t *w) {
    w_u16(w, 0x0005); w_u16(w, 0x0005);
    w_u8(w, 0x01); w_u16(w, 0x0000); w_u16(w, 0x0000);
}
static void ext_grease_empty(w_t *w, uint16_t val) { w_u16(w, val); w_u16(w, 0x0000); }
static void ext_grease_1byte(w_t *w, uint16_t val) { w_u16(w, val); w_u16(w, 0x0001); w_u8(w, 0x00); }

static void ext_key_share_x25519(w_t *w, const uint8_t pub[32], uint16_t grease) {
    w_u16(w, 0x0033);
    size_t ext = w_mark_u16(w);
      size_t shares = w_mark_u16(w);
        if (grease) { w_u16(w, grease); w_u16(w, 0x0001); w_u8(w, 0x00); }
        w_u16(w, 0x001d); w_u16(w, 32); w_bytes(w, pub, 32);
      w_patch_u16(w, shares);
    w_patch_u16(w, ext);
}
static void ext_key_share_firefox(w_t *w, const uint8_t x25519[32], const uint8_t *p256) {
    w_u16(w, 0x0033);
    size_t ext = w_mark_u16(w);
      size_t shares = w_mark_u16(w);
        w_u16(w, 0x001d); w_u16(w, 32); w_bytes(w, x25519, 32);
        if (p256) { w_u16(w, 0x0017); w_u16(w, 65); w_bytes(w, p256, 65); }
      w_patch_u16(w, shares);
    w_patch_u16(w, ext);
}

static void ext_groups_chrome(w_t *w, uint16_t grease) {
    static const uint16_t g[] = {0x001d, 0x0017, 0x0018};
    w_u16(w, 0x000a);
    size_t ext = w_mark_u16(w);
      size_t list = w_mark_u16(w);
        if (grease) w_u16(w, grease);
        for (size_t i = 0; i < 3; i++) w_u16(w, g[i]);
      w_patch_u16(w, list);
    w_patch_u16(w, ext);
}
static void ext_groups_firefox(w_t *w) {
    static const uint16_t g[] = {0x001d, 0x0017, 0x0018, 0x0019, 0x0100, 0x0101};
    w_u16(w, 0x000a);
    size_t ext = w_mark_u16(w);
      size_t list = w_mark_u16(w);
        for (size_t i = 0; i < 6; i++) w_u16(w, g[i]);
      w_patch_u16(w, list);
    w_patch_u16(w, ext);
}
static void ext_groups_x25519_p256(w_t *w) { /* randomized */
    w_u16(w, 0x000a);
    size_t ext = w_mark_u16(w);
      size_t list = w_mark_u16(w);
        w_u16(w, 0x001d); w_u16(w, 0x0017);
      w_patch_u16(w, list);
    w_patch_u16(w, ext);
}

static void ext_versions_tls13(w_t *w) {
    w_u16(w, 0x002b); w_u16(w, 0x0003); w_u8(w, 0x02); w_u16(w, 0x0304);
}
static void ext_versions_chrome(w_t *w, uint16_t grease) {
    w_u16(w, 0x002b); w_u16(w, 0x0007); w_u8(w, 0x06);
    w_u16(w, grease); w_u16(w, 0x0304); w_u16(w, 0x0303);
}
static void ext_versions_firefox(w_t *w) {
    w_u16(w, 0x002b); w_u16(w, 0x0005); w_u8(w, 0x04);
    w_u16(w, 0x0304); w_u16(w, 0x0303);
}

static void ext_sigalgs(w_t *w, const uint16_t *algs, size_t n) {
    w_u16(w, 0x000d);
    size_t ext = w_mark_u16(w);
      size_t list = w_mark_u16(w);
        for (size_t i = 0; i < n; i++) w_u16(w, algs[i]);
      w_patch_u16(w, list);
    w_patch_u16(w, ext);
}
static void ext_sigalgs_default(w_t *w) {
    static const uint16_t a[] = {0x0403,0x0804,0x0401,0x0503,0x0805,0x0501,0x0806,0x0601};
    ext_sigalgs(w, a, sizeof a / sizeof a[0]);
}
static void ext_sigalgs_firefox(w_t *w) {
    static const uint16_t a[] = {0x0403,0x0503,0x0603,0x0804,0x0805,0x0806,0x0401,0x0501,0x0601,0x0203,0x0201};
    ext_sigalgs(w, a, sizeof a / sizeof a[0]);
}

static void ext_delegated_creds(w_t *w) {
    static const uint8_t d[] = {0x00,0x08,0x04,0x03,0x05,0x03,0x06,0x03,0x02,0x03};
    w_u16(w, 0x0022); w_u16(w, (uint16_t)sizeof d); w_bytes(w, d, sizeof d);
}
static void ext_record_size_limit(w_t *w) {
    w_u16(w, 0x001c); w_u16(w, 0x0002); w_u8(w, 0x40); w_u8(w, 0x01);
}
static void ext_ech_grease(w_t *w, prng_t *pr) {
    w_u16(w, 0xfe0d);
    size_t ext = w_mark_u16(w);
      w_u8(w, 0x00);
      w_u16(w, 0x0001); /* kdf */
      w_u16(w, 0x0001); /* aead */
      w_u8(w, (uint8_t)prng_next(pr));
      w_u16(w, 0x0020);
      for (int i = 0; i < 32; i++) w_u8(w, (uint8_t)prng_next(pr));
      w_u16(w, 0x00ef); /* 239 */
      for (int i = 0; i < 239; i++) w_u8(w, (uint8_t)prng_next(pr));
    w_patch_u16(w, ext);
}
static void ext_alps_h2(w_t *w) {
    static const uint8_t d[] = {0x00,0x03,0x02,0x68,0x32};
    w_u16(w, 0x4469); w_u16(w, (uint16_t)sizeof d); w_bytes(w, d, sizeof d);
}

static void ciphers_chrome(w_t *w, uint16_t grease) {
    static const uint16_t c[] = {0x1301,0x1303,0xc02b,0xc02f,0xc02c,0xc030,
        0xcca9,0xcca8,0xc013,0xc014,0x009c,0x009d,0x002f,0x0035};
    size_t at = w_mark_u16(w);
      w_u16(w, grease);
      for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) w_u16(w, c[i]);
    w_patch_u16(w, at);
}
static void ciphers_firefox(w_t *w) {
    static const uint16_t c[] = {0x1301,0x1303,0xc02b,0xc02f,0xcca9,0xcca8,
        0xc02c,0xc030,0xc00a,0xc009,0xc013,0xc014,0x009c,0x009d,0x002f,0x0035};
    size_t at = w_mark_u16(w);
      for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) w_u16(w, c[i]);
    w_patch_u16(w, at);
}
static void ciphers_min(w_t *w) { /* randomized: 1301 + 1303 (our two) */
    size_t at = w_mark_u16(w);
      w_u16(w, 0x1301); w_u16(w, 0x1303);
    w_patch_u16(w, at);
}

/* profiles keep the supported reality fingerprints explicit */

static void exts_chrome(w_t *w, const tls_ch_params_t *p, prng_t *pr, int edge) {
    (void)edge; /* edge == chrome here */
    uint16_t g1 = prng_grease(pr), g2 = prng_grease(pr);
    uint16_t gg = prng_grease(pr), gv = prng_grease(pr);
    size_t exts = w_mark_u16(w);
      ext_grease_empty(w, g1);
      if (p->sni && p->sni[0]) ext_sni(w, p->sni);
      ext_ems(w);
      ext_reneg(w);
      ext_groups_chrome(w, gg);
      ext_ec_points(w);
      ext_session_ticket(w);
      ext_alpn(w);
      ext_status_request(w);
      ext_sigalgs_default(w);
      ext_sct(w);
      ext_key_share_x25519(w, p->x25519_pub, gg);
      ext_psk_modes(w);
      ext_versions_chrome(w, gv);
      ext_alps_h2(w); /* match chrome alps */
      ext_grease_1byte(w, g2);
    w_patch_u16(w, exts);
}

static void exts_qq(w_t *w, const tls_ch_params_t *p, prng_t *pr) {
    exts_chrome(w, p, pr, 0);
}

static void exts_firefox(w_t *w, const tls_ch_params_t *p, prng_t *pr) {
    size_t exts = w_mark_u16(w);
      if (p->sni && p->sni[0]) ext_sni(w, p->sni);
      ext_ems(w);
      ext_reneg(w);
      ext_groups_firefox(w);
      ext_ec_points(w);
      ext_session_ticket(w);
      ext_alpn(w);
      ext_status_request(w);
      ext_delegated_creds(w);
      ext_key_share_firefox(w, p->x25519_pub, p->p256_pub);
      ext_versions_firefox(w);
      ext_sigalgs_firefox(w);
      ext_psk_modes(w);
      ext_record_size_limit(w);
      ext_ech_grease(w, pr);
    w_patch_u16(w, exts);
}

static void exts_randomized(w_t *w, const tls_ch_params_t *p) {
/* fixed order (no shuffle: our transcript must be reproducible for the seal), minimal browser-agnostic set */
    size_t exts = w_mark_u16(w);
      if (p->sni && p->sni[0]) ext_sni(w, p->sni);
      ext_groups_x25519_p256(w);
      ext_sigalgs_default(w);
      ext_alpn(w);
      ext_versions_tls13(w);
      ext_psk_modes(w);
      ext_key_share_x25519(w, p->x25519_pub, 0);
      ext_ec_points(w);
    w_patch_u16(w, exts);
}

tls_ch_status_t tls_build_clienthello(const tls_ch_params_t *p,
                                      uint8_t *buf, size_t cap, size_t *out_len) {
    if (!p || !buf || !out_len) return TLS_CH_ERR_ARG;
    if (p->sni && strlen(p->sni) > 4096) return TLS_CH_ERR_SNI;

    prng_t pr; prng_seed(&pr, p->random); /* per-connection grease/ech */
    uint16_t cipher_grease = prng_grease(&pr);

    w_t w = { buf, cap, 0, 0 };

    w_u8(&w, 0x01);
    size_t hs_len = w_mark_u24(&w);

    w_u16(&w, 0x0303); /* retain the tls 1.2 legacy field */
    w_bytes(&w, p->random, TLS_CH_RANDOM_LEN);

    w_u8(&w, TLS_CH_SESSIONID_LEN); /* reality's session_id slot */
    w_bytes(&w, p->session_id, TLS_CH_SESSIONID_LEN);

    switch (p->fp) {
        case TLS_FP_FIREFOX:    ciphers_firefox(&w); break;
        case TLS_FP_RANDOMIZED: ciphers_min(&w); break;
        default:                ciphers_chrome(&w, cipher_grease); break;
    }

    w_u8(&w, 0x01); w_u8(&w, 0x00); /* compression: null */

    switch (p->fp) {
        case TLS_FP_FIREFOX:    exts_firefox(&w, p, &pr); break;
        case TLS_FP_QQ:         exts_qq(&w, p, &pr); break;
        case TLS_FP_RANDOMIZED: exts_randomized(&w, p); break;
        case TLS_FP_EDGE:       exts_chrome(&w, p, &pr, 1); break;
        default:                exts_chrome(&w, p, &pr, 0); break;
    }

    w_patch_u24(&w, hs_len);

    if (w.err) return TLS_CH_ERR_SPACE;
    *out_len = w.pos;
    return TLS_CH_OK;
}
