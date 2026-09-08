#ifndef CTL_SERVER_H
#define CTL_SERVER_H

#include <stddef.h>
#include <stdint.h>

#include "ctl_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CTL_SERVER_MAX_CLIENTS 4
#define CTL_CLIENT_OUT_MAX (1024 * 1024)

typedef int (*ctl_apply_fn)(void *ctx, const ctl_action_t *action);

typedef void (*ctl_persist_fn)(void *ctx, const store_t *store);

typedef struct {
    uint64_t expire;
    uint64_t upload;
    uint64_t download;
    uint64_t total;
    char description[256];
    char support_url[512];
/* the panel served a device gated placeholder instead of the node list */
    int  gated;
    char gate_reason[256];
} ctl_fetch_meta_t;

typedef int (*ctl_fetch_fn)(void *ctx, const char *url,
                            const char *request_header,
                            unsigned char *buf, size_t cap, size_t *len,
                            ctl_fetch_meta_t *meta);

typedef int (*ctl_probe_fn)(void *ctx, const char *host, uint16_t port);

typedef int (*ctl_verify_fn)(void *ctx, char *reason, size_t reason_cap);

/* the backend's own words for the last failure, or NULL */
typedef const char *(*ctl_reason_fn)(void *ctx);

typedef int (*ctl_tunnel_probe_fn)(void *ctx);

typedef int (*ctl_backup_fn)(void *ctx, int restore, store_t *store);
typedef int (*ctl_check_fn)(void *ctx, const char *mode,
                            const vl_server_t *server, char *reason, size_t reason_cap);

typedef struct {
    int   fd; /* connected ui client */
    int   authed;
    char  inbuf[1024]; /* partial control line */
    size_t in_len;
    char  *outbuf;
    size_t out_len;
    size_t out_off;
} ctl_client_t;

typedef struct {
    int           listen_fd;
    char          sock_path[108]; /* unix socket path */
    char          token_path[108];
    char          token[40];
    ctl_engine_t  engine;
    ctl_apply_fn  apply;
    ctl_reason_fn reason;
    ctl_persist_fn persist; /* save store changes */
    ctl_fetch_fn  fetch; /* fetch subscriptions */
    ctl_probe_fn  probe; /* probe an idle server */
    ctl_verify_fn verify; /* verify a new tunnel */
    ctl_tunnel_probe_fn tunnel_probe; /* probe the active tunnel */
    ctl_backup_fn backup;
    ctl_check_fn check;
    void         *apply_ctx;
    ctl_client_t  clients[CTL_SERVER_MAX_CLIENTS];
} ctl_server_t;

typedef enum {
    CTLS_OK       =  0,
    CTLS_ERR_ARG  = -1,
    CTLS_ERR_BIND = -2,
    CTLS_ERR      = -3,
    CTLS_ERR_AUTH = -4
} ctls_status_t;

ctls_status_t ctl_server_init(ctl_server_t *s, const char *path,
                              ctl_apply_fn apply, void *apply_ctx);

void ctl_server_set_persist(ctl_server_t *s, ctl_persist_fn persist);

void ctl_server_set_fetch(ctl_server_t *s, ctl_fetch_fn fetch);

void ctl_server_set_probe(ctl_server_t *s, ctl_probe_fn probe);

void ctl_server_set_verify(ctl_server_t *s, ctl_verify_fn verify);

void ctl_server_set_tunnel_probe(ctl_server_t *s, ctl_tunnel_probe_fn probe);

void ctl_server_set_backup(ctl_server_t *s, ctl_backup_fn backup);
void ctl_server_set_check(ctl_server_t *s, ctl_check_fn check);

void ctl_server_set_reason(ctl_server_t *s, ctl_reason_fn reason);

int ctl_server_restore_tunnel(ctl_server_t *s);

ctls_status_t ctl_server_step(ctl_server_t *s, int timeout_ms);

void ctl_server_broadcast(ctl_server_t *s, const char *line, size_t len);

size_t ctl_server_client_count(const ctl_server_t *s);

void ctl_server_close(ctl_server_t *s);

#ifdef __cplusplus
}
#endif

#endif /* ctl_server_h */
