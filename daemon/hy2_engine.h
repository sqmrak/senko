#ifndef SENKO_HY2_ENGINE_H
#define SENKO_HY2_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "core/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the hysteria2 client inside senkod: one quic connection on its own thread,
   one bidirectional quic stream per proxied tcp flow.

   a flow reaches the engine as a socketpair end. hy2_engine_dial() matches
   tun_opener_dial_fn, so the tunnel's existing opener threads, tcp bridge and
   session layer (core/session.c writes the hysteria2 request on that fd) run
   unchanged; the engine only moves bytes between the other end and the stream.

   a connection that dies mid flow aborts its streams, and transport_hy2 turns
   that into a read error, so the app gets a reset and never mistakes a
   truncated transfer for a finished one. the next dial reconnects */

typedef struct {
    const uint8_t (*addresses)[16]; /* the server's pinned addresses */
    const uint8_t *address_lens;    /* 4 or 16 for each */
    size_t address_count;
    uint16_t port;
    const char *sni;                /* tls server name, empty uses peer_host */
    const char *peer_host;          /* the profile's host, last name fallback */
    const char *auth;               /* hysteria2 password */
    const char *pin_sha256;         /* leaf certificate sha-256 in hex, or empty */
    const char *obfs_password;      /* salamander key, or empty for none */
    const char *ca_file;            /* trust roots for the unpinned case */
    int connect_timeout_ms;         /* one dial waits at most this long */
} hy2_engine_config_t;

typedef struct hy2_engine hy2_engine_t;

/* starts the thread and the first connection attempt; NULL with the reason in
   error when the configuration or tls context is unusable */
hy2_engine_t *hy2_engine_start(const hy2_engine_config_t *config,
                               char *error, size_t error_cap);

/* matches tun_opener_dial_fn: waits for an authenticated connection and
   returns a nonblocking descriptor for one new stream, or -1 with a sentence
   in error */
int hy2_engine_dial(void *engine, char *error, size_t error_cap);

/* ends pending and future dials. the opener threads that call hy2_engine_dial
   must be joined before hy2_engine_stop frees the engine under them */
void hy2_engine_cancel(hy2_engine_t *engine);

/* cancels, closes the connection politely, joins the thread and frees */
void hy2_engine_stop(hy2_engine_t *engine);

/* reads and writes the descriptor hy2_engine_dial returned, and reports a
   stream the engine had to abort as an error instead of a clean eof */
extern const transport_vt_t transport_hy2;

#ifdef __cplusplus
}
#endif

#endif /* SENKO_HY2_ENGINE_H */
