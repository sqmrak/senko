#ifndef C_BACKEND_H
#define C_BACKEND_H

#include <stddef.h>

#include "loop.h"
#include "routing_exec.h"
#include "routing_fwd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the in-process C core: senkod terminates the redirected connections itself
   and carries them over vless. it needs no utun, so it is the only backend
   that runs on every supported system. the go backend replaces it wherever a
   utun tunnel is available */
typedef struct {
    int            active;
    int            app_proxy; /* traffic arrives through the connect hook, not a listener */
    int            redir_port;
    routing_exec_t rules; /* pf rdr, or numbered ipfw rules plus the dns forwarder */
    routing_fwd_t  fwd;   /* plain ipfw fwd, or the published socks port */
} c_backend_t;

/* prove that an installed ruleset actually reaches the transparent listener.
   returns 0 when traffic arrived, so a ruleset that pfctl or ipfw accepted but
   the kernel ignores does not strand the user on a silent rung */
typedef int (*c_backend_verify_fn)(void *ctx);

int  c_backend_start(c_backend_t *cb, loop_t *loop, int socks_port,
                     const char *server_ip, const char *server_ips,
                     const char *dns_upstream, int dns_local_port,
                     c_backend_verify_fn verify, void *verify_ctx,
                     char *reason, size_t reason_cap);

void c_backend_stop(c_backend_t *cb, loop_t *loop);

/* the transparent listener carries traffic in every mode but the connect hook */
int  c_backend_uses_tproxy(const c_backend_t *cb);

/* keep senkod's own probes off the redirect so they measure the real path */
void c_backend_bypass_add_ipv4(c_backend_t *cb, const char *ip);

/* drop rules a crash left behind before installing a new catch-all */
void c_backend_clear_stale(void);

#ifdef __cplusplus
}
#endif

#endif /* C_BACKEND_H */
