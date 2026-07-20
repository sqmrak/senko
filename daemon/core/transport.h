#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRANSPORT_WANT_READ   (-1) /* wait for socket input before retrying */
#define TRANSPORT_WANT_WRITE  (-2) /* wait for socket output before retrying */
#define TRANSPORT_EOF         (-3) /* close after the peer ends the stream */
#define TRANSPORT_ERR         (-4) /* close after an unrecoverable failure */

/* borrow config strings so transport opens can outlive the caller stack */
typedef struct {
    const char *sni; /* preserve the tls server name */
    const char *fingerprint;/* preserve the browser fingerprint choice */
    const char *reality_pbk;/* enable reality only when a key is present */
    const char *reality_sid;/* preserve the optional reality short id */
    const char *path; /* preserve the transport request path */
    const char *ws_host; /* preserve the websocket host header */
    const char *xhttp_mode; /* preserve the xhttp stream mode */
} transport_tls_cfg_t;

typedef struct transport_vt {
/* open from a connected fd without blocking the shared loop */
    void *(*open)(int fd, const transport_tls_cfg_t *cfg);

/* move application bytes and expose backpressure through the result */
    int (*read)(void *h, uint8_t *buf, size_t len);
    int (*write)(void *h, const uint8_t *buf, size_t len);

/* splice plaintext only after the transport has entered direct mode */
    int (*raw_write)(void *h, const uint8_t *buf, size_t len);

/* release transport state while the daemon retains fd ownership */
    void (*close)(void *h);

/* report pending ciphertext so the loop continues polling for output */
    int (*want_write)(void *h);
} transport_vt_t;

extern const transport_vt_t transport_tcp; /* plain tcp transport */
extern const transport_vt_t transport_tls; /* tls and reality transport */
extern const transport_vt_t transport_ws_tcp;/* websocket over plain tcp */
extern const transport_vt_t transport_ws_tls;/* websocket over tls */
extern const transport_vt_t transport_ws_reality; /* websocket over reality */
extern const transport_vt_t transport_xhttp_tcp;
extern const transport_vt_t transport_xhttp_tls;
extern const transport_vt_t transport_xhttp_reality;

#ifdef __cplusplus
}
#endif

#endif /* transport_h */
