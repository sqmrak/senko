#include "plist_bundle_id.h"

#include <string.h>

#define KEY "CFBundleIdentifier"
#define BPLIST_MAX_OBJECTS 100000u

static int ident_ok(const char *s, size_t n) {
    /* a bundle identifier is reverse dns; refusing the rest keeps hostile bytes
       out of rule matching and logs */
    if (n == 0 || n > 200) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

static int emit(const char *s, size_t n, char *out, size_t cap) {
    if (!ident_ok(s, n) || n >= cap) return -1;
    memcpy(out, s, n);
    out[n] = '\0';
    return (int)n;
}

static int xml_bundle_id(const uint8_t *data, size_t len, char *out, size_t cap) {
    static const char key[] = "<key>" KEY "</key>";
    const char *p = (const char *)data, *end = p + len;
    size_t key_len = sizeof key - 1;
    while ((size_t)(end - p) >= key_len) {
        const char *hit = memchr(p, '<', (size_t)(end - p));
        if (!hit || (size_t)(end - hit) < key_len) return -1;
        if (memcmp(hit, key, key_len) != 0) {
            p = hit + 1;
            continue;
        }
        p = hit + key_len;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
        if ((size_t)(end - p) < 8 || memcmp(p, "<string>", 8) != 0) return -1;
        p += 8;
        const char *close = p;
        while (close < end && *close != '<') ++close;
        if (close >= end || (size_t)(end - close) < 9 || memcmp(close, "</string>", 9) != 0)
            return -1;
        return emit(p, (size_t)(close - p), out, cap);
    }
    return -1;
}

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t offsets_at;
    unsigned offset_size;
    unsigned ref_size;
    uint64_t objects;
} bplist_t;

static int read_be(const uint8_t *p, size_t len, size_t at, unsigned size, uint64_t *out) {
    if (size == 0 || size > 8 || at > len || len - at < size) return -1;
    uint64_t v = 0;
    for (unsigned i = 0; i < size; ++i) v = v << 8 | p[at + i];
    *out = v;
    return 0;
}

static int object_at(const bplist_t *b, uint64_t index, size_t *at) {
    uint64_t off;
    if (index >= b->objects) return -1;
    if (read_be(b->data, b->len, b->offsets_at + (size_t)index * b->offset_size,
                b->offset_size, &off) != 0)
        return -1;
    if (off < 8 || off >= b->len) return -1;
    *at = (size_t)off;
    return 0;
}

/* the marker byte at an object, its low nibble as a length, with the long form
   that follows an 0xf nibble: an int object holding the real length */
static int marker(const bplist_t *b, size_t at, uint8_t *type, uint64_t *count, size_t *body) {
    uint8_t m = b->data[at];
    *type = m & 0xf0;
    *count = m & 0x0f;
    *body = at + 1;
    if (*count != 0x0f) return 0;
    if (*body >= b->len || (b->data[*body] & 0xf0) != 0x10) return -1;
    unsigned size = 1u << (b->data[*body] & 0x0f);
    if (size > 8) return -1;
    if (read_be(b->data, b->len, *body + 1, size, count) != 0) return -1;
    *body += 1 + size;
    return 0;
}

/* an ascii or utf-16 string object copied as ascii; anything wider is refused */
static int string_at(const bplist_t *b, uint64_t index, char *out, size_t cap, size_t *n) {
    size_t at, body;
    uint8_t type;
    uint64_t count;
    if (object_at(b, index, &at) != 0 || marker(b, at, &type, &count, &body) != 0) return -1;
    if (type == 0x50) {
        if (count >= cap || count > b->len - body) return -1;
        memcpy(out, b->data + body, (size_t)count);
    } else if (type == 0x60) {
        if (count >= cap || count > (b->len - body) / 2) return -1;
        for (uint64_t i = 0; i < count; ++i) {
            if (b->data[body + 2 * i] != 0) return -1;
            out[i] = (char)b->data[body + 2 * i + 1];
        }
    } else {
        return -1;
    }
    out[count] = '\0';
    *n = (size_t)count;
    return 0;
}

static int bplist_bundle_id(const uint8_t *data, size_t len, char *out, size_t cap) {
    bplist_t b;
    uint64_t top;
    if (len < 8 + 32 || memcmp(data, "bplist00", 8) != 0) return -1;
    b.data = data;
    b.len = len;
    const uint8_t *trailer = data + len - 32;
    b.offset_size = trailer[6];
    b.ref_size = trailer[7];
    uint64_t offsets_at;
    if (read_be(trailer, 32, 8, 8, &b.objects) != 0 || read_be(trailer, 32, 16, 8, &top) != 0 ||
        read_be(trailer, 32, 24, 8, &offsets_at) != 0)
        return -1;
    if (b.offset_size == 0 || b.offset_size > 8 || b.ref_size == 0 || b.ref_size > 8 ||
        b.objects == 0 || b.objects > BPLIST_MAX_OBJECTS || offsets_at < 8 ||
        offsets_at > len - 32 ||
        b.objects * b.offset_size > (uint64_t)(len - 32 - offsets_at))
        return -1;
    b.offsets_at = (size_t)offsets_at;

    size_t at, body;
    uint8_t type;
    uint64_t count;
    if (object_at(&b, top, &at) != 0 || marker(&b, at, &type, &count, &body) != 0 ||
        type != 0xd0)
        return -1;
    if (count > b.objects || count * 2 * b.ref_size > b.len - body) return -1;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t key_ref, value_ref;
        char key[sizeof KEY + 8];
        size_t key_len;
        if (read_be(data, len, body + (size_t)(i * b.ref_size), b.ref_size, &key_ref) != 0 ||
            read_be(data, len, body + (size_t)((count + i) * b.ref_size), b.ref_size, &value_ref) != 0)
            return -1;
        if (string_at(&b, key_ref, key, sizeof key, &key_len) != 0) continue;
        if (key_len != sizeof KEY - 1 || memcmp(key, KEY, key_len) != 0) continue;
        char value[256];
        size_t value_len;
        if (string_at(&b, value_ref, value, sizeof value, &value_len) != 0) return -1;
        return emit(value, value_len, out, cap);
    }
    return -1;
}

int plist_bundle_id(const uint8_t *data, size_t len, char *out, size_t cap) {
    if (!data || !out || cap < 2) return -1;
    out[0] = '\0';
    if (len >= 8 && memcmp(data, "bplist00", 8) == 0) return bplist_bundle_id(data, len, out, cap);
    return xml_bundle_id(data, len, out, cap);
}
