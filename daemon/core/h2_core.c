#include "h2_core.h"

int senko_h2_parse_frame(const uint8_t *data, size_t len,
                         senko_h2_frame_t *frame, size_t *consumed) {
    uint32_t n;
    if (!data || !frame || !consumed) return -1;
    *consumed = 0;
    if (len < 9) return 0;
    n = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    if (n > 16384 && data[3] != 0x04) return -1;
    if ((size_t)n > len - 9) return 0;
    frame->length = n;
    frame->type = data[3];
    frame->flags = data[4];
    frame->stream_id = (((uint32_t)data[5] << 24) | ((uint32_t)data[6] << 16) |
                        ((uint32_t)data[7] << 8) | data[8]) & 0x7fffffffu;
    frame->payload = data + 9;
    *consumed = 9 + (size_t)n;
    return 1;
}
