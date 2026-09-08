/* vision framing is removed from server-controlled bytes on every xtls-rprx-vision
   connection, and its content/padding counters are signed */
#include "vision.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 17 || size > 65536) return 0;

    uint8_t uuid[16];
    memcpy(uuid, data, 16);
    const uint8_t *body = data + 16;
    size_t body_len = size - 16;

    uint8_t *out = (uint8_t *)malloc(body_len + 64);
    if (!out) return 0;

    vision_unpad_t up;
    vision_unpad_init(&up, uuid);

    size_t off = 0;
    while (off < body_len) {
        size_t chunk = (size_t)(body[off] % 97) + 1;
        if (chunk > body_len - off) chunk = body_len - off;
        size_t produced = 0;
        int direct = 0;
        if (vision_unpad(&up, body + off, chunk, out, body_len + 64,
                         &produced, &direct) != 0)
            break;
        if (produced > body_len + 64) abort(); /* contract: never overrun cap */
        off += chunk;
    }

    /* the writer side shares the tls detection state machine */
    vision_wrap_t wrap;
    vision_wrap_init(&wrap, uuid);
    size_t wrote = 0;
    (void)vision_wrap_bootstrap(&wrap, out, body_len + 64, &wrote);
    off = 0;
    while (off < body_len) {
        size_t chunk = (size_t)(body[off] % 61) + 1;
        if (chunk > body_len - off) chunk = body_len - off;
        size_t produced = 0;
        if (vision_wrap(&wrap, body + off, chunk, out, body_len + 64, &produced) != 0)
            break;
        off += chunk;
    }

    vision_traffic_t traffic;
    vision_traffic_init(&traffic);
    vision_filter_tls(&traffic, body, body_len);

    free(out);
    return 0;
}
