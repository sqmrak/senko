/* the neon chacha20-poly1305 against rfc 8439 and against openssl */
#if defined(__ARM_NEON) || defined(__ARM_NEON__)

#include "chacha_poly.inc"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>

static int failures;

static void check(int condition, const char *what) {
    if (condition) return;
    fprintf(stderr, "failed: %s\n", what);
    ++failures;
}

static int openssl_seal(const uint8_t key[32], const uint8_t nonce[12],
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *pt, size_t len, uint8_t *ct, uint8_t tag[16]) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int n = 0, ok = ctx != NULL;
    ok = ok && EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, key, nonce) == 1;
    if (ok && aad_len) ok = EVP_EncryptUpdate(ctx, NULL, &n, aad, (int)aad_len) == 1;
    if (ok && len) ok = EVP_EncryptUpdate(ctx, ct, &n, pt, (int)len) == 1;
    ok = ok && EVP_EncryptFinal_ex(ctx, ct + len, &n) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag) == 1;
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

static int openssl_xor(const uint8_t key[32], const uint8_t nonce[12],
                       const uint8_t *in, uint8_t *out, size_t len) {
    uint8_t iv[16] = {0};
    memcpy(iv + 4, nonce, 12);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int n = 0;
    int ok = ctx && EVP_EncryptInit_ex(ctx, EVP_chacha20(), NULL, key, iv) == 1 &&
             (!len || EVP_EncryptUpdate(ctx, out, &n, in, (int)len) == 1);
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

static void rfc8439_vector(void) {
    static const char text[] = "Ladies and Gentlemen of the class of '99: If I could offer you "
                               "only one tip for the future, sunscreen would be it.";
    static const uint8_t aad[12] = { 0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3,
                                     0xc4, 0xc5, 0xc6, 0xc7 };
    static const uint8_t nonce[12] = { 0x07, 0, 0, 0, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
                                       0x46, 0x47 };
    static const uint8_t head[16] = { 0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb,
                                      0x7b, 0x86, 0xaf, 0xbc, 0x53, 0xef, 0x7e, 0xc2 };
    static const uint8_t want_tag[16] = { 0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09, 0xe2, 0x6a,
                                          0x7e, 0x90, 0x2e, 0xcb, 0xd0, 0x60, 0x06, 0x91 };
    uint8_t key[32], ct[sizeof text], tag[16], back[sizeof text];
    for (size_t i = 0; i < 32; ++i) key[i] = (uint8_t)(0x80 + i);
    size_t len = sizeof text - 1;
    cp_aead_seal(key, nonce, aad, sizeof aad, (const uint8_t *)text, len, ct, tag);
    check(memcmp(ct, head, 16) == 0, "rfc 8439 2.8.2 ciphertext");
    check(memcmp(tag, want_tag, 16) == 0, "rfc 8439 2.8.2 tag");
    check(cp_aead_open(key, nonce, aad, sizeof aad, ct, len, tag, back) == 0 &&
          memcmp(back, text, len) == 0, "rfc 8439 2.8.2 opens");
}

/* every length around the block, the four block step and the poly block */
static void against_openssl(void) {
    uint8_t key[32], nonce[12], aad[40], pt[700], ct[700], ref[700], tag[16], ref_tag[16];
    uint8_t back[700];
    for (size_t len = 0; len <= 700; len += (len < 140 || len > 560) ? 1 : 7) {
        RAND_bytes(key, sizeof key);
        RAND_bytes(nonce, sizeof nonce);
        RAND_bytes(aad, sizeof aad);
        RAND_bytes(pt, sizeof pt);
        size_t aad_len = len % 41;
        check(openssl_seal(key, nonce, aad, aad_len, pt, len, ref, ref_tag), "openssl seal");
        cp_aead_seal(key, nonce, aad, aad_len, pt, len, ct, tag);
        check(memcmp(ct, ref, len) == 0 && memcmp(tag, ref_tag, 16) == 0, "seal matches openssl");
        check(cp_aead_open(key, nonce, aad, aad_len, ct, len, tag, back) == 0 &&
              memcmp(back, pt, len) == 0, "open round trip");

        uint8_t stream[700], mine[700];
        check(openssl_xor(key, nonce, pt, stream, len), "openssl xor");
        cp_chacha_xor(key, 0, nonce, pt, mine, len);
        check(memcmp(stream, mine, len) == 0, "keystream from counter zero matches openssl");
    }
}

static void tampering(void) {
    uint8_t key[32] = {1}, nonce[12] = {2}, aad[5] = {3}, pt[100], ct[100], tag[16], out[100];
    memset(pt, 7, sizeof pt);
    cp_aead_seal(key, nonce, aad, sizeof aad, pt, sizeof pt, ct, tag);
    memset(out, 0xaa, sizeof out);
    ct[50] ^= 1;
    check(cp_aead_open(key, nonce, aad, sizeof aad, ct, sizeof ct, tag, out) != 0,
          "flipped ciphertext bit is refused");
    check(out[0] == 0xaa, "a refused packet is not decrypted");
    ct[50] ^= 1;
    tag[15] ^= 0x80;
    check(cp_aead_open(key, nonce, aad, sizeof aad, ct, sizeof ct, tag, out) != 0,
          "flipped tag bit is refused");
    tag[15] ^= 0x80;
    aad[0] ^= 1;
    check(cp_aead_open(key, nonce, aad, sizeof aad, ct, sizeof ct, tag, out) != 0,
          "changed aad is refused");
}

static void in_place(void) {
    uint8_t key[32], nonce[12], buf[300], plain[300], tag[16], ref[300], ref_tag[16];
    RAND_bytes(key, sizeof key);
    RAND_bytes(nonce, sizeof nonce);
    RAND_bytes(plain, sizeof plain);
    memcpy(buf, plain, sizeof plain);
    cp_aead_seal(key, nonce, NULL, 0, buf, sizeof buf, buf, tag);
    check(openssl_seal(key, nonce, NULL, 0, plain, sizeof plain, ref, ref_tag), "openssl seal");
    check(memcmp(buf, ref, sizeof ref) == 0 && memcmp(tag, ref_tag, 16) == 0,
          "in place seal matches openssl");
    check(cp_aead_open(key, nonce, NULL, 0, buf, sizeof buf, tag, buf) == 0 &&
          memcmp(buf, plain, sizeof plain) == 0, "in place open");
}

int main(void) {
    rfc8439_vector();
    against_openssl();
    tampering();
    in_place();
    if (failures) return 1;
    printf("all chacha poly checks passed\n");
    return 0;
}

#else

#include <stdio.h>

int main(void) {
    printf("chacha poly checks skipped: no neon on this host\n");
    return 0;
}

#endif
