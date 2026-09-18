#ifndef GO_BACKEND_H
#define GO_BACKEND_H

#include "awg_route.h"
#include "core/config.h"
#include "core/traffic.h"

#include <sys/types.h>

typedef struct {
    pid_t child;
    int tun_fd;
    int active;
    awg_route_plan_t route;
    traffic_counter_t upload;
    traffic_counter_t download;
} go_backend_t;

/* the separate go core owns a utun device, which its runtime can only reach on
   arm64 from ios 12 onward. everything else falls back to the c backend */
int go_backend_supported(void);

int go_backend_start(go_backend_t *backend, const vl_server_t *server,
                     const char *endpoint_ip, const ruleset_t *rules,
                     char *reason, size_t reason_cap);
void go_backend_stop(go_backend_t *backend);
int go_backend_running(go_backend_t *backend);
int go_backend_stats(go_backend_t *backend, uint64_t *up, uint64_t *down);

/* pin one destination to the same physical gateway the tunnel endpoint uses,
   so a manual latency probe reaches the real host instead of looping back
   through the split-default routes this backend installed. remove it again
   once the probe is done: nothing else ever un-pins a stale entry */
int go_backend_bypass_add_ipv4(go_backend_t *backend, const char *ip);
void go_backend_bypass_remove_ipv4(go_backend_t *backend, const char *ip);

#endif
