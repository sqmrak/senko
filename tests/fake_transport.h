#ifndef FAKE_TRANSPORT_H
#define FAKE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "transport.h"

typedef struct {
    uint8_t out[64 * 1024];
    size_t  out_len;
    uint8_t raw_out[64 * 1024];
    size_t  raw_out_len;

    uint8_t in[64 * 1024];
    size_t  in_len;
    size_t  in_off;     /* retain the next unread byte */

    int     eof;            /* end the stream after queued input drains */
    int     stall_writes;   /* inject backpressure for the next writes */
    size_t  write_chunk;    /* cap each write to exercise partial progress */
    size_t  read_chunk;     /* cap each read to exercise partial progress */
} fake_tp_t;

extern const transport_vt_t fake_transport;

void fake_tp_reset(fake_tp_t *f);
void fake_tp_queue_server_bytes(fake_tp_t *f, const uint8_t *b, size_t n);

#endif /* fake_transport_h */
