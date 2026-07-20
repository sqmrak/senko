#include "b64.h"

#include <stdint.h>

#define B64_SKIP 0xFE /* whitespace / padding, ignore */
#define B64_BAD  0xFF /* not a base64 char at all */

static unsigned char val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A');
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0' + 52);
    if (c == '+' || c == '-') return 62; /* url-safe '-' == '+' */
    if (c == '/' || c == '_') return 63; /* url-safe '_' == '/' */
    if (c == '=' ) return B64_SKIP; /* padding, doesn't add bits */
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t') return B64_SKIP;
    return B64_BAD;
}

size_t b64_decoded_maxlen(size_t in_len) {
    return (in_len / 4 + 1) * 3 + 3;
}

int b64_decode(const char *in, size_t in_len,
               unsigned char *out, size_t cap, size_t *out_len) {
    if (!in || !out || !out_len) return -1;

    size_t o = 0;
    uint_fast32_t acc = 0; /* bit accumulator */
    int nbits = 0; /* bits currently in acc */

    for (size_t i = 0; i < in_len; ++i) {
        unsigned char v = val((unsigned char)in[i]);
        if (v == B64_SKIP) continue;
        if (v == B64_BAD)  return -1; /* real garbage, bail */

        acc = (acc << 6) | v;
        nbits += 6;
        if (nbits >= 8) { /* got a full byte */
            nbits -= 8;
            if (o >= cap) return -2;
            out[o++] = (unsigned char)((acc >> nbits) & 0xff);
        }
    }

    *out_len = o;
    return 0;
}

static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t b64_encoded_maxlen(size_t in_len) {
    return ((in_len + 2) / 3) * 4 + 1;
}

int b64_encode(const unsigned char *in, size_t in_len,
               char *out, size_t cap, size_t *out_len) {
    if (!in || !out || !out_len) return -1;

    size_t o = 0;
    size_t i = 0;
    while (i < in_len) {
        unsigned v = (unsigned)in[i++] << 16;
        int have = 1;
        if (i < in_len) { v |= (unsigned)in[i++] << 8; have = 2; }
        if (i < in_len) { v |= (unsigned)in[i++]; have = 3; }

        if (o + 4 >= cap) return -2;
        out[o++] = b64_alphabet[(v >> 18) & 63];
        out[o++] = b64_alphabet[(v >> 12) & 63];
        out[o++] = have >= 2 ? b64_alphabet[(v >> 6) & 63] : '=';
        out[o++] = have >= 3 ? b64_alphabet[v & 63] : '=';
    }

    if (o >= cap) return -2;
    out[o] = '\0';
    *out_len = o;
    return 0;
}
