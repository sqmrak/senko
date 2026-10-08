#ifndef SENKO_HY2_PROTO_H
#define SENKO_HY2_PROTO_H

#include <stddef.h>
#include <stdint.h>

#include "vless.h"

#ifdef __cplusplus
extern "C" {
#endif

/* hysteria2 wire pieces that need no quic: the tcp stream request and
   response, and salamander's udp packet mask. the quic and http/3 side lives
   in daemon/hy2_engine.c */

#define HY2_FRAME_TCP_REQUEST 0x401u
#define HY2_AUTH_STATUS 233
/* hysteria2 servers cap the message and padding well below these */
#define HY2_MESSAGE_MAX 2048u
#define HY2_PADDING_MAX 4096u
#define HY2_SALAMANDER_SALT_LEN 8u
#define HY2_SALAMANDER_KEY_MAX 128u

typedef enum {
    HY2_OK        =  0,
    HY2_NEED_MORE = -1, /* the input ended inside a field */
    HY2_ERR_ARG   = -2,
    HY2_ERR_PROTO = -3, /* a length over the cap, or a varint over 62 bits */
    HY2_REJECTED  = -4  /* the server answered with a non-zero status */
} hy2_status_t;

/* qUIC varint (RFC 9000 section 16). put returns the byte count, 0 when cap
   or the value does not fit; get returns the byte count, 0 when more input is
   needed */
size_t hy2_varint_put(uint64_t value, uint8_t *out, size_t cap);
size_t hy2_varint_get(const uint8_t *in, size_t len, uint64_t *value);

/* "host:port", "a.b.c.d:port" or "[v6]:port" the way hysteria2 names a
   destination */
hy2_status_t hy2_address_text(const vless_dest_t *dest, char *out, size_t cap);

/* the request that opens a proxied tcp connection on a fresh quic stream */
hy2_status_t hy2_tcp_request_build(const vless_dest_t *dest, uint8_t *out,
                                   size_t cap, size_t *out_len);

typedef enum {
    HY2_RESP_STATUS = 0,
    HY2_RESP_MESSAGE_LEN,
    HY2_RESP_MESSAGE,
    HY2_RESP_PADDING_LEN,
    HY2_RESP_PADDING,
    HY2_RESP_DONE
} hy2_resp_state_t;

/* incremental, because the server's first stream bytes arrive in any split */
typedef struct {
    hy2_resp_state_t state;
    uint8_t  status;
    uint64_t varint;   /* the partly read length field */
    uint8_t  varint_have;
    uint8_t  varint_size;
    uint64_t remaining;
    char     message[HY2_MESSAGE_MAX + 1];
    size_t   message_len;
} hy2_tcp_response_t;

void hy2_tcp_response_init(hy2_tcp_response_t *r);

/* consumes header bytes only; consumed stops at the end of the padding, so
   whatever follows is application data. HY2_OK once the header is complete,
   HY2_REJECTED when the status was not zero (message holds the reason) */
hy2_status_t hy2_tcp_response_feed(hy2_tcp_response_t *r, const uint8_t *in,
                                   size_t len, size_t *consumed);

/* salamander: salt || (packet xor blake2b256(key || salt)) with the key
   repeated over the packet. wrap writes salt + len bytes, unwrap len - salt */
hy2_status_t hy2_salamander_wrap(const uint8_t *key, size_t key_len,
                                 const uint8_t salt[HY2_SALAMANDER_SALT_LEN],
                                 const uint8_t *in, size_t len,
                                 uint8_t *out, size_t cap, size_t *out_len);
hy2_status_t hy2_salamander_unwrap(const uint8_t *key, size_t key_len,
                                   const uint8_t *in, size_t len,
                                   uint8_t *out, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* SENKO_HY2_PROTO_H */
