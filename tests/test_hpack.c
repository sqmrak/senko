/* hpack vectors straight from rfc 7541 appendix c, plus malformed blocks */
#include "hpack.h"

#include <stdio.h>
#include <string.h>

static int failed;
static void ok(const char *name, int condition) {
    if (condition) return;
    fprintf(stderr, "FAIL %s\n", name);
    failed++;
}

static void str_ok(const char *name, const uint8_t *in, size_t len,
                   const char *want) {
    char out[64];
    size_t used = 0;
    int rc = senko_hpack_string(in, len, out, sizeof out, &used);
    ok(name, rc == 0 && strlen(out) == strlen(want) &&
       memcmp(out, want, strlen(want)) == 0);
}

int main(void) {
/* rfc 7541 C.4.1 */
    static const uint8_t www[] = {
        0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b,
        0xa0, 0xab, 0x90, 0xf4, 0xff
    };
    str_ok("huffman www.example.com", www, sizeof www, "www.example.com");
/* rfc 7541 C.6.1 */
    static const uint8_t nc[] = { 0x86, 0xa8, 0xeb, 0x10, 0x64, 0x9c, 0xbf };
    str_ok("huffman no-cache", nc, sizeof nc, "no-cache");
/* canonical order check: symbol codes are not contiguous in symbol space */
    static const uint8_t cust[] = {
        0x88, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f
    };
    str_ok("huffman custom-key", cust, sizeof cust, "custom-key");
    static const uint8_t zero[] = { 0x81, 0x07 };
    str_ok("huffman single digit", zero, sizeof zero, "0");

/* header block checks */
    senko_hpack_fields_t f;
    static const uint8_t ok200[] = { 0x88 };
    ok("indexed 200", senko_hpack_parse(ok200, sizeof ok200, &f) == 0 &&
       f.have_status && f.status == 200);
    static const uint8_t nf[] = { 0x8d };
    ok("indexed 404", senko_hpack_parse(nf, sizeof nf, &f) == 0 &&
       f.have_status && f.status == 404);
    static const uint8_t st302[] = { 0x48, 0x03, '3', '0', '2' };
    ok("literal status", senko_hpack_parse(st302, sizeof st302, &f) == 0 &&
       f.have_status && f.status == 302);

/* grpc trailers in the grpc-go wire style: huffman names and values */
    static const uint8_t trailers[] = {
        0x40, 0x88, 0x9a, 0xca, 0xc8, 0xb2, 0x12, 0x34, 0xda, 0x8f, 0x81, 0x07
    };
    ok("grpc-status 0", senko_hpack_parse(trailers, sizeof trailers, &f) == 0 &&
       f.have_grpc_status && f.grpc_status == 0);

/* malformed inputs */
    static const uint8_t idx0[] = { 0x80 };
    ok("index 0 rejected", senko_hpack_parse(idx0, sizeof idx0, &f) != 0);
    static const uint8_t trunc[] = { 0x48, 0x03, '3' };
    ok("truncated value rejected", senko_hpack_parse(trunc, sizeof trunc, &f) != 0);
    static const uint8_t badpad[] = { 0x81, 0x00 };
    ok("huffman padding rejected", senko_hpack_parse(badpad, sizeof badpad,
                                                     &f) != 0);
    static const uint8_t sizeupd[] = { 0x3f, 0x10, 0x88 };
    ok("size update skipped", senko_hpack_parse(sizeupd, sizeof sizeupd,
                                                &f) == 0 && f.status == 200);
    ok("empty block", senko_hpack_parse(NULL, 0, &f) != 0);
    ok("empty buffer ok", senko_hpack_parse((const uint8_t *)"", 0, &f) == 0);

/* huffman expansion: a run of 5-bit symbols decodes to 8/5x its encoded size.
   the decoder used to size its scratch at the encoded length and rejected this
   valid block once the decoded form passed 255 bytes. */
    {
        uint8_t blk[200];
        size_t n = 0;
        blk[n++] = 0x00;                 /* literal, without indexing, new name */
        blk[n++] = 0x03;                 /* name: 3 raw bytes */
        blk[n++] = 'x'; blk[n++] = 'y'; blk[n++] = 'z';
        blk[n++] = 0xff; blk[n++] = 0x3d; /* value: huffman, length 188 */
        /* 300 '0' symbols (code 00000) then 4 bits of ones padding */
        for (int i = 0; i < 187; ++i) blk[n++] = 0x00;
        blk[n++] = 0x0f;
        ok("huffman value expands past encoded length",
           senko_hpack_parse(blk, n, &f) == 0);
    }

/* a huffman literal larger than a real header block is skipped, not decoded */
    {
        static uint8_t big[4805];
        size_t n = 0;
        big[n++] = 0x0f; big[n++] = 0x00; /* literal without indexing, name index 15 */
        big[n++] = 0xff; big[n++] = 0xc1; big[n++] = 0x24; /* huffman value, length 4800 */
        while (n < sizeof big) big[n++] = 0x00;
        ok("oversized huffman literal skipped",
           senko_hpack_parse(big, sizeof big, &f) == 0);
    }

    if (failed) return 1;
    puts("all hpack checks passed");
    return 0;
}
