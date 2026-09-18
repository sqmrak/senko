#ifndef DAEMON_CTL_H
#define DAEMON_CTL_H

#include "core/ctl_engine.h"
#include "ctl_server.h"
#include "dialer.h"
#include "loop.h"
#include "c_backend.h"
#include "go_backend.h"
#include "settings.h"

#include <pthread.h>

#define DCTL_OK             0
#define DCTL_ERR_TRANSPORT (-1)
#define DCTL_ERR_UUID      (-2)
#define DCTL_ERR_LOOP      (-3)
#define DCTL_ERR_DNS       (-4)
#define DCTL_ERR_ROUTING   (-5)
#define DCTL_ERR_GO        (-6)
#define DCTL_ERR_SETTING_KEY   (-7)
#define DCTL_ERR_SETTING_VALUE (-8)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    loop_t        *loop; /* use the shared loop */
    dialer_ctx_t   dialer; /* point to the active server */
    char           config_path[1024];
    ruleset_t      *rules;

/* keep full-device routing opt-in */
    int            full_device;
    daemon_settings_t settings;
    /* resolved before the rules go up, because the routing probe cannot rely
       on dns once the redirect it is testing is already installed */
    char           probe_ip[16];
    c_backend_t    c_backend; /* in-process core, used wherever utun is not */
    go_backend_t   go; /* separate utun core, preferred where it runs */
    /* what the backend actually complained about. the control protocol only
       carries a layer name, and on ios 5 the missing piece is never obvious */
    char           last_reason[192];
/* wall clock second the daemon came up, so the diagnostics can report an age
   without asking the kernel for process start times it cannot read on ios 5 */
    long           started_at;
/* the check in flight, so the probe helpers can name the stage they reached
   without every one of them growing a parameter */
    ctl_check_trace_t *trace;
    pthread_mutex_t probe_route_lock;
} daemon_ctl_t;

void daemon_ctl_init(daemon_ctl_t *d, loop_t *loop, const char *config_path);

void daemon_ctl_set_full_device(daemon_ctl_t *d, int on);

void daemon_ctl_set_settings(daemon_ctl_t *d, const daemon_settings_t *s);
void daemon_ctl_set_rules(daemon_ctl_t *d, ruleset_t *rules);

void daemon_ctl_shutdown(daemon_ctl_t *d);

/* tear down routes if the separate go core exits unexpectedly */
int daemon_ctl_maintain(daemon_ctl_t *d);
int daemon_ctl_stats(void *ctx, uint64_t *up, uint64_t *down);

int daemon_ctl_apply(void *ctx, const ctl_action_t *action);

/* why the last backend start failed; empty when there is nothing to add */
const char *daemon_ctl_last_reason(void *ctx);

void daemon_ctl_persist(void *ctx, const store_t *store);

/* append "DIAG <key> <value>" lines describing which rung of every fallback
   ladder the daemon actually took. the control server adds what only it knows
   and streams the result */
int daemon_ctl_diag(void *ctx, char *buf, size_t cap, size_t *len);

int daemon_ctl_fetch(void *ctx, const char *url,
                     const char *request_header,
                     unsigned char *buf, size_t cap, size_t *len,
                     ctl_fetch_meta_t *meta);

int daemon_ctl_probe(void *ctx, const char *host, uint16_t port);
int daemon_ctl_probe_server(void *ctx, const vl_server_t *server);

int daemon_ctl_verify_tunnel(void *ctx, char *reason, size_t reason_cap);

int daemon_ctl_ping_tunnel(void *ctx);

int daemon_ctl_backup(void *ctx, int restore, store_t *store);
int daemon_ctl_check(void *ctx, const char *mode, const vl_server_t *server,
                     ctl_check_trace_t *trace, char *reason, size_t reason_cap);

/* the pf config pfctl loaded, or the ipfw rules the backend spawned */
int daemon_ctl_fwconf(void *ctx, char *buf, size_t cap, size_t *len);

/* drop the dns answer cache, the dynamic bypass table, or the settings */
int daemon_ctl_flush(void *ctx, const char *what, char *reason, size_t reason_cap);

int daemon_ctl_native_config(void *ctx, const vl_server_t *server, char *buf,
                             size_t cap, size_t *len);

#ifdef __cplusplus
}
#endif

#endif /* daemon_ctl_h */
