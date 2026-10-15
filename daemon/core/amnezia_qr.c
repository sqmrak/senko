#include "amnezia_qr.h"

#include "b64.h"

#include <stdlib.h>
#include <string.h>

#define AMZ_QR_MAGIC 1984u
#define AMZ_QR_HEADER 8u

void amz_qr_init(amz_qr_t *q) {
    if (q) memset(q, 0, sizeof *q);
}

void amz_qr_free(amz_qr_t *q) {
    if (!q) return;
    for (size_t i = 0; i < AMZ_QR_MAX_PARTS; ++i) free(q->part[i]);
    amz_qr_init(q);
}

/* frames carry no whitespace, but a decoder may hand back a trailing newline */
static size_t trimmed_len(const char *s, size_t n) {
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) --n;
    return n;
}

amz_qr_status_t amz_qr_add(amz_qr_t *q, const char *text, size_t len) {
    if (!q || !text) return AMZ_QR_NOT_FRAME;
    len = trimmed_len(text, len);
    if (len < 8 || len > AMZ_QR_MAX_FRAME_TEXT) return AMZ_QR_NOT_FRAME;

    unsigned char raw[AMZ_QR_MAX_FRAME_TEXT];
    size_t n = 0;
    if (b64_decode(text, len, raw, sizeof raw, &n) != 0 || n < AMZ_QR_HEADER)
        return AMZ_QR_NOT_FRAME;
    if ((((unsigned)raw[0] << 8) | raw[1]) != AMZ_QR_MAGIC) return AMZ_QR_NOT_FRAME;

    unsigned count = raw[2];
    unsigned index = raw[3];
    size_t body = ((size_t)raw[4] << 24) | ((size_t)raw[5] << 16) |
                  ((size_t)raw[6] << 8) | (size_t)raw[7];
    if (count == 0 || index >= count || body != n - AMZ_QR_HEADER)
        return AMZ_QR_NOT_FRAME;

    if (q->count != count) amz_qr_free(q);
    if (q->part[index]) return q->have == q->count ? AMZ_QR_DONE : AMZ_QR_REPEAT;
    if (q->total + body > AMZ_QR_MAX_BYTES) {
        amz_qr_free(q);
        return AMZ_QR_ERR_SPACE;
    }

    unsigned char *copy = (unsigned char *)malloc(body ? body : 1);
    if (!copy) return AMZ_QR_ERR_SPACE;
    memcpy(copy, raw + AMZ_QR_HEADER, body);
    q->count = (uint8_t)count;
    q->part[index] = copy;
    q->part_len[index] = body;
    q->total += body;
    q->have++;
    return q->have == q->count ? AMZ_QR_DONE : AMZ_QR_PART;
}

int amz_qr_result(const amz_qr_t *q, char *out, size_t cap, size_t *out_len) {
    if (!q || !out || !out_len || cap == 0) return -1;
    *out_len = 0;
    out[0] = '\0';
    if (q->count == 0 || q->have != q->count || q->total == 0) return -1;

    unsigned char *all = (unsigned char *)malloc(q->total);
    if (!all) return -1;
    size_t at = 0;
    for (size_t i = 0; i < q->count; ++i) {
        memcpy(all + at, q->part[i], q->part_len[i]);
        at += q->part_len[i];
    }

    int rc = -1;
    if (all[0] == '{') {
        if (q->total < cap) {
            memcpy(out, all, q->total);
            out[q->total] = '\0';
            *out_len = q->total;
            rc = 0;
        }
    } else {
        static const char scheme[] = "vpn://";
        size_t sl = sizeof scheme - 1;
        size_t enc = 0;
        if (cap > sl + b64_encoded_maxlen(q->total)) {
            memcpy(out, scheme, sl);
            if (b64_encode(all, q->total, out + sl, cap - sl, &enc) == 0) {
                out[sl + enc] = '\0';
                *out_len = sl + enc;
                rc = 0;
            }
        }
    }
    free(all);
    return rc;
}
