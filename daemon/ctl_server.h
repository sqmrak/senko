#ifndef CTL_SERVER_H
#define CTL_SERVER_H

#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "core/geo_update.h"
#include "ctl_engine.h"
#include "helper_jobs.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the app opens one connection per request: a status poll (STATUS and AWG
   STATUS), three list pings and a subscription refresh that holds its slot for
   the whole download already overran four, and the fifth was hung up on */
#define CTL_SERVER_MAX_CLIENTS 16
/* the listener, the result pipe, every client and every helper job */
#define CTL_SERVER_POLL_MAX (2 + CTL_SERVER_MAX_CLIENTS + HELPER_JOB_MAX)
#define CTL_CLIENT_OUT_MAX (1024 * 1024)

typedef int (*ctl_apply_fn)(void *ctx, const ctl_action_t *action);

typedef void (*ctl_persist_fn)(void *ctx, const store_t *store);

typedef struct {
    uint64_t expire;
    uint64_t upload;
    uint64_t download;
    uint64_t total;
    char title[256];
    char description[256];
    char support_url[512];
/* the panel served a device gated placeholder instead of the node list */
    int  gated;
    char gate_reason[256];
/* why the fetch failed, for the user; empty on success */
    char error[160];
/* the body ran past the buffer and only its first part was kept */
    int  body_cut;
/* the routing response header of a happ panel, empty when there was none */
    char routing[20 * 1024];
    int  routing_too_long;
} ctl_fetch_meta_t;

typedef int (*ctl_fetch_fn)(void *ctx, const char *url,
                            const char *request_header,
                            unsigned char *buf, size_t cap, size_t *len,
                            ctl_fetch_meta_t *meta);

/* downloads a geo dat into out, as subscription fetches do. 0 on success; on
   failure -1 with a reason for the user in error */
typedef int (*ctl_geo_fetch_fn)(void *ctx, const char *url, FILE *out, size_t max_bytes,
                                size_t *written, char *error, size_t error_cap);

typedef struct {
    pthread_mutex_t lock;
    int  busy;        /* a download thread is running */
    int  result;      /* 0 none yet, 1 the last download finished, 2 it failed */
    char message[200];
} ctl_geo_state_t;

typedef int (*ctl_probe_fn)(void *ctx, const char *host, uint16_t port);
typedef int (*ctl_server_probe_fn)(void *ctx, const vl_server_t *server);

typedef int (*ctl_verify_fn)(void *ctx, char *reason, size_t reason_cap);

/* the backend's own words for the last failure, or NULL */
typedef const char *(*ctl_reason_fn)(void *ctx);

typedef int (*ctl_tunnel_probe_fn)(void *ctx);
typedef int (*ctl_stats_fn)(void *ctx, uint64_t *up, uint64_t *down);

typedef int (*ctl_backup_fn)(void *ctx, int restore, store_t *store);

#define CTL_CHECK_STAGE_MAX 10
#define CTL_CHECK_STAGE_NAME_MAX 48

/* one named step of a check and how long the check had been running when it
   finished. "check failed" names nothing a tester can repeat, and the four
   check modes stop at four different places */
typedef struct {
    char name[CTL_CHECK_STAGE_NAME_MAX];
    int  ms;
    int  ok;
} ctl_check_stage_t;

typedef struct {
    ctl_check_stage_t stages[CTL_CHECK_STAGE_MAX];
    size_t            count;
    long              started_ms;
} ctl_check_trace_t;

/* trace may be NULL; when it is not, it is filled with the stages the check
   walked, in order, whether or not the check succeeded */
typedef int (*ctl_check_fn)(void *ctx, const char *mode,
                            const vl_server_t *server, ctl_check_trace_t *trace,
                            char *reason, size_t reason_cap);

/* the firewall ruleset the kernel is running, written into buf as text.
   returns 0 when there is one to show */
typedef int (*ctl_fwconf_fn)(void *ctx, char *buf, size_t cap, size_t *len);

/* drop one named piece of accumulated state. returns 0 when it was dropped */
typedef int (*ctl_flush_fn)(void *ctx, const char *what,
                            char *reason, size_t reason_cap);

/* render the validated JSON handed to a native Network Extension provider */
typedef int (*ctl_native_config_fn)(void *ctx, const vl_server_t *server,
                                    char *buf, size_t cap, size_t *len);

/* amneziawg requests: flag is the helper flag of the AWG verb. returns an exit
   code with the status line in out, or CTL_AWG_CHILD when the request has to
   run as a senkod child (the handshake probe) */
#define CTL_AWG_CHILD 2
typedef int (*ctl_awg_fn)(void *ctx, const char *flag, const char *path,
                          char *out, size_t cap);
typedef int (*ctl_awg_busy_fn)(void *ctx);

/* append DIAG lines the daemon owns; the server adds its own before streaming */
typedef int (*ctl_diag_fn)(void *ctx, char *buf, size_t cap, size_t *len);

typedef struct {
    int   fd; /* connected ui client */
    int   authed;
    char  inbuf[1024]; /* partial control line */
    size_t in_len;
    char  *outbuf;
    size_t out_len;
    size_t out_off;
    uint64_t generation;
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
    ctl_server_probe_fn server_probe; /* probe one server, including its protocol */
    ctl_verify_fn verify; /* verify a new tunnel */
    ctl_tunnel_probe_fn tunnel_probe; /* probe the active tunnel */
    ctl_backup_fn backup;
    ctl_check_fn check;
    ctl_diag_fn  diag;
    ctl_fwconf_fn fwconf;
    ctl_flush_fn  flush;
    ctl_native_config_fn native_config;
    ctl_awg_fn    awg;
    ctl_awg_busy_fn awg_busy;
    ctl_stats_fn stats;
    ctl_geo_fetch_fn geo_fetch;
    char geo_dir[GEO_UPDATE_DIR_MAX];
    ctl_geo_state_t geo;
    uint64_t stat_at_ms;
    int stat_failed;
    void         *apply_ctx;
/* a read only view of the copy the apply hook writes into, so the schedules
   below and the SETTINGS dump never disagree with the daemon */
    const daemon_settings_t *settings;
/* redial schedule with no client behind it. retry_at_ms is 0 when idle */
    long          retry_at_ms;
    int           retry_attempts;
/* last measured latency per server index, -1 when unknown or unreachable.
   failover orders candidates by it. a refresh moves it with its server, other
   commands that can move server indexes drop it */
    int           ping_ms[STORE_MAX_SERVERS];
/* default egress seen at the last check, to notice a wifi to cellular move */
    char          egress_iface[32];
    char          egress_ip[16];
    long          egress_check_ms;
    long          sub_check_ms;
/* earliest retry per subscription after a scheduled refresh failed, so a dead
   panel is not pulled every minute */
    long          sub_retry_at_ms[STORE_MAX_SUBS];
/* a subscription pull runs on its own thread (the control socket went silent
   for its whole 16 s budget otherwise), and one per subscription at a time */
    unsigned char sub_fetching[STORE_MAX_SUBS];
    size_t        fetch_active;
/* ping and fetch threads hand their results back through this pipe */
    int           ping_pipe[2];
    size_t        ping_active;
    uint64_t      client_generation;
    ctl_client_t  clients[CTL_SERVER_MAX_CLIENTS];
/* amneziawg handshake probes running in senkod children */
    helper_jobs_t helpers;
/* what each slot ctl_server_prepare filled serves: a client index, or -1 the
   result pipe, -2 the listener, -3 a helper job */
    int           poll_slot[CTL_SERVER_POLL_MAX];
    size_t        poll_count;
    int           poll_helpers_busy;
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

/* where the per tag geo files live and how a dat is downloaded. without them the
   server keeps routing profiles but cannot load the geo data they name */
void ctl_server_set_geo(ctl_server_t *s, ctl_geo_fetch_fn fetch, const char *dir);

/* the state of the last geo download, for tests and the GEO verb */
int ctl_server_geo_busy(ctl_server_t *s);

void ctl_server_set_fetch(ctl_server_t *s, ctl_fetch_fn fetch);

void ctl_server_set_probe(ctl_server_t *s, ctl_probe_fn probe);

void ctl_server_set_server_probe(ctl_server_t *s, ctl_server_probe_fn probe);

void ctl_server_set_verify(ctl_server_t *s, ctl_verify_fn verify);

void ctl_server_set_tunnel_probe(ctl_server_t *s, ctl_tunnel_probe_fn probe);

void ctl_server_set_backup(ctl_server_t *s, ctl_backup_fn backup);
void ctl_server_set_check(ctl_server_t *s, ctl_check_fn check);

void ctl_server_set_reason(ctl_server_t *s, ctl_reason_fn reason);
void ctl_server_set_stats(ctl_server_t *s, ctl_stats_fn stats);

void ctl_server_set_settings(ctl_server_t *s, const daemon_settings_t *settings);

void ctl_server_set_diag(ctl_server_t *s, ctl_diag_fn diag);

void ctl_server_set_fwconf(ctl_server_t *s, ctl_fwconf_fn fwconf);
void ctl_server_set_flush(ctl_server_t *s, ctl_flush_fn flush);
void ctl_server_set_native_config(ctl_server_t *s, ctl_native_config_fn render);
void ctl_server_set_awg(ctl_server_t *s, ctl_awg_fn awg, ctl_awg_busy_fn busy);

int ctl_server_restore_tunnel(ctl_server_t *s);

/* the data path went down without a DISCONNECT: schedule a redial when the
   settings allow one, or publish the failure */
void ctl_server_tunnel_lost(ctl_server_t *s);

/* run what has no client behind it: redial backoff, egress changes and
   scheduled subscription refreshes */
void ctl_server_tick(ctl_server_t *s);

/* one poll() can serve the control socket beside other owners: prepare
   fills at most CTL_SERVER_POLL_MAX slots and lowers *timeout_ms (-1 waits
   forever) to the nearest stats, helper or ctl_server_tick deadline, dispatch
   services those slots after the poll. returns the slots used, 0 when cap is
   too small */
size_t ctl_server_prepare(ctl_server_t *s, struct pollfd *pfd, size_t cap,
                          int *timeout_ms);
void ctl_server_dispatch(ctl_server_t *s, const struct pollfd *pfd, size_t count);

ctls_status_t ctl_server_step(ctl_server_t *s, int timeout_ms);

void ctl_server_broadcast(ctl_server_t *s, const char *line, size_t len);

size_t ctl_server_client_count(const ctl_server_t *s);

void ctl_server_close(ctl_server_t *s);

#ifdef __cplusplus
}
#endif

#endif /* ctl_server_h */
