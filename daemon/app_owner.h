#ifndef SENKO_APP_OWNER_H
#define SENKO_APP_OWNER_H

#include <stddef.h>
#include <stdint.h>

#include "core/tun_flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/* names the app behind a captured flow.

   a flow that enters the tunnel carries only the address and port it left from.
   the kernel knows which process holds that socket, and the process knows its
   executable, whose .app directory holds an Info.plist with the bundle
   identifier. proc_info is how the kernel tells: it lists every process, the
   descriptors of each, and the local port of each socket descriptor.

   what it cannot name: a socket whose process is not inside an .app (system
   daemons, nsurlsessiond for background transfers, and on ios 8 and later the
   WebKit network and content processes that carry a browser's page loads) and a
   socket that closed before the table saw it */

/* __proc_info of libsystem_kernel: call number, pid, flavor, argument, buffer,
   size. returns the bytes written, or -1 */
typedef int (*app_owner_info_fn)(int callnum, int pid, int flavor, uint64_t arg,
                                 void *buffer, int size);

typedef struct app_owner app_owner_t;

typedef struct {
    uint64_t scans;      /* complete passes over every process */
    uint64_t sockets;    /* tcp and udp sockets the last pass saw */
    uint64_t lookups;
    uint64_t named;      /* lookups that found a bundle identifier */
    uint64_t unowned;    /* lookups whose port no process held */
    uint64_t no_bundle;  /* lookups whose process is not inside an app */
    uint64_t errors;     /* proc_info calls that failed */
} app_owner_stats_t;

/* info NULL looks __proc_info up in the process. NULL back means the system has
   no such call and app rules cannot be decided there */
app_owner_t *app_owner_open(app_owner_info_fn info);
void app_owner_close(app_owner_t *owner);

/* 1 with the bundle identifier of the flow's app, 0 when it has none or is
   unknown. now_ms is a monotonic clock and bounds how often the process table
   is read again */
int app_owner_lookup(app_owner_t *owner, const tun_flow_key_t *key, uint64_t now_ms,
                     char *bundle, size_t cap);

void app_owner_stats(const app_owner_t *owner, app_owner_stats_t *out);

/* the tun_policy_app_fn that wraps app_owner_lookup; ctx is the app_owner_t */
int app_owner_policy_lookup(void *ctx, const tun_flow_key_t *key, char *bundle, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
