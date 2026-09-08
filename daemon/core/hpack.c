/* limiting decode to status fields avoids accepting guessed dynamic-table names
   whose state is unavailable to this transport */
#include "hpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hpack_huffman.inc"

/* a real response header block is capped at ~1 KiB by the transport; refuse a
   single huffman literal larger than four blocks instead of sizing a scratch
   from an unbounded length field */
#define HPACK_HUFF_INPUT_MAX 4096

typedef struct {
    const uint8_t *p;
    size_t len;
    size_t pos;
} hpack_reader_t;

static int reader_u8(hpack_reader_t *r, uint8_t *out) {
    if (r->pos >= r->len) return -1;
    *out = r->p[r->pos++];
    return 0;
}

/* hpack integer with a prefix of prefix_bits (rfc 7541 5.1) */
static int reader_int(hpack_reader_t *r, unsigned prefix_bits, uint32_t *out) {
    uint8_t first;
    uint32_t value;
    uint32_t mult = 1;
    if (prefix_bits == 0 || prefix_bits > 8) return -1;
    if (reader_u8(r, &first) != 0) return -1;
    value = first & ((1u << prefix_bits) - 1u);
    if (value < (1u << prefix_bits) - 1u) {
        *out = value;
        return 0;
    }
    for (;;) {
        uint8_t b;
        if (reader_u8(r, &b) != 0) return -1;
        if (mult > 0x1000000u) return -1; /* hard continuation bound */
        value += (uint32_t)(b & 0x7f) * mult;
        if (!(b & 0x80)) break;
        mult *= 128;
    }
    *out = value;
    return 0;
}

/* canonical huffman decode over the rfc 7541 appendix b table */
static int huff_decode(const uint8_t *in, size_t len,
                       uint8_t *out, size_t cap, size_t *out_len) {
    uint64_t acc = 0; /* 30-bit codes: a byte can push the shift past 32 */
    unsigned bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < len; ++i) {
        acc = (acc << 8) | in[i];
        bits += 8;
        for (;;) {
            int group = -1;
            uint32_t code = 0;
            for (int gi = 0; gi < HUFF_N_LENGTHS; ++gi) {
                unsigned l = huff_len_of[gi];
                if (bits < l) break; /* group lengths are ascending */
                code = (acc >> (bits - l)) & ((1u << l) - 1u);
                if (code >= huff_first_code[gi] &&
                    code < huff_first_code[gi] + huff_count[gi]) {
                    group = gi;
                    break;
                }
            }
            if (group < 0) {
                /* no code is longer than HUFF_MAX_LEN bits, so once that many
                   bits are buffered without a match the string is malformed;
                   letting bits keep growing shifts acc past its width (ub) */
                if (bits >= HUFF_MAX_LEN) return -1;
                break;
            }
            unsigned l = huff_len_of[group];
            uint32_t sym = huff_syms[huff_start[group] +
                                     (code - huff_first_code[group])];
            if (sym >= 256) return -1; /* eos inside a string */
            if (o >= cap) return -1;
            out[o++] = (uint8_t)sym;
            bits -= l;
        }
    }
/* padding must be shorter than a byte and all ones (rfc 7541 5.2) */
    if (bits >= 8) return -1;
    uint64_t mask = (1u << bits) - 1u;
    if ((acc & mask) != mask) return -1;
    *out_len = o;
    return 0;
}

/* one string literal with the huffman flag; when allow_truncate the output
   is cut at cap-1 bytes instead of failing on oversized input */
static int reader_string(hpack_reader_t *r, char *out, size_t cap,
                         int allow_truncate, size_t *out_len) {
    uint8_t first;
    uint32_t length;
    if (reader_u8(r, &first) != 0) return -1;
    int huff = (first & 0x80) != 0;
    r->pos--;
    if (reader_int(r, 7, &length) != 0) return -1;
    if (length > r->len - r->pos) return -1;
    if (!huff) {
        if (length >= cap && !allow_truncate) return -1;
        size_t take = length < cap - 1 ? length : cap - 1;
        memcpy(out, r->p + r->pos, take);
        out[take] = '\0';
        r->pos += length;
        *out_len = take;
        return 0;
    }
    if (length == 0) {
        if (cap) out[0] = '\0';
        *out_len = 0;
        return 0;
    }
    /* a single literal larger than a full header block is never a field this
       decoder extracts; skip it rather than allocate a scratch for it */
    if (length > HPACK_HUFF_INPUT_MAX) {
        if (!allow_truncate) return -1;
        r->pos += length;
        out[0] = '\0';
        *out_len = 0;
        return 0;
    }
    /* huffman codes are at least HUFF_MIN_LEN bits, so the decoded form can
       reach length*8/HUFF_MIN_LEN bytes; sizing the scratch at length (the old
       "never expands" assumption) rejected valid runs of short codes */
    size_t need = length + (length * (8 - HUFF_MIN_LEN) + HUFF_MIN_LEN - 1) / HUFF_MIN_LEN;
    uint8_t tmp[512];
    uint8_t *dst = tmp;
    uint8_t *heap = NULL;
    size_t cap_in = sizeof tmp;
    if (need > sizeof tmp) {
        heap = (uint8_t *)malloc(need);
        if (!heap) return -1;
        dst = heap;
        cap_in = need;
    }
    size_t dl = 0;
    int rc = huff_decode(r->p + r->pos, length, dst, cap_in, &dl);
    if (rc != 0 || (dl >= cap && !allow_truncate)) {
        free(heap);
        return -1;
    }
    size_t take = dl < cap - 1 ? dl : cap - 1;
    memcpy(out, dst, take);   /* dst may be heap, so copy out before freeing it */
    free(heap);
    out[take] = '\0';
    r->pos += length;
    *out_len = take;
    return 0;
}

static void apply_field(senko_hpack_fields_t *out,
                        const char *name, const char *value) {
    if (strcmp(name, ":status") == 0) {
        char *endp = NULL;
        long v = strtol(value, &endp, 10);
        if (endp && *endp == '\0' && v >= 100 && v <= 599) {
            out->have_status = 1;
            out->status = (int)v;
        }
    } else if (strcmp(name, "grpc-status") == 0) {
        char *endp = NULL;
        long v = strtol(value, &endp, 10);
        if (endp && *endp == '\0' && v >= 0 && v <= 65535) {
            out->have_grpc_status = 1;
            out->grpc_status = (int)v;
        }
    } else if (strcmp(name, "grpc-message") == 0) {
        snprintf(out->grpc_message, sizeof out->grpc_message, "%.*s",
                 (int)(sizeof out->grpc_message - 1), value);
    }
}

/* indexed static :status entries (rfc 7541 appendix a) */
static const int h2_status_codes[] = { 200, 204, 206, 304, 400, 404, 500 };

static int parse_entry(hpack_reader_t *r, senko_hpack_fields_t *out) {
    uint8_t first;
    char name[64];
    char value[256];
    if (reader_u8(r, &first) != 0) return -1;

    if (first & 0x80) { /* indexed header field */
        r->pos--;
        uint32_t idx;
        if (reader_int(r, 7, &idx) != 0) return -1;
        if (idx == 0) return -1;
        if (idx >= 8 && idx <= 14) {
            out->have_status = 1;
            out->status = h2_status_codes[idx - 8];
        }
/* index >= 62 references the peer dynamic table, which is not tracked */
        return 0;
    }

    if ((first & 0xe0) == 0x20) { /* dynamic table size update */
        r->pos--;
        uint32_t size;
        return reader_int(r, 5, &size) != 0 ? -1 : 0;
    }

    unsigned name_prefix;
    if ((first & 0xc0) == 0x40) name_prefix = 6; /* incremental indexing */
    else if ((first & 0xf0) == 0x00 || (first & 0xf0) == 0x10)
        name_prefix = 4; /* without indexing / never indexed */
    else return -1;

    r->pos--;
    uint32_t name_idx;
    if (reader_int(r, name_prefix, &name_idx) != 0) return -1;

    int known = 0;
    if (name_idx == 0) {
        size_t nl = 0;
        if (reader_string(r, name, sizeof name, 1, &nl) != 0 || nl == 0)
            return -1;
        known = 1;
    } else if (name_idx >= 8 && name_idx <= 14) {
        snprintf(name, sizeof name, ":status");
        known = 1;
    }
/* a known or unknown name, the value string always follows */
    size_t vl = 0;
    if (reader_string(r, value, sizeof value, 1, &vl) != 0) return -1;
    if (known) apply_field(out, name, value);
    return 0;
}

int senko_hpack_parse(const uint8_t *buf, size_t len, senko_hpack_fields_t *out) {
    if (!buf || !out) return -1;
    memset(out, 0, sizeof *out);
    hpack_reader_t r = { buf, len, 0 };
    while (r.pos < r.len)
        if (parse_entry(&r, out) != 0) return -1;
    return 0;
}

int senko_hpack_string(const uint8_t *buf, size_t len,
                       char *out, size_t cap, size_t *used) {
    if (!buf || !out || !used || cap == 0) return -1;
    hpack_reader_t r = { buf, len, 0 };
    size_t ol = 0;
    if (reader_string(&r, out, cap, 0, &ol) != 0) return -1;
    *used = r.pos;
    return 0;
}
