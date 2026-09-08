#ifndef SENKO_GRPC_CORE_H
#define SENKO_GRPC_CORE_H

#include <stddef.h>
#include <stdint.h>

/* encode one uncompressed gRPC message carrying VLESS bytes in field 1 */
int senko_grpc_encode(const uint8_t *data, size_t len,
                      uint8_t *out, size_t cap, size_t *out_len);

/* decode one message and return the VLESS payload length */
int senko_grpc_decode(const uint8_t *frame, size_t len,
                      uint8_t *out, size_t cap, size_t *out_len);

#endif
