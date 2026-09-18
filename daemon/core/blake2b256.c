/* BLAKE2b restricted to the one shape senko needs: unkeyed, 32-byte digest,
   no salt or personalization. adapted from the RFC 7693 reference source
   (Samuel Neves, CC0 / OpenSSL / Apache-2.0), trimmed to that single shape so
   there is no key schedule or parameter block to get wrong. */
#include "blake2b256.h"

#include <string.h>

#include <stdint.h>

#define BLAKE2B_BLOCKBYTES 128
#define BLAKE2B_OUTBYTES   32

typedef struct {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t  buf[BLAKE2B_BLOCKBYTES];
    size_t   buflen;
} blake2b256_state;

static const uint64_t kIV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

static const uint8_t kSigma[12][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0 },
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 }
};

static uint64_t load64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static void store64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t rotr64(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64 - n));
}

#define G(r, i, a, b, c, d)                          \
    do {                                             \
        a = a + b + m[kSigma[r][2 * i + 0]];         \
        d = rotr64(d ^ a, 32);                       \
        c = c + d;                                   \
        b = rotr64(b ^ c, 24);                        \
        a = a + b + m[kSigma[r][2 * i + 1]];         \
        d = rotr64(d ^ a, 16);                       \
        c = c + d;                                   \
        b = rotr64(b ^ c, 63);                        \
    } while (0)

#define ROUND(r)                                     \
    do {                                              \
        G(r, 0, v[0], v[4], v[8], v[12]);             \
        G(r, 1, v[1], v[5], v[9], v[13]);             \
        G(r, 2, v[2], v[6], v[10], v[14]);            \
        G(r, 3, v[3], v[7], v[11], v[15]);            \
        G(r, 4, v[0], v[5], v[10], v[15]);            \
        G(r, 5, v[1], v[6], v[11], v[12]);            \
        G(r, 6, v[2], v[7], v[8], v[13]);             \
        G(r, 7, v[3], v[4], v[9], v[14]);             \
    } while (0)

static void compress(blake2b256_state *s, const uint8_t block[BLAKE2B_BLOCKBYTES]) {
    uint64_t m[16], v[16];
    int i;
    for (i = 0; i < 16; ++i) m[i] = load64(block + i * 8);
    for (i = 0; i < 8; ++i) v[i] = s->h[i];
    v[8] = kIV[0]; v[9] = kIV[1]; v[10] = kIV[2]; v[11] = kIV[3];
    v[12] = kIV[4] ^ s->t[0];
    v[13] = kIV[5] ^ s->t[1];
    v[14] = kIV[6] ^ s->f[0];
    v[15] = kIV[7] ^ s->f[1];
    ROUND(0); ROUND(1); ROUND(2); ROUND(3);
    ROUND(4); ROUND(5); ROUND(6); ROUND(7);
    ROUND(8); ROUND(9); ROUND(10); ROUND(11);
    for (i = 0; i < 8; ++i) s->h[i] ^= v[i] ^ v[i + 8];
}

#undef G
#undef ROUND

static void increment_counter(blake2b256_state *s, uint64_t inc) {
    s->t[0] += inc;
    if (s->t[0] < inc) s->t[1]++;
}

void blake2b256(const void *data, size_t len, unsigned char out[32]) {
    blake2b256_state s;
    const uint8_t *in = (const uint8_t *)data;
    size_t left, fill;
    int i;

    memset(&s, 0, sizeof s);
    for (i = 0; i < 8; ++i) s.h[i] = kIV[i];
/* unkeyed, 32-byte digest, fanout=1, depth=1, nothing else set: the packed
   parameter block's first little-endian word collapses to this constant */
    s.h[0] ^= 0x01010000ULL ^ (uint64_t)BLAKE2B_OUTBYTES;

    if (len > 0) {
        left = s.buflen;
        fill = BLAKE2B_BLOCKBYTES - left;
        if (len > fill) {
            memcpy(s.buf + left, in, fill);
            increment_counter(&s, BLAKE2B_BLOCKBYTES);
            compress(&s, s.buf);
            s.buflen = 0;
            in += fill;
            len -= fill;
            while (len > BLAKE2B_BLOCKBYTES) {
                increment_counter(&s, BLAKE2B_BLOCKBYTES);
                compress(&s, in);
                in += BLAKE2B_BLOCKBYTES;
                len -= BLAKE2B_BLOCKBYTES;
            }
        }
        memcpy(s.buf + s.buflen, in, len);
        s.buflen += len;
    }

    increment_counter(&s, (uint64_t)s.buflen);
    s.f[0] = ~(uint64_t)0;
    memset(s.buf + s.buflen, 0, BLAKE2B_BLOCKBYTES - s.buflen);
    compress(&s, s.buf);

    for (i = 0; i < 4; ++i) store64(out + i * 8, s.h[i]);
}
