#ifndef SENKO_HPACK_H
#define SENKO_HPACK_H

#include <stddef.h>
#include <stdint.h>

/* extracted response fields; everything else in the block is ignored */
typedef struct {
    int  have_status;
    int  status;                /* :status, only valid when have_status */
    int  have_grpc_status;
    int  grpc_status;           /* grpc-status trailer value */
    char grpc_message[128];     /* grpc-message trailer, truncated to fit */
} senko_hpack_fields_t;

/* decode one header block. returns 0 when the block is well formed even if
   it carries none of the fields above, -1 on malformed input, truncation, or
   bad huffman padding. entries whose name lives in the peer dynamic table
   (index >= 62) are skipped, so a caller that needs a field must treat an
   unset field as unknown, never as absent. */
int senko_hpack_parse(const uint8_t *buf, size_t len, senko_hpack_fields_t *out);

/* test hook: decode one rfc 7541 string literal */
int senko_hpack_string(const uint8_t *buf, size_t len,
                       char *out, size_t cap, size_t *used);

#endif
