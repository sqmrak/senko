#include "tls13_transcript.h"

#include <string.h>

int tls13_transcript_init(tls13_transcript_t *t) {
    if (!t) return -1;
    t->ctx = EVP_MD_CTX_new();
    if (!t->ctx) return -1;
    if (EVP_DigestInit_ex(t->ctx, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(t->ctx);
        t->ctx = NULL;
        return -1;
    }
    return 0;
}

void tls13_transcript_update(tls13_transcript_t *t,
                             const uint8_t *msg, size_t len) {
    if (!t || !t->ctx || (len && !msg)) return;
    EVP_DigestUpdate(t->ctx, msg, len);
}

int tls13_transcript_current(const tls13_transcript_t *t,
                             uint8_t out[TLS13_TRANSCRIPT_LEN]) {
    if (!t || !t->ctx || !out) return -1;

/* finalize a copy so the live transcript can keep accepting messages */
    EVP_MD_CTX *copy = EVP_MD_CTX_new();
    if (!copy) return -1;
    int rc = -1;
    if (EVP_MD_CTX_copy_ex(copy, t->ctx) == 1) {
        unsigned int n = 0;
        if (EVP_DigestFinal_ex(copy, out, &n) == 1 && n == TLS13_TRANSCRIPT_LEN)
            rc = 0;
    }
    EVP_MD_CTX_free(copy);
    return rc;
}

void tls13_transcript_free(tls13_transcript_t *t) {
    if (!t || !t->ctx) return;
    EVP_MD_CTX_free(t->ctx);
    t->ctx = NULL;
}
