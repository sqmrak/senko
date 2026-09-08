#ifndef ROUTING_FWD_H
#define ROUTING_FWD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the plainest transparent mode the C backend has: a single ipfw fwd rule in
   front of a bypass list. ipfw keeps the original destination on the accepted
   socket, so the listener reads it with getsockname and needs no pf lookup */
typedef struct {
    int  active;
    int  app_proxy; /* no firewall tool, the in-process connect hook carries traffic */
    int  redir_port;
    int  socks_port;
    char server_ips[4096];
    char scoped_route_prev[16];
    int  next_bypass_slot;
} routing_fwd_t;

int  routing_fwd_up(routing_fwd_t *st, int socks_port,
                    const char *server_ip, const char *server_ips);

/* publish the socks port for senkotlsfix when no firewall tool exists */
int  routing_fwd_app_proxy_up(routing_fwd_t *st, int socks_port);

void routing_fwd_down(routing_fwd_t *st);
void routing_fwd_bypass_add_ipv4(routing_fwd_t *st, const char *ip);
void routing_fwd_clear_rules(void);

#ifdef __cplusplus
}
#endif

#endif /* ROUTING_FWD_H */
