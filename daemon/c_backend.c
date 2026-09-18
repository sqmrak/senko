#include "c_backend.h"

#include "core/net_safe.h"

#include <stdio.h>
#include <string.h>

static void set_reason(char *reason, size_t cap, const char *text) {
    if (reason && cap) snprintf(reason, cap, "%s", text);
}

/* pf rewrites the destination, so the listener has to ask pf what it was.
   every ipfw fwd mode leaves the original destination on the accepted socket
   and needs the wildcard listener that getsockname can read it from */
static int enable_listener(loop_t *loop, int port, int sockname_dest) {
    loop_status_t rc = sockname_dest
        ? loop_enable_tproxy_sockname(loop, (uint16_t)port)
        : loop_enable_tproxy(loop, (uint16_t)port);
    return rc == LOOP_OK ? 0 : -1;
}

static int rung_ok(c_backend_t *cb, loop_t *loop, int port, int sockname_dest,
                   c_backend_verify_fn verify, void *verify_ctx) {
    if (enable_listener(loop, port, sockname_dest) != 0) {
        fprintf(stderr, "senkod: c backend: transparent listener on port %d failed\n",
                port);
        return -1;
    }
    cb->redir_port = port;
    if (verify && verify(verify_ctx) != 0) {
        fprintf(stderr, "senkod: c backend: rules were accepted but no traffic "
                        "reached the listener\n");
        loop_disable_tproxy(loop);
        cb->redir_port = 0;
        return -1;
    }
    return 0;
}

int c_backend_start(c_backend_t *cb, loop_t *loop, int socks_port,
                    const char *server_ip, const char *server_ips,
                    const char *dns_upstream, int dns_local_port,
                    dns_block_response_t block_response,
                    ruleset_t *rules,
                    const senko_force_t *force,
                    c_backend_verify_fn verify, void *verify_ctx,
                    char *reason, size_t reason_cap) {
    if (!cb || !loop || !server_ip || !server_ips) return -1;
    memset(cb, 0, sizeof *cb);
    if (reason && reason_cap) reason[0] = '\0';

    int pinned_app_proxy = force && force->backend == SENKO_BACKEND_APP_PROXY;
    int force_pf_mode = force ? force->pf_mode : SENKO_PF_MODE_AUTO;

    /* pf, or numbered ipfw rules; this rung is the only one with a dns
       forwarder, so it is worth trying before the plain fwd ruleset */
    if (!pinned_app_proxy &&
        routing_exec_up(&cb->rules, socks_port, server_ip, server_ips,
                        dns_upstream, dns_local_port, block_response,
                        rules, force_pf_mode) == REXEC_OK) {
        if (rung_ok(cb, loop, cb->rules.redir_port,
                    cb->rules.mode == ROUTING_MODE_IPFW,
                    verify, verify_ctx) == 0) {
            cb->active = 1;
            return 0;
        }
        routing_exec_down(&cb->rules);
    }

    if (!pinned_app_proxy &&
        routing_fwd_up(&cb->fwd, socks_port, server_ip, server_ips) == 0) {
        if (rung_ok(cb, loop, cb->fwd.redir_port, 1, verify, verify_ctx) == 0) {
            cb->active = 1;
            return 0;
        }
        routing_fwd_down(&cb->fwd);
    }

    /* without a firewall tool only hooked processes can be redirected, so this
       rung covers less of the device and stays last */
    if (routing_fwd_app_proxy_up(&cb->fwd, socks_port) == 0) {
        cb->active = 1;
        cb->app_proxy = 1;
        cb->redir_port = cb->fwd.redir_port;
        return 0;
    }

/* ipfw is off on ios 5 because its fwd ruleset panics that kernel, which
   leaves the substrate proxy as the only route and makes the hook, not the
   firewall, the thing the user has to install */
    set_reason(reason, reason_cap,
               pinned_app_proxy
                   ? "the connect hook was pinned but senkotlsfix is not loaded"
               : "no usable firewall backend (need pfctl, ipfw, or senkotlsfix)");
    memset(cb, 0, sizeof *cb);
    return -1;
}

void c_backend_stop(c_backend_t *cb, loop_t *loop) {
    if (!cb) return;
    if (loop) loop_disable_tproxy(loop);
    routing_exec_down(&cb->rules);
    routing_fwd_down(&cb->fwd);
    memset(cb, 0, sizeof *cb);
}

int c_backend_uses_tproxy(const c_backend_t *cb) {
    return cb && cb->active && !cb->app_proxy;
}

void c_backend_bypass_add_ipv4(c_backend_t *cb, const char *ip) {
    char literal[64];
    /* both rule writers paste the address straight into a pf table or an ipfw
       rule, so anything but a plain ipv4 literal has to stop here */
    if (!cb || !cb->active || !net_ipv4_literal(ip, literal, sizeof literal))
        return;
    routing_exec_bypass_add_ipv4(&cb->rules, literal);
    routing_fwd_bypass_add_ipv4(&cb->fwd, literal);
}

void c_backend_clear_stale(void) {
    routing_exec_clear_stale();
    routing_fwd_clear_rules();
}
