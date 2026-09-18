#ifndef ROUTING_EXEC_H
#define ROUTING_EXEC_H

#include <stddef.h>

#include "core/dns_msg.h"
#include "pf_table.h"
#include "routing.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROUTING_MODE_NONE = 0,
    ROUTING_MODE_IPFW,
    ROUTING_MODE_PF
} routing_mode_t;

#include <pthread.h>

typedef struct {
    routing_mode_t mode;
    int            redir_port;
    int            socks_port; /* exclude the local socks listener */
    char           dns_upstream[64];
    dns_block_response_t block_response;
    int            dns_local_port; /* redirect local dns queries here */
    int            dns_fd;
    int            dns_bound;
    char           server_ip[64]; /* retain the primary bypass target */
    char           server_ips[4096]; /* retain all pf bypass targets */
    char           scoped_route_prev[16]; /* restore what the fwd rules needed off */
    pthread_t      dns_thread;
    int            dns_stop;
    int            pf_table_ready; /* keep live bypass updates only when rules provide a table */
/* which syntax variant pfctl took and how many it refused first. the ladder
   used to leave this in one stderr line, and only when every rung failed, so
   "it does not redirect" could not be answered from the device */
    routing_pf_mode_t pf_mode;
    int            pf_mode_valid;
    int            pf_rejected;
    char           pf_last_error[192];
    ruleset_t      *rules;
    uint8_t        rule_logged[RULESET_MAX_RULES / 8];
/* the dns forwarder thread is the only writer of the answer cache and the
   bypass shadow, so a flush asked for over the control socket is a request it
   picks up on its next pass instead of a second writer */
    int            flush_dns_requested;
    int            flush_bypass_requested;
} routing_exec_t;

typedef enum {
    REXEC_OK            =  0,
    REXEC_ERR_ARG       = -1,
    REXEC_ERR_NO_BACKEND  = -3, /* reject when neither backend applies */
    REXEC_ERR_PORT      = -4, /* reject when no redirect port is free */
    REXEC_ERR_SPAWN     = -5
} rexec_status_t;

/* locate the firewall tools once so every ipfw user searches the same paths */
const char *routing_find_ipfw(void);
const char *routing_find_pfctl(void);

/* run a helper to completion and return its exit status, -1 when it never ran */
int routing_spawn(const char *bin, char *const argv[]);

/* scoped routing makes the kernel drop packets that ipfw fwd sends to the
   loopback listener, so every fwd based mode has to turn it off and restore it */
int  routing_scopedroute_disable(char *prev, size_t cap);
void routing_scopedroute_restore(const char *prev);

/* reserve a free tcp port without keeping the probe socket */
int routing_pick_free_port(int start, int end);

/* reserve a free udp port for the local dns forwarder */
int routing_pick_free_udp_port(int start, int end);

/* enable the first compatible full-device backend without resolving dns
   force_pf_mode pins one pf syntax variant and fails instead of walking the
   ladder; SENKO_PF_MODE_AUTO keeps the full ladder */
rexec_status_t routing_exec_up(routing_exec_t *st, int socks_port,
                               const char *server_ip, const char *server_ips,
                               const char *dns_upstream, int dns_local_port,
                               dns_block_response_t block_response,
                               ruleset_t *rules, int force_pf_mode);

/* the ruleset the kernel is actually running: the pf config as it was handed
   to pfctl, or the ipfw rules as they were spawned. returns 0 when one was
   written, so the caller can say "no backend" rather than print an empty box */
int routing_exec_render(const routing_exec_t *st, char *buf, size_t cap,
                        size_t *len);

/* the shared dns answer cache, counted since the forwarder started */
void routing_exec_dns_stats(uint64_t *hits, uint64_t *misses,
                            uint64_t *stale_hits, size_t *entries);

/* the <senko_bypass> shadow: what it holds and what it has lost to its own
   capacity */
void routing_exec_bypass_stats(pf_table_counts_t *counts,
                               uint64_t *evicted_addresses,
                               uint64_t *evicted_refs);

/* drop every cached answer; the next query goes upstream */
void routing_exec_flush_dns(routing_exec_t *st);

/* drop every dynamic bypass, in the shadow and in the live pf table. the
   static server addresses come back with the next rule reload, the dynamic
   ones with the next dns answer that matches a direct rule */
void routing_exec_flush_bypass(routing_exec_t *st);

void routing_exec_down(routing_exec_t *st);

/* clear crash leftovers before installing a new catch-all rule */
void routing_exec_clear_stale(void);

/* name and address of the interface the device would send a default route out
   of, so a wifi to cellular handover is visible without SystemConfiguration,
   which the armv7 slice does not link. returns 0 when one was found */
int routing_exec_egress_snapshot(char *name, size_t name_cap,
                                 char *ip, size_t ip_cap);

/* add a live bypass so probes do not loop through the redirect */
void routing_exec_bypass_add_ipv4(routing_exec_t *st, const char *ip);

#ifdef __cplusplus
}
#endif

#endif /* routing_exec_h */
