#ifndef CTL_SERVER_H
#define CTL_SERVER_H

#include <stddef.h>

#include "ctl_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CTL_SERVER_MAX_CLIENTS 4
#define CTL_CLIENT_OUT_MAX (1024 * 1024)

/* apply engine actions to the data path and report only immediate errors */
typedef int (*ctl_apply_fn)(void *ctx, const ctl_action_t *action);

/* persist store mutations after the command has been accepted */
typedef void (*ctl_persist_fn)(void *ctx, const store_t *store);

/* fetch subscription bytes with a bounded timeout so refresh cannot hang control */
typedef int (*ctl_fetch_fn)(void *ctx, const char *url,
                            unsigned char *buf, size_t cap, size_t *len);

/* measure idle-server latency while continuing the data-path loop */
typedef int (*ctl_probe_fn)(void *ctx, const char *host, uint16_t port);

/* verify the active tunnel and return a short failure reason for the ui */
typedef int (*ctl_verify_fn)(void *ctx, char *reason, size_t reason_cap);

/* measure the active tunnel without mutating routing or selection */
typedef int (*ctl_tunnel_probe_fn)(void *ctx);

typedef struct {
    int   fd; /* connected ui client, or -1 */
    int   authed;
    char  inbuf[1024]; /* retain an incomplete control line */
    size_t in_len;
    char  *outbuf;
    size_t out_len;
    size_t out_off;
} ctl_client_t;

typedef struct {
    int           listen_fd;
    char          sock_path[108]; /* match the unix socket path limit */
    char          token_path[108];
    char          token[40];
    int           require_auth;
    ctl_engine_t  engine;
    ctl_apply_fn  apply;
    ctl_persist_fn persist; /* save accepted store mutations */
    ctl_fetch_fn  fetch; /* fetch subscription data */
    ctl_probe_fn  probe; /* probe an idle server */
    ctl_verify_fn verify; /* verify a newly connected tunnel */
    ctl_tunnel_probe_fn tunnel_probe; /* probe the active tunnel */
    void         *apply_ctx;
    ctl_client_t  clients[CTL_SERVER_MAX_CLIENTS];
} ctl_server_t;

typedef enum {
    CTLS_OK       =  0,
    CTLS_ERR_ARG  = -1,
    CTLS_ERR_BIND = -2,
    CTLS_ERR      = -3
} ctls_status_t;

/* bind the control socket and remove only an unresponsive stale path */
ctls_status_t ctl_server_init(ctl_server_t *s, const char *path,
                              ctl_apply_fn apply, void *apply_ctx);

void ctl_server_set_persist(ctl_server_t *s, ctl_persist_fn persist);

void ctl_server_set_fetch(ctl_server_t *s, ctl_fetch_fn fetch);

void ctl_server_set_probe(ctl_server_t *s, ctl_probe_fn probe);

void ctl_server_set_verify(ctl_server_t *s, ctl_verify_fn verify);

void ctl_server_set_tunnel_probe(ctl_server_t *s, ctl_tunnel_probe_fn probe);

/* restore the persisted selection during daemon startup */
int ctl_server_restore_tunnel(ctl_server_t *s);

/* accept clients, dispatch complete lines, and flush replies for one interval */
ctls_status_t ctl_server_step(ctl_server_t *s, int timeout_ms);

/* publish an asynchronous state or ping result to connected ui clients */
void ctl_server_broadcast(ctl_server_t *s, const char *line, size_t len);

size_t ctl_server_client_count(const ctl_server_t *s);

void ctl_server_close(ctl_server_t *s);

#ifdef __cplusplus
}
#endif

#endif /* ctl_server_h */
