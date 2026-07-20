#ifndef TLS13_TRANSCRIPT_H
#define TLS13_TRANSCRIPT_H

#include <stddef.h>
#include <stdint.h>

#include <openssl/evp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS13_TRANSCRIPT_LEN 32 /* keep transcript hashes at sha-256 size */

typedef struct {
    EVP_MD_CTX *ctx; /* null means initialization failed */
} tls13_transcript_t;

int tls13_transcript_init(tls13_transcript_t *t);

/* append raw handshake bytes so finished covers the exact wire transcript */
void tls13_transcript_update(tls13_transcript_t *t,
                             const uint8_t *msg, size_t len);

/* snapshot the digest without advancing the running transcript */
int tls13_transcript_current(const tls13_transcript_t *t,
                             uint8_t out[TLS13_TRANSCRIPT_LEN]);

void tls13_transcript_free(tls13_transcript_t *t);

#ifdef __cplusplus
}
#endif

#endif /* tls13_transcript_h */
