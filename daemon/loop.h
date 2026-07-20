#ifndef LOOP_H
#define LOOP_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "session.h"
#include "transport.h"
#include "vless.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOOP_MAX_CONNS 96
/* allow safari bursts so connection drops do not force page reloads */
#define LOOP_MAX_OPENING 48
#define LOOP_PREBUF_CAP (16 * 1024)

typedef int (*loop_dialer_fn)(void *ctx);

typedef struct {
    struct loop *owner;
    int        local_fd; /* preserve the client socket */
    int        remote_fd; /* preserve the remote socket */
    session_t  sess;
    void      *th; /* retain transport state for remote_fd */
    int        used;
    int        opening; /* prevent loop work during worker open */
    int        open_done;
    int        open_cancelled;
    pthread_t  open_thread;

/* snapshot configuration so server switches cannot race a handshake */
    const transport_vt_t *open_vt;
    vl_proto_t open_proto;
    uint8_t    open_uuid[VLESS_UUID_LEN];
    char       open_flow[32];
    char       open_user[64];
    char       open_pass[64];
    char       open_sni[256];
    char       open_fingerprint[32];
    char       open_reality_pbk[128];
    char       open_reality_sid[32];
    char       open_path[256];
    char       open_ws_host[256];
    char       open_xhttp_mode[16];
    transport_tls_cfg_t open_tls_cfg;
    void      *open_th;

/* preserve the destination captured before the transparent handshake */
    int        transparent;
    vless_dest_t tproxy_dest;

/* retain early client bytes until the remote transport is ready */
    int        socks_greet_done;
    uint8_t    prebuf[LOOP_PREBUF_CAP];
    size_t     prebuf_len;

/* retain local output so large package transfers survive backpressure */
    uint8_t    pend[64 * 1024];
    size_t     pend_len;
    size_t     pend_off;
} loop_conn_t;

typedef struct loop {
    int                   listen_fd; /* local socks listener */
    int                   tproxy_fd; /* transparent listener, -1 means off */
    uint16_t              tproxy_port; /* redirect port used by natlook */
    const transport_vt_t *vt; /* active remote transport */
    loop_dialer_fn        dial;
    void                 *dial_ctx;

    uint8_t  uuid[VLESS_UUID_LEN];
    char     flow[32]; /* own the active flow string */
    vl_proto_t proto;
    char     user[64];
    char     pass[64];

/* own tls strings so worker opens never observe dead pointers */
    char     sni[256];
    char     fingerprint[32];
    char     reality_pbk[128];
    char     reality_sid[32];
    char     path[256];
    char     ws_host[256];
    char     xhttp_mode[16];
    transport_tls_cfg_t tls_cfg;

/* keep the listener bound while rejecting clients without a server */
    int      active;

    loop_conn_t conns[LOOP_MAX_CONNS];
    size_t      nconns;
    size_t      nopening;

    int         wake_rd;
    int         wake_wr;
    pthread_mutex_t open_lock;
    int         open_lock_ready;
} loop_t;

typedef enum {
    LOOP_OK       =  0,
    LOOP_ERR_ARG  = -1,
    LOOP_ERR_BIND = -2, /* reject a listener that cannot bind */
    LOOP_ERR      = -3
} loop_status_t;

/* bind the local listener and retain the remote path configuration */
loop_status_t loop_init(loop_t *lp, uint16_t listen_port, int bind_public,
                        const transport_vt_t *vt,
                        loop_dialer_fn dial, void *dial_ctx,
                        vl_proto_t proto,
                        const uint8_t uuid[VLESS_UUID_LEN], const char *flow,
                        const char *user, const char *pass);

uint16_t loop_listen_port(const loop_t *lp);

/* copy transport parameters so worker opens can use stable pointers */
void loop_set_tls(loop_t *lp, const char *sni, const char *fingerprint,
                  const char *reality_pbk, const char *reality_sid,
                  const char *path, const char *ws_host,
                  const char *xhttp_mode);

/* replace the active path and drop connections tied to the old server */
loop_status_t loop_set_server(loop_t *lp, const transport_vt_t *vt,
                              loop_dialer_fn dial, void *dial_ctx,
                              vl_proto_t proto,
                              const uint8_t uuid[VLESS_UUID_LEN], const char *flow,
                              const char *user, const char *pass,
                              const char *sni, const char *fingerprint,
                              const char *reality_pbk, const char *reality_sid,
                              const char *path, const char *ws_host,
                              const char *xhttp_mode);

/* stop traffic while keeping the listener ready for the next selection */
void loop_stop(loop_t *lp);

/* enable the listener used by full-device transparent routing */
loop_status_t loop_enable_tproxy(loop_t *lp, uint16_t port);
void loop_disable_tproxy(loop_t *lp);

/* advance sockets, workers, and completed connections for one poll interval */
loop_status_t loop_step(loop_t *lp, int timeout_ms);

size_t loop_conn_count(const loop_t *lp);

void loop_close(loop_t *lp);

#ifdef __cplusplus
}
#endif

#endif /* loop_h */
