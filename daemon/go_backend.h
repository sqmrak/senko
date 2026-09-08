#ifndef GO_BACKEND_H
#define GO_BACKEND_H

#include "awg_route.h"
#include "core/config.h"

#include <sys/types.h>

typedef struct {
    pid_t child;
    int tun_fd;
    int active;
    awg_route_plan_t route;
} go_backend_t;

/* the separate go core owns a utun device, which its runtime can only reach on
   arm64 from ios 12 onward. everything else falls back to the c backend */
int go_backend_supported(void);

int go_backend_start(go_backend_t *backend, const vl_server_t *server,
                     const char *endpoint_ip, char *reason, size_t reason_cap);
void go_backend_stop(go_backend_t *backend);
int go_backend_running(go_backend_t *backend);

#endif
