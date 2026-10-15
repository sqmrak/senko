#ifndef SENKO_CORE_BACKEND_H
#define SENKO_CORE_BACKEND_H

#include "awg_route.h"
#include "core/config.h"
#include "core/traffic.h"
#include "utun_dns.h"

#include <sys/types.h>

typedef struct {
    pid_t child;
    int tun_fd;
    int active;
    awg_route_plan_t route;
    utun_dns_t dns;
    traffic_counter_t upload;
    traffic_counter_t download;
} senko_core_backend_t;

/* the separate senko-core owns a utun device, which its runtime can only reach on
   arm64 from ios 12 onward. everything else falls back to the c backend */
int senko_core_backend_supported(void);

int senko_core_backend_start(senko_core_backend_t *backend, const vl_server_t *server,
                     const char *endpoint_ip, const char *dns_server,
                     const ruleset_t *rules, const char *geo_dir,
                     char *reason, size_t reason_cap);
void senko_core_backend_stop(senko_core_backend_t *backend);
int senko_core_backend_running(senko_core_backend_t *backend);
int senko_core_backend_stats(senko_core_backend_t *backend, uint64_t *up, uint64_t *down);

/* pin one destination to the same physical gateway the tunnel endpoint uses,
   so a manual latency probe reaches the real host instead of looping back
   through the split-default routes this backend installed. remove it again
   once the probe is done: nothing else ever un-pins a stale entry */
int senko_core_backend_bypass_add_ipv4(senko_core_backend_t *backend, const char *ip);
void senko_core_backend_bypass_remove_ipv4(senko_core_backend_t *backend, const char *ip);

#endif
