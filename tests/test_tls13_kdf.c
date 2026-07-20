/* pin hkdf label framing so tls 1.3 key derivation cannot drift */
#include "../daemon/core/tls13_kdf.h"
#include "../daemon/core/reality_crypto.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(const char *name, int cond) {
    if (cond) printf("  ok  %s\n", name);
    else { printf(" FAIL %s\n", name); fails++; }
}

static void test_build_label(void) {
    uint8_t out[64];
    size_t n = 0;
    /* label "key", empty context, length 16 */
    ok("build label",
       tls13_build_hkdf_label(16, "key", NULL, 0, out, sizeof out, &n) == TLS13_OK);
    ok("label len", n == 2 + 1 + 6 + 3 + 1); /* uint16 + lbl_len + "tls13 key" + ctx_len */
    ok("length field", out[0] == 0 && out[1] == 16);
    ok("label prefix", out[2] == 9 && memcmp(out + 3, "tls13 key", 9) == 0);
    ok("empty context", out[12] == 0);
}

static void test_expand_label(void) {
    uint8_t secret[32], out[16], again[16];
    memset(secret, 0xab, sizeof secret);
    ok("expand a",
       tls13_expand_label(secret, "key", NULL, 0, out, sizeof out) == TLS13_OK);
    ok("expand b",
       tls13_expand_label(secret, "key", NULL, 0, again, sizeof again) == TLS13_OK);
    ok("expand stable", memcmp(out, again, sizeof out) == 0);
    ok("expand differs by label",
       tls13_expand_label(secret, "iv", NULL, 0, again, sizeof again) == TLS13_OK &&
       memcmp(out, again, sizeof out) != 0);
}

int main(void) {
    fails = 0;
    test_build_label();
    test_expand_label();
    if (fails) {
        printf("%d tls13_kdf checks failed\n", fails);
        return 1;
    }
    printf("all tls13_kdf checks passed\n");
    return 0;
}
