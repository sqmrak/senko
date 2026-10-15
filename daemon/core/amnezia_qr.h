#ifndef AMNEZIA_QR_H
#define AMNEZIA_QR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the amnezia client never puts a profile in one qr code. it shows a series of
   frames, each one base64url(be16 1984, u8 count, u8 index, be32 len, bytes),
   and the concatenated bytes are what a vpn:// link carries after the scheme */
#define AMZ_QR_MAX_PARTS 255
#define AMZ_QR_MAX_FRAME_TEXT 8192
#define AMZ_QR_MAX_BYTES (512 * 1024)

typedef enum {
    AMZ_QR_NOT_FRAME = 0,
    AMZ_QR_PART = 1,
    AMZ_QR_REPEAT = 2,
    AMZ_QR_DONE = 3,
    AMZ_QR_ERR_SPACE = -1
} amz_qr_status_t;

typedef struct {
    uint8_t count;
    uint8_t have;
    size_t total;
    unsigned char *part[AMZ_QR_MAX_PARTS];
    size_t part_len[AMZ_QR_MAX_PARTS];
} amz_qr_t;

void amz_qr_init(amz_qr_t *q);
void amz_qr_free(amz_qr_t *q);

/* a frame that belongs to a different series than the one collected so far
   starts a new series, so a stale scan cannot poison the next profile */
amz_qr_status_t amz_qr_add(amz_qr_t *q, const char *text, size_t len);

/* text the import path already understands: vpn://<base64> for a compressed
   profile, or the plain json when the series carries that */
int amz_qr_result(const amz_qr_t *q, char *out, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
