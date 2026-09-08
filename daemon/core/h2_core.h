#ifndef SENKO_H2_CORE_H
#define SENKO_H2_CORE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;
    const uint8_t *payload;
} senko_h2_frame_t;

/* return one for a frame, zero for more input, and minus one for invalid input */
int senko_h2_parse_frame(const uint8_t *data, size_t len,
                         senko_h2_frame_t *frame, size_t *consumed);

#endif
