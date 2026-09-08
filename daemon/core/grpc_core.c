#include "grpc_core.h"

#include <string.h>

static size_t put_varint(uint8_t *out, uint32_t value) {
    size_t n = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7f);
        value >>= 7;
        if (value) byte |= 0x80;
        out[n++] = byte;
    } while (value);
    return n;
}

static int get_varint(const uint8_t *in, size_t len, size_t *used,
                     uint32_t *value) {
    uint32_t result = 0;
    unsigned shift = 0;
    for (size_t i = 0; i < len && i < 5; ++i) {
        uint8_t byte = in[i];
        if (i == 4 && (byte & 0xf0)) return -1;
        result |= (uint32_t)(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            *used = i + 1;
            *value = result;
            return 0;
        }
        shift += 7;
    }
    return -1;
}

int senko_grpc_encode(const uint8_t *data, size_t len,
                      uint8_t *out, size_t cap, size_t *out_len) {
    size_t var_len;
    size_t message_len;
    if ((!data && len) || !out || !out_len || len > 16368) return -1;
    var_len = len < 128 ? 1 : (len < 16384 ? 2 : 3);
    message_len = 1 + var_len + len;
    if (message_len > UINT32_MAX || message_len + 5 > cap) return -1;
    out[0] = 0;
    out[1] = (uint8_t)(message_len >> 24);
    out[2] = (uint8_t)(message_len >> 16);
    out[3] = (uint8_t)(message_len >> 8);
    out[4] = (uint8_t)message_len;
    out[5] = 0x0a;
    put_varint(out + 6, (uint32_t)len);
    memcpy(out + 6 + var_len, data, len);
    *out_len = message_len + 5;
    return 0;
}

int senko_grpc_decode(const uint8_t *frame, size_t len,
                      uint8_t *out, size_t cap, size_t *out_len) {
    size_t used;
    uint32_t message_len, field_len;
    if (!frame || !out || !out_len || len < 7 || frame[0] != 0) return -1;
    message_len = ((uint32_t)frame[1] << 24) | ((uint32_t)frame[2] << 16) |
                  ((uint32_t)frame[3] << 8) | frame[4];
    if ((size_t)message_len + 5 != len || frame[5] != 0x0a) return -1;
    if (get_varint(frame + 6, len - 6, &used, &field_len) != 0 ||
        field_len > len - 6 - used || field_len > cap ||
        6 + used + field_len != len)
        return -1;
    memcpy(out, frame + 6 + used, field_len);
    *out_len = field_len;
    return 0;
}
