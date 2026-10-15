#include "amnezia_bundle.h"
#include "amnezia_qr.h"
#include "b64.h"

#include <stdio.h>
#include <string.h>

#include "amnezia_fixture.inc"

static int failed;

static void expect(int condition, const char *what) {
    if (condition) return;
    fprintf(stderr, "failed: %s\n", what);
    failed = 1;
}

/* the frame layout the amnezia client writes, built byte by byte */
static size_t make_frame(char *out, size_t cap, unsigned magic, unsigned count,
                         unsigned index, const unsigned char *body, size_t n) {
    unsigned char raw[4096];
    raw[0] = (unsigned char)(magic >> 8);
    raw[1] = (unsigned char)magic;
    raw[2] = (unsigned char)count;
    raw[3] = (unsigned char)index;
    raw[4] = (unsigned char)(n >> 24);
    raw[5] = (unsigned char)(n >> 16);
    raw[6] = (unsigned char)(n >> 8);
    raw[7] = (unsigned char)n;
    memcpy(raw + 8, body, n);
    size_t len = 0;
    if (b64_encode(raw, 8 + n, out, cap, &len) != 0) return 0;
    return len;
}

int main(void) {
    unsigned char packed[4096];
    size_t packed_len = 0;
    const char *body = k_vpn_link + strlen("vpn://");
    expect(b64_decode(body, strlen(body), packed, sizeof packed, &packed_len) == 0,
           "fixture decodes");

    size_t cut1 = packed_len / 3;
    size_t cut2 = 2 * packed_len / 3;
    char f0[4096], f1[4096], f2[4096];
    size_t l0 = make_frame(f0, sizeof f0, 1984, 3, 0, packed, cut1);
    size_t l1 = make_frame(f1, sizeof f1, 1984, 3, 1, packed + cut1, cut2 - cut1);
    size_t l2 = make_frame(f2, sizeof f2, 1984, 3, 2, packed + cut2, packed_len - cut2);
    expect(l0 && l1 && l2, "frames built");

    amz_qr_t q;
    amz_qr_init(&q);
    expect(amz_qr_add(&q, f2, l2) == AMZ_QR_PART, "last frame first");
    expect(amz_qr_add(&q, f2, l2) == AMZ_QR_REPEAT, "same frame again");
    expect(amz_qr_add(&q, f0, l0) == AMZ_QR_PART, "first frame");
    char padded[4200];
    snprintf(padded, sizeof padded, "%s\n", f1);
    expect(amz_qr_add(&q, padded, strlen(padded)) == AMZ_QR_DONE,
           "frame with newline completes");

    char link[8192];
    size_t link_len = 0;
    expect(amz_qr_result(&q, link, sizeof link, &link_len) == 0, "result written");
    expect(link_len == strlen(link) && strncmp(link, "vpn://", 6) == 0, "result is a vpn link");

    char conf[8192];
    char reason[160];
    size_t conf_len = 0;
    amz_status_t r = amz_bundle_extract_conf(link, link_len, conf, sizeof conf,
                                             &conf_len, reason, sizeof reason);
    expect(r == AMZ_OK && strstr(conf, "[Interface]") != NULL, "assembled series imports");
    expect(amz_qr_result(&q, link, 10, &link_len) != 0 && link_len == 0, "small result buffer");

    /* a frame from another series drops what was collected */
    char other[4096];
    size_t ol = make_frame(other, sizeof other, 1984, 2, 0, packed, 10);
    expect(amz_qr_add(&q, other, ol) == AMZ_QR_PART, "other series restarts");
    expect(amz_qr_result(&q, link, sizeof link, &link_len) != 0, "partial series has no result");
    amz_qr_free(&q);

    /* a single frame holding plain json comes out as json */
    amz_qr_init(&q);
    const char *json = "{\"containers\":[]}";
    size_t jl = make_frame(other, sizeof other, 1984, 1, 0,
                           (const unsigned char *)json, strlen(json));
    expect(amz_qr_add(&q, other, jl) == AMZ_QR_DONE, "single json frame");
    expect(amz_qr_result(&q, link, sizeof link, &link_len) == 0 && strcmp(link, json) == 0,
           "json passes through");
    amz_qr_free(&q);

    amz_qr_init(&q);
    expect(amz_qr_add(&q, k_vpn_link, strlen(k_vpn_link)) == AMZ_QR_NOT_FRAME,
           "vpn link is no frame");
    expect(amz_qr_add(&q, "vless://x@1.2.3.4:443#a", 23) == AMZ_QR_NOT_FRAME,
           "vless is no frame");
    expect(amz_qr_add(&q, "", 0) == AMZ_QR_NOT_FRAME, "empty is no frame");
    ol = make_frame(other, sizeof other, 1985, 1, 0, packed, 10);
    expect(amz_qr_add(&q, other, ol) == AMZ_QR_NOT_FRAME, "wrong magic");
    ol = make_frame(other, sizeof other, 1984, 2, 2, packed, 10);
    expect(amz_qr_add(&q, other, ol) == AMZ_QR_NOT_FRAME, "index past count");
    ol = make_frame(other, sizeof other, 1984, 0, 0, packed, 10);
    expect(amz_qr_add(&q, other, ol) == AMZ_QR_NOT_FRAME, "zero count");
    {
        unsigned char lie[12] = {0x07, 0xC0, 1, 0, 0, 0, 0, 99, 1, 2, 3, 4};
        char text[64];
        size_t tl = 0;
        expect(b64_encode(lie, sizeof lie, text, sizeof text, &tl) == 0, "lie encoded");
        expect(amz_qr_add(&q, text, tl) == AMZ_QR_NOT_FRAME, "length beyond the frame");
    }
    amz_qr_free(&q);

    if (failed) return 1;
    puts("all amnezia qr checks passed");
    return 0;
}
