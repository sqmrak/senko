#ifndef DAEMON_CTL_H
#define DAEMON_CTL_H

#include "core/ctl_engine.h"
#include "ctl_server.h"
#include "dialer.h"
#include "loop.h"
#include "routing_exec.h"
#include "settings.h"

#define DCTL_OK             0
#define DCTL_ERR_TRANSPORT (-1)
#define DCTL_ERR_UUID      (-2)
#define DCTL_ERR_LOOP      (-3)
#define DCTL_ERR_DNS       (-4)
#define DCTL_ERR_ROUTING   (-5)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    loop_t        *loop; /* borrow the shared data-path loop */
    dialer_ctx_t   dialer; /* retarget the active server on start */
    char           config_path[1024];

/* keep full-device routing opt-in so socks mode remains isolated */
    int            full_device;
    daemon_settings_t settings;
    routing_exec_t routing; /* retain live state for deterministic teardown */
} daemon_ctl_t;

/* attach orchestration to an initialized loop and optional persistence */
void daemon_ctl_init(daemon_ctl_t *d, loop_t *loop, const char *config_path);

void daemon_ctl_set_full_device(daemon_ctl_t *d, int on);

void daemon_ctl_set_settings(daemon_ctl_t *d, const daemon_settings_t *s);

/* remove every redirect owned by the daemon on every shutdown path */
void daemon_ctl_shutdown(daemon_ctl_t *d);

/* translate control actions into loop, routing, and transport operations */
int daemon_ctl_apply(void *ctx, const ctl_action_t *action);

/* persist accepted server and subscription mutations for restart recovery */
void daemon_ctl_persist(void *ctx, const store_t *store);

/* fetch subscription data through the configured network path with a deadline */
int daemon_ctl_fetch(void *ctx, const char *url,
                     unsigned char *buf, size_t cap, size_t *len);

/* measure tcp connect latency without including dns resolution */
int daemon_ctl_probe(void *ctx, const char *host, uint16_t port);

/* verify the new tunnel through socks and return a short ui failure reason */
int daemon_ctl_verify_tunnel(void *ctx, char *reason, size_t reason_cap);

/* measure the selected active tunnel through the local socks listener */
int daemon_ctl_ping_tunnel(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* daemon_ctl_h */
