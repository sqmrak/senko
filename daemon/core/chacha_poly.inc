/* chacha20 and poly1305 for 32 bit arm, included by reality_crypto.c.
   the armv7 openssl is built without assembly (the mach-o assembler rejects
   openssl's armv4 kernels), so its chacha20 and poly1305 run as plain c.
   every armv7 device the 4.3 slice supports has neon. rfc 8439 */

#include <arm_neon.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CP_BLOCK 64u
#define CP_TAG 16u

static uint32_t cp_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void cp_put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void cp_put_le64(uint8_t *p, uint64_t v) {
    cp_put_le32(p, (uint32_t)v);
    cp_put_le32(p + 4, (uint32_t)(v >> 32));
}

#define CP_ROTL(x, n) vsriq_n_u32(vshlq_n_u32((x), (n)), (x), 32 - (n))

#define CP_QR(a, b, c, d)                                              \
    do {                                                               \
        a = vaddq_u32(a, b); d = veorq_u32(d, a); d = CP_ROTL(d, 16);  \
        c = vaddq_u32(c, d); b = veorq_u32(b, c); b = CP_ROTL(b, 12);  \
        a = vaddq_u32(a, b); d = veorq_u32(d, a); d = CP_ROTL(d, 8);   \
        c = vaddq_u32(c, d); b = veorq_u32(b, c); b = CP_ROTL(b, 7);   \
    } while (0)

static void cp_state(uint32_t s[16], const uint8_t key[32], uint32_t counter,
                     const uint8_t nonce[12]) {
    s[0] = 0x61707865u; s[1] = 0x3320646eu; s[2] = 0x79622d32u; s[3] = 0x6b206574u;
    for (size_t i = 0; i < 8; ++i) s[4 + i] = cp_le32(key + 4 * i);
    s[12] = counter;
    for (size_t i = 0; i < 3; ++i) s[13 + i] = cp_le32(nonce + 4 * i);
}

/* one block, the four rows of the state in one register each */
static void cp_block(const uint32_t s[16], uint8_t out[CP_BLOCK]) {
    uint32x4_t a0 = vld1q_u32(s), b0 = vld1q_u32(s + 4);
    uint32x4_t c0 = vld1q_u32(s + 8), d0 = vld1q_u32(s + 12);
    uint32x4_t a = a0, b = b0, c = c0, d = d0;
    for (int i = 0; i < 10; ++i) {
        CP_QR(a, b, c, d);
        b = vextq_u32(b, b, 1); c = vextq_u32(c, c, 2); d = vextq_u32(d, d, 3);
        CP_QR(a, b, c, d);
        b = vextq_u32(b, b, 3); c = vextq_u32(c, c, 2); d = vextq_u32(d, d, 1);
    }
    vst1q_u8(out,      vreinterpretq_u8_u32(vaddq_u32(a, a0)));
    vst1q_u8(out + 16, vreinterpretq_u8_u32(vaddq_u32(b, b0)));
    vst1q_u8(out + 32, vreinterpretq_u8_u32(vaddq_u32(c, c0)));
    vst1q_u8(out + 48, vreinterpretq_u8_u32(vaddq_u32(d, d0)));
}

/* four consecutive blocks at once, one state word per register and one block
   per lane, then transposed back into byte order while xoring the input */
static void cp_xor4(const uint32_t s[16], const uint8_t *in, uint8_t *out) {
    static const uint32_t lanes[4] = { 0, 1, 2, 3 };
    uint32x4_t init[16], x[16];
    for (int i = 0; i < 16; ++i) init[i] = vdupq_n_u32(s[i]);
    init[12] = vaddq_u32(init[12], vld1q_u32(lanes));
    for (int i = 0; i < 16; ++i) x[i] = init[i];
    for (int i = 0; i < 10; ++i) {
        CP_QR(x[0], x[4], x[8],  x[12]);
        CP_QR(x[1], x[5], x[9],  x[13]);
        CP_QR(x[2], x[6], x[10], x[14]);
        CP_QR(x[3], x[7], x[11], x[15]);
        CP_QR(x[0], x[5], x[10], x[15]);
        CP_QR(x[1], x[6], x[11], x[12]);
        CP_QR(x[2], x[7], x[8],  x[13]);
        CP_QR(x[3], x[4], x[9],  x[14]);
    }
    for (int g = 0; g < 4; ++g) {
        uint32x4_t v0 = vaddq_u32(x[4 * g],     init[4 * g]);
        uint32x4_t v1 = vaddq_u32(x[4 * g + 1], init[4 * g + 1]);
        uint32x4_t v2 = vaddq_u32(x[4 * g + 2], init[4 * g + 2]);
        uint32x4_t v3 = vaddq_u32(x[4 * g + 3], init[4 * g + 3]);
        uint32x4x2_t t = vtrnq_u32(v0, v1);
        uint32x4x2_t u = vtrnq_u32(v2, v3);
        uint32x4_t row[4];
        row[0] = vcombine_u32(vget_low_u32(t.val[0]),  vget_low_u32(u.val[0]));
        row[1] = vcombine_u32(vget_low_u32(t.val[1]),  vget_low_u32(u.val[1]));
        row[2] = vcombine_u32(vget_high_u32(t.val[0]), vget_high_u32(u.val[0]));
        row[3] = vcombine_u32(vget_high_u32(t.val[1]), vget_high_u32(u.val[1]));
        for (int b = 0; b < 4; ++b) {
            size_t at = (size_t)b * CP_BLOCK + (size_t)g * 16;
            vst1q_u8(out + at, veorq_u8(vld1q_u8(in + at), vreinterpretq_u8_u32(row[b])));
        }
    }
}

/* in and out may be the same buffer, every block is read before it is written */
static void cp_chacha_xor(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12],
                          const uint8_t *in, uint8_t *out, size_t len) {
    uint32_t s[16];
    cp_state(s, key, counter, nonce);
    while (len >= 4 * CP_BLOCK) {
        cp_xor4(s, in, out);
        s[12] += 4;
        in += 4 * CP_BLOCK; out += 4 * CP_BLOCK; len -= 4 * CP_BLOCK;
    }
    while (len) {
        uint8_t stream[CP_BLOCK];
        size_t n = len < CP_BLOCK ? len : CP_BLOCK;
        cp_block(s, stream);
        for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)(in[i] ^ stream[i]);
        s[12] += 1;
        in += n; out += n; len -= n;
    }
}

/* poly1305 in five 26 bit limbs, so every product fits the 32x32 to 64 multiply
   of armv7 */
typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
} cp_poly_t;

static void cp_poly_init(cp_poly_t *p, const uint8_t key[32]) {
    p->r[0] = cp_le32(key) & 0x3ffffffu;
    p->r[1] = (cp_le32(key + 3) >> 2) & 0x3ffff03u;
    p->r[2] = (cp_le32(key + 6) >> 4) & 0x3ffc0ffu;
    p->r[3] = (cp_le32(key + 9) >> 6) & 0x3f03fffu;
    p->r[4] = (cp_le32(key + 12) >> 8) & 0x00fffffu;
    memset(p->h, 0, sizeof p->h);
    for (size_t i = 0; i < 4; ++i) p->pad[i] = cp_le32(key + 16 + 4 * i);
}

/* whole 16 byte blocks, each one carrying the 2^128 bit */
static void cp_poly_blocks(cp_poly_t *p, const uint8_t *m, size_t len) {
    const uint32_t r0 = p->r[0], r1 = p->r[1], r2 = p->r[2], r3 = p->r[3], r4 = p->r[4];
    const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4];
    for (; len >= 16; m += 16, len -= 16) {
        h0 += cp_le32(m) & 0x3ffffffu;
        h1 += (cp_le32(m + 3) >> 2) & 0x3ffffffu;
        h2 += (cp_le32(m + 6) >> 4) & 0x3ffffffu;
        h3 += (cp_le32(m + 9) >> 6) & 0x3ffffffu;
        h4 += (cp_le32(m + 12) >> 8) | (1u << 24);
        uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 +
                      (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 +
                      (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 +
                      (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 +
                      (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 +
                      (uint64_t)h3 * r1 + (uint64_t)h4 * r0;
        uint32_t c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffffu;
        d1 += c; c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffffu;
        d2 += c; c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffffu;
        d3 += c; c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffffu;
        d4 += c; c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffffu;
        h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffffu;
        h1 += c;
    }
    p->h[0] = h0; p->h[1] = h1; p->h[2] = h2; p->h[3] = h3; p->h[4] = h4;
}

/* the data, then zeros up to a 16 byte boundary, as the aead construction pads */
static void cp_poly_padded(cp_poly_t *p, const uint8_t *m, size_t len) {
    size_t whole = len & ~(size_t)15;
    cp_poly_blocks(p, m, whole);
    if (len > whole) {
        uint8_t last[16];
        memset(last, 0, sizeof last);
        memcpy(last, m + whole, len - whole);
        cp_poly_blocks(p, last, sizeof last);
    }
}

static void cp_poly_finish(cp_poly_t *p, uint8_t tag[CP_TAG]) {
    uint32_t h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4];
    uint32_t c = h1 >> 26; h1 &= 0x3ffffffu;
    h2 += c; c = h2 >> 26; h2 &= 0x3ffffffu;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffffu;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffffu;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffffu;
    h1 += c;
    /* h - (2^130 - 5), kept only when it did not go negative */
    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffffu;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffffu;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffffu;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffffu;
    uint32_t g4 = h4 + c - (1u << 26);
    uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;
    uint32_t w0 = h0 | (h1 << 26);
    uint32_t w1 = (h1 >> 6) | (h2 << 20);
    uint32_t w2 = (h2 >> 12) | (h3 << 14);
    uint32_t w3 = (h3 >> 18) | (h4 << 8);
    uint64_t f = (uint64_t)w0 + p->pad[0];
    cp_put_le32(tag, (uint32_t)f);
    f = (uint64_t)w1 + p->pad[1] + (f >> 32);
    cp_put_le32(tag + 4, (uint32_t)f);
    f = (uint64_t)w2 + p->pad[2] + (f >> 32);
    cp_put_le32(tag + 8, (uint32_t)f);
    f = (uint64_t)w3 + p->pad[3] + (f >> 32);
    cp_put_le32(tag + 12, (uint32_t)f);
}

/* the tag over aad and ciphertext, with the one time key from block zero */
static void cp_aead_tag(const uint8_t key[32], const uint8_t nonce[12],
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *ct, size_t ct_len, uint8_t tag[CP_TAG]) {
    uint32_t s[16];
    uint8_t block[CP_BLOCK];
    cp_state(s, key, 0, nonce);
    cp_block(s, block);
    cp_poly_t p;
    cp_poly_init(&p, block);
    cp_poly_padded(&p, aad, aad_len);
    cp_poly_padded(&p, ct, ct_len);
    uint8_t lengths[16];
    cp_put_le64(lengths, (uint64_t)aad_len);
    cp_put_le64(lengths + 8, (uint64_t)ct_len);
    cp_poly_blocks(&p, lengths, sizeof lengths);
    cp_poly_finish(&p, tag);
    memset(block, 0, sizeof block);
    memset(&p, 0, sizeof p);
}

static void cp_aead_seal(const uint8_t key[32], const uint8_t nonce[12],
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *pt, size_t len, uint8_t *ct, uint8_t tag[CP_TAG]) {
    cp_chacha_xor(key, 1, nonce, pt, ct, len);
    cp_aead_tag(key, nonce, aad, aad_len, ct, len, tag);
}

/* 0 when the tag matches; nothing is decrypted otherwise */
static int cp_aead_open(const uint8_t key[32], const uint8_t nonce[12],
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *ct, size_t len, const uint8_t tag[CP_TAG],
                        uint8_t *pt) {
    uint8_t expect[CP_TAG];
    cp_aead_tag(key, nonce, aad, aad_len, ct, len, expect);
    uint8_t diff = 0;
    for (size_t i = 0; i < CP_TAG; ++i) diff |= (uint8_t)(expect[i] ^ tag[i]);
    if (diff) return -1;
    cp_chacha_xor(key, 1, nonce, ct, pt, len);
    return 0;
}
