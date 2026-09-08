#ifndef DAEMON_CTL_H
#define DAEMON_CTL_H

#include "core/ctl_engine.h"
#include "ctl_server.h"
#include "dialer.h"
#include "loop.h"
#include "c_backend.h"
#include "go_backend.h"
#include "settings.h"

#define DCTL_OK             0
#define DCTL_ERR_TRANSPORT (-1)
#define DCTL_ERR_UUID      (-2)
#define DCTL_ERR_LOOP      (-3)
#define DCTL_ERR_DNS       (-4)
#define DCTL_ERR_ROUTING   (-5)
#define DCTL_ERR_GO        (-6)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    loop_t        *loop; /* use the shared loop */
    dialer_ctx_t   dialer; /* point to the active server */
    char           config_path[1024];

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
} daemon_ctl_t;

void daemon_ctl_init(daemon_ctl_t *d, loop_t *loop, const char *config_path);

void daemon_ctl_set_full_device(daemon_ctl_t *d, int on);

void daemon_ctl_set_settings(daemon_ctl_t *d, const daemon_settings_t *s);

void daemon_ctl_shutdown(daemon_ctl_t *d);

/* tear down routes if the separate go core exits unexpectedly */
int daemon_ctl_maintain(daemon_ctl_t *d);

int daemon_ctl_apply(void *ctx, const ctl_action_t *action);

/* why the last backend start failed; empty when there is nothing to add */
const char *daemon_ctl_last_reason(void *ctx);

void daemon_ctl_persist(void *ctx, const store_t *store);

int daemon_ctl_fetch(void *ctx, const char *url,
                     const char *request_header,
                     unsigned char *buf, size_t cap, size_t *len,
                     ctl_fetch_meta_t *meta);

int daemon_ctl_probe(void *ctx, const char *host, uint16_t port);

int daemon_ctl_verify_tunnel(void *ctx, char *reason, size_t reason_cap);

int daemon_ctl_ping_tunnel(void *ctx);

int daemon_ctl_backup(void *ctx, int restore, store_t *store);
int daemon_ctl_check(void *ctx, const char *mode, const vl_server_t *server,
                     char *reason, size_t reason_cap);

#ifdef __cplusplus
}
#endif

#endif /* daemon_ctl_h */
