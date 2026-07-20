#ifndef B64_H
#define B64_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* accept both alphabets and missing padding used by subscription sources */
int b64_decode(const char *in, size_t in_len,
               unsigned char *out, size_t cap, size_t *out_len);

size_t b64_decoded_maxlen(size_t in_len);

/* emit padded standard base64 so persisted links remain interoperable */
int b64_encode(const unsigned char *in, size_t in_len,
               char *out, size_t cap, size_t *out_len);

size_t b64_encoded_maxlen(size_t in_len);

#ifdef __cplusplus
}
#endif

#endif /* b64_h */
