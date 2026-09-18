#ifndef BLAKE2B256_H
#define BLAKE2B256_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* unkeyed BLAKE2b with a 32-byte digest (RFC 7693). this is the one primitive
   hysteria2's salamander obfuscation needs: key = blake2b256(password || salt) */
void blake2b256(const void *data, size_t len, unsigned char out[32]);

#ifdef __cplusplus
}
#endif

#endif /* blake2b256_h */
