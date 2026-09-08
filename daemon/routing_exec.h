#ifndef ROUTING_EXEC_H
#define ROUTING_EXEC_H

#include <stddef.h>

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
    int            dns_local_port; /* redirect local dns queries here */
    int            dns_fd;
    int            dns_bound;
    char           server_ip[64]; /* retain the primary bypass target */
    char           server_ips[4096]; /* retain all pf bypass targets */
    char           scoped_route_prev[16]; /* restore what the fwd rules needed off */
    pthread_t      dns_thread;
    int            dns_stop;
    int            pf_table_ready; /* keep live bypass updates only when rules provide a table */
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

/* enable the first compatible full-device backend without resolving dns */
rexec_status_t routing_exec_up(routing_exec_t *st, int socks_port,
                               const char *server_ip, const char *server_ips,
                               const char *dns_upstream, int dns_local_port);

void routing_exec_down(routing_exec_t *st);

/* clear crash leftovers before installing a new catch-all rule */
void routing_exec_clear_stale(void);

/* add a live bypass so probes do not loop through the redirect */
void routing_exec_bypass_add_ipv4(routing_exec_t *st, const char *ip);

#ifdef __cplusplus
}
#endif

#endif /* routing_exec_h */
