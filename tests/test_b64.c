#include "b64.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

static int decodes_to(const char *in, const char *expect) {
    unsigned char out[256];
    size_t n = 0;
    if (b64_decode(in, strlen(in), out, sizeof out, &n) != 0) return 0;
    return n == strlen(expect) && memcmp(out, expect, n) == 0;
}

int main(void) {
    unsigned char out[64];
    char enc[128];
    size_t n = 0;

    ok("padded", decodes_to("aGVsbG8=", "hello"));
    ok("unpadded", decodes_to("aGVsbG8", "hello"));
    ok("url-safe alphabet", decodes_to("Pz8-Pz8_", "?\?>?\?\?"));
    ok("wrapped lines", decodes_to("aGVs\r\nbG8=", "hello"));
    ok("empty", decodes_to("", ""));

    /* a feed served as a text file arrives with a byte order mark in front of
       the payload; those three bytes must not fail the whole body */
    ok("utf-8 bom", decodes_to("\xEF\xBB\xBF" "aGVsbG8=", "hello"));

    /* one lone character carries no whole byte: the input was cut mid group */
    ok("truncated group rejected",
       b64_decode("aGVsbG8=A", 9, out, sizeof out, &n) != 0);
    ok("lone trailing char rejected",
       b64_decode("aGVsbG8Ax", 9, out, sizeof out, &n) != 0);

    /* two payloads glued together decode as one longer one unless the padding
       is treated as the end of the message */
    ok("data after padding rejected",
       b64_decode("aGVsbG8=aGVsbG8=", 16, out, sizeof out, &n) != 0);

    ok("garbage rejected",
       b64_decode("aGVs!G8=", 8, out, sizeof out, &n) != 0);
    ok("short buffer reported",
       b64_decode("aGVsbG8=", 8, out, 2, &n) == -2);

    /* a rejected decode must leave the caller a defined length */
    n = 12345;
    (void)b64_decode("aGVs!G8=", 8, out, sizeof out, &n);
    ok("length cleared on failure", n == 0);

    ok("round trip",
       b64_encode((const unsigned char *)"senko", 5, enc, sizeof enc, &n) == 0 &&
       decodes_to(enc, "senko"));

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all base64 checks passed\n");
    return 0;
}
