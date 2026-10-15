#ifndef VMESS_CLIENT_H
#define VMESS_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "vless.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the body ciphers current xray still speaks; "none" and "zero" are gone from
   its encoder and decoder, so a link that names them runs as auto there too */
typedef enum {
    VMESS_CIPHER_AES_128_GCM = 3,
    VMESS_CIPHER_CHACHA20_POLY1305 = 4
} vmess_cipher_t;

typedef enum {
    VMESS_OK        =  0,
    VMESS_ERR_ARG   = -1,
    VMESS_ERR_CRYPTO = -2,
    VMESS_ERR_BAD_TAG = -3,
    VMESS_ERR_PROTO = -4, /* response header or chunk the server cannot have sent */
    VMESS_NEED_MORE = -5,
    VMESS_EOF       = -6  /* the server's end of stream chunk arrived */
} vmess_status_t;

/* xray seals at most an 8192 byte buffer per chunk: 2 size bytes, 16 tag bytes
   and up to 63 padding bytes leave this much payload */
#define VMESS_CHUNK_PAYLOAD_MAX (8192 - 2 - 16 - 64)
/* a chunk the server sends is never larger than its 8192 byte buffer; twice
   that is refused as corrupt instead of staged */
#define VMESS_CHUNK_WIRE_MAX 16384
/* one largest chunk plus one session read (16 kib) */
#define VMESS_STAGE_CAP (VMESS_CHUNK_WIRE_MAX + 2 + 16 * 1024)
/* sealed header: auth id, sealed length, nonce, then the sealed request of at
   most 41 fixed bytes, a 255 byte domain and 15 padding bytes */
#define VMESS_REQUEST_MAX (16 + 18 + 8 + 41 + 256 + 15 + 4 + 16)

/* SHAKE128 read two bytes at a time for the whole connection */
typedef struct {
    uint64_t s[25];
    uint8_t  block[168];
    size_t   pos;
} vmess_shake_t;

typedef int (*vmess_random_fn)(uint8_t *out, size_t len);

typedef struct {
    vmess_cipher_t cipher;
    uint8_t        cmd_key[16];
    uint8_t        req_key[16];
    uint8_t        req_iv[16];
    uint8_t        resp_key[16];
    uint8_t        resp_iv[16];
    uint8_t        resp_v;
    uint8_t        req_body_key[32];  /* chacha stretches the key to 32 bytes */
    uint8_t        resp_body_key[32];
    uint16_t       enc_count;
    uint16_t       dec_count;
    vmess_shake_t  enc_mask;
    vmess_shake_t  dec_mask;
    vmess_random_fn random;
    int64_t        unix_time; /* 0 takes the wall clock when the header is sealed */

    vless_dest_t   dest;
    int            header_sent;
    /* the udp command: every chunk is one datagram. the caller sends one chunk
       per datagram, and the downstream side hands each chunk back behind a two
       byte length, the framing the tunnel's udp codec already reads */
    int            packet;

    size_t         resp_header_len; /* 0 until the sealed length is opened */
    int            resp_header_done;
    int            have_size;       /* chunk size drawn but its bytes not all here */
    uint16_t       chunk_size;
    uint16_t       chunk_padding;
    int            eof;

    uint8_t        dec_stage[VMESS_STAGE_CAP];
    size_t         dec_stage_len;
} vmess_client_t;

/* any name but the two ciphers runs as auto, as xray's conf maps it: aes-128-gcm
   where the cpu has aes instructions, chacha20-poly1305 where it does not */
vmess_cipher_t vmess_cipher_from_name(const char *name);

/* random NULL uses RAND_bytes; the per connection keys are drawn here */
int vmess_client_init(vmess_client_t *c, const uint8_t uuid[VLESS_UUID_LEN],
                      const char *security, const vless_dest_t *dest,
                      vmess_random_fn random);

/* the sealed request header followed by payload as chunks */
int vmess_client_build_request(vmess_client_t *c,
                               const uint8_t *payload, size_t payload_len,
                               uint8_t *out, size_t cap, size_t *out_len);

/* one chunk of at most VMESS_CHUNK_PAYLOAD_MAX bytes */
int vmess_client_encrypt_chunk(vmess_client_t *c,
                               const uint8_t *plain, size_t plain_len,
                               uint8_t *out, size_t cap, size_t *out_len);

/* stages in, then opens the response header and every whole chunk that fits
   plain_cap. VMESS_EOF once the end chunk is read, with the plaintext before it
   still returned in plain_out */
int vmess_client_feed_downstream(vmess_client_t *c,
                                 const uint8_t *in, size_t in_len,
                                 uint8_t *plain_out, size_t plain_cap,
                                 size_t *plain_len);

/* exposed for the host tests: KDF(key, path...) as xray's vmess/aead does it */
int vmess_kdf(const uint8_t *key, size_t key_len,
              const uint8_t *const *path, const size_t *path_len, size_t path_count,
              uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif /* VMESS_CLIENT_H */
