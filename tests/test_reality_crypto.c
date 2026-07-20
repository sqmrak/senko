/* pin hkdf to rfc 5869 and keep aead and x25519 round trips self-consistent */
#include "../daemon/core/reality_crypto.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(const char *name, int cond) {
    if (cond) {
        printf("  ok  %s\n", name);
    } else {
        printf(" FAIL %s\n", name);
        fails++;
    }
}

static int hex_eq(const uint8_t *a, const uint8_t *b, size_t n) {
    return memcmp(a, b, n) == 0;
}

/* use the rfc 5869 sha-256 vector to catch hkdf drift */
static void test_hkdf_rfc5869_a1(void) {
    static const uint8_t ikm[22] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b
    };
    static const uint8_t salt[13] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c
    };
    static const uint8_t info[10] = {
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9
    };
    static const uint8_t prk_exp[32] = {
        0x07,0x77,0x09,0x36,0x2c,0x2e,0x32,0xdf,0x0d,0xdc,0x3f,0x0d,0xc4,0x7b,
        0xba,0x63,0x90,0xb6,0xc7,0x3b,0xb5,0x0f,0x9c,0x31,0x22,0xec,0x84,0x4a,
        0xd7,0xc2,0xb3,0xe5
    };
    static const uint8_t okm_exp[42] = {
        0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,
        0x2f,0x2a,0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,
        0xec,0xc4,0xc5,0xbf,0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65
    };

    uint8_t prk[32], okm[42];
    ok("hkdf extract",
       rc_hkdf_extract(salt, sizeof salt, ikm, sizeof ikm, prk) == RC_OK &&
       hex_eq(prk, prk_exp, 32));
    ok("hkdf expand",
       rc_hkdf_expand(prk, info, sizeof info, okm, sizeof okm) == RC_OK &&
       hex_eq(okm, okm_exp, sizeof okm));
    ok("hkdf combined",
       rc_hkdf_sha256(ikm, sizeof ikm, salt, sizeof salt, info, sizeof info,
                      okm, sizeof okm) == RC_OK &&
       hex_eq(okm, okm_exp, sizeof okm));
}

static void test_x25519_roundtrip(void) {
    uint8_t a_priv[32], a_pub[32], a_pub_derived[32], b_priv[32], b_pub[32];
    uint8_t ab[32], ba[32];
    ok("x25519 keygen a", rc_x25519_keygen(a_priv, a_pub) == RC_OK);
    ok("x25519 keygen b", rc_x25519_keygen(b_priv, b_pub) == RC_OK);
    ok("x25519 public", rc_x25519_public(a_priv, a_pub_derived) == RC_OK &&
       hex_eq(a_pub, a_pub_derived, sizeof a_pub));
    ok("x25519 shared ab",
       rc_x25519_shared(a_priv, b_pub, ab) == RC_OK);
    ok("x25519 shared ba",
       rc_x25519_shared(b_priv, a_pub, ba) == RC_OK);
    ok("x25519 shared match", hex_eq(ab, ba, 32));
}

static void test_aes256gcm_roundtrip(void) {
    uint8_t key[32], iv[12], tag[16], ct[32], pt[32], out[32];
    memset(key, 0x11, sizeof key);
    memset(iv, 0x22, sizeof iv);
    for (int i = 0; i < 32; ++i) pt[i] = (uint8_t)i;
    static const uint8_t aad[] = { 0xaa, 0xbb, 0xcc };

    ok("aes256gcm seal",
       rc_aes256gcm_seal(key, iv, aad, sizeof aad, pt, sizeof pt, ct, tag) == RC_OK);
    ok("aes256gcm open",
       rc_aes256gcm_open(key, iv, aad, sizeof aad, ct, sizeof ct, tag, out) == RC_OK &&
       hex_eq(out, pt, sizeof pt));
    tag[0] ^= 0xff;
    ok("aes256gcm bad tag",
       rc_aes256gcm_open(key, iv, aad, sizeof aad, ct, sizeof ct, tag, out) != RC_OK);
}

static void test_chacha_roundtrip(void) {
    uint8_t key[32], iv[12], tag[16], ct[16], pt[16], out[16];
    memset(key, 0x33, sizeof key);
    memset(iv, 0x44, sizeof iv);
    memcpy(pt, "hello-chacha-pad", 16);
    ok("chacha seal",
       rc_chacha20poly1305_seal(key, iv, NULL, 0, pt, sizeof pt, ct, tag) == RC_OK);
    ok("chacha open",
       rc_chacha20poly1305_open(key, iv, NULL, 0, ct, sizeof ct, tag, out) == RC_OK &&
       hex_eq(out, pt, sizeof pt));
}

int main(void) {
    fails = 0;
    test_hkdf_rfc5869_a1();
    test_x25519_roundtrip();
    test_aes256gcm_roundtrip();
    test_chacha_roundtrip();
    if (fails) {
        printf("%d reality_crypto checks failed\n", fails);
        return 1;
    }
    printf("all reality_crypto checks passed\n");
    return 0;
}
