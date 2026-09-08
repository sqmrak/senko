#include "grpc_core.h"

#include <stdio.h>
#include <string.h>

static int failed;
static void ok(const char *name, int condition) {
    if (condition) return;
    fprintf(stderr, "FAIL %s\n", name);
    failed++;
}

int main(void) {
    uint8_t wire[20000], decoded[20000];
    size_t wire_len = 0, decoded_len = 0;
    uint8_t payload[256];
    for (size_t i = 0; i < sizeof payload; ++i) payload[i] = (uint8_t)i;
    ok("encode", senko_grpc_encode(payload, sizeof payload, wire,
                                    sizeof wire, &wire_len) == 0);
    ok("encoded length", wire_len == sizeof payload + 8);
    ok("decode", senko_grpc_decode(wire, wire_len, decoded, sizeof decoded,
                                    &decoded_len) == 0);
    ok("decoded payload", decoded_len == sizeof payload &&
       memcmp(decoded, payload, sizeof payload) == 0);
    wire[0] = 1;
    ok("compressed rejected", senko_grpc_decode(wire, wire_len, decoded,
                                                 sizeof decoded, &decoded_len) != 0);
    wire[0] = 0;
    ok("truncated rejected", senko_grpc_decode(wire, wire_len - 1, decoded,
                                               sizeof decoded, &decoded_len) != 0);
    ok("oversized frame rejected", senko_grpc_decode(wire, wire_len + 1, decoded,
                                                     sizeof decoded, &decoded_len) != 0);
    wire[5] = 0x0b;
    ok("wrong field tag rejected", senko_grpc_decode(wire, wire_len, decoded,
                                                     sizeof decoded, &decoded_len) != 0);
    wire[5] = 0x0a;
    ok("bad len arg rejected", senko_grpc_encode(payload, sizeof payload, wire,
                                                 8, &wire_len) != 0);
    ok("null payload with len rejected", senko_grpc_encode(NULL, 5, wire,
                                                           sizeof wire, &wire_len) != 0);
    ok("empty payload encodes", senko_grpc_encode(payload, 0, wire,
                                                  sizeof wire, &wire_len) == 0 &&
       wire_len == 7);
    /* several messages back to back decode one at a time */
    {
        uint8_t two[512];
        size_t one_len = 0, two_len = 0, off = 0;
        ok("msg one", senko_grpc_encode(payload, 3, two, sizeof two, &one_len) == 0);
        ok("msg two", senko_grpc_encode(payload + 3, 4, two + one_len,
                                        sizeof two - one_len, &two_len) == 0);
        two_len += one_len;
        ok("stream msg one", senko_grpc_decode(two + off, one_len,
                                               decoded, sizeof decoded,
                                               &decoded_len) == 0 && decoded_len == 3);
        off += one_len;
        ok("stream msg two", senko_grpc_decode(two + off, two_len - off,
                                               decoded, sizeof decoded,
                                               &decoded_len) == 0 && decoded_len == 4);
        ok("stream fully consumed", off + 5 +
           ((size_t)two[off + 1] << 24 | (size_t)two[off + 2] << 16 |
            (size_t)two[off + 3] << 8 | two[off + 4]) == two_len);
    }
    if (failed) return 1;
    puts("all grpc core checks passed");
    return 0;
}
