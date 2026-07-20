#ifndef TLS13_RECORD_H
#define TLS13_RECORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS13_REC_KEY_LEN 16
#define TLS13_REC_IV_LEN  12
#define TLS13_REC_TAG_LEN 16
#define TLS13_REC_HDR_LEN 5

/* keep both supported aeads on the same sha-256 schedule */
typedef enum {
    TLS13_AEAD_AES128GCM = 0, /* use the default 16-byte key */
    TLS13_AEAD_CHACHA20  = 1 /* use the 32-byte chacha key */
} tls13_aead_t;

#define TLS13_REC_MAXKEY_LEN 32 /* fit the largest supported aead key */

#define TLS13_CT_HANDSHAKE      22
#define TLS13_CT_APPLICATION    23
#define TLS13_CT_ALERT          21

typedef enum {
    TLS13_REC_OK        =  0,
    TLS13_REC_ERR_ARG   = -1,
    TLS13_REC_ERR_SPACE = -2,
    TLS13_REC_ERR_AUTH  = -3, /* reject malformed data or a bad tag */
    TLS13_REC_ERR_CRYPTO= -4
} tls13_rec_status_t;

/* seal one tls record and return the complete wire bytes */
tls13_rec_status_t tls13_record_seal(const uint8_t key[TLS13_REC_KEY_LEN],
                                     const uint8_t iv[TLS13_REC_IV_LEN],
                                     uint64_t seq,
                                     uint8_t content_type,
                                     const uint8_t *content, size_t content_len,
                                     uint8_t *out, size_t cap, size_t *out_len);

/* authenticate and open one tls record before exposing plaintext */
tls13_rec_status_t tls13_record_open(const uint8_t key[TLS13_REC_KEY_LEN],
                                     const uint8_t iv[TLS13_REC_IV_LEN],
                                     uint64_t seq,
                                     const uint8_t *record, size_t record_len,
                                     uint8_t *out, size_t cap, size_t *out_len,
                                     uint8_t *out_type);

/* select the aead explicitly while keeping the default wrappers stable */
tls13_rec_status_t tls13_record_seal_suite(tls13_aead_t aead,
                                     const uint8_t *key, const uint8_t iv[TLS13_REC_IV_LEN],
                                     uint64_t seq,
                                     uint8_t content_type,
                                     const uint8_t *content, size_t content_len,
                                     uint8_t *out, size_t cap, size_t *out_len);

tls13_rec_status_t tls13_record_open_suite(tls13_aead_t aead,
                                     const uint8_t *key, const uint8_t iv[TLS13_REC_IV_LEN],
                                     uint64_t seq,
                                     const uint8_t *record, size_t record_len,
                                     uint8_t *out, size_t cap, size_t *out_len,
                                     uint8_t *out_type);

#ifdef __cplusplus
}
#endif

#endif /* tls13_record_h */
