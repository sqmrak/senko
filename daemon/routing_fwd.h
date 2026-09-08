#ifndef ROUTING_IOS5_H
#define ROUTING_IOS5_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  active;
    int  redir_port;
    int  socks_port;
    char server_ips[4096];
    int  scoped_route_saved;
    char scoped_route_prev[16];
    int  next_bypass_slot;
} routing_ios5_t;

int  routing_ios5_up(routing_ios5_t *st, int socks_port,
                     const char *server_ip, const char *server_ips);
void routing_ios5_down(routing_ios5_t *st);
void routing_ios5_bypass_add_ipv4(routing_ios5_t *st, const char *ip);
void routing_ios5_clear_rules(void);

#ifdef __cplusplus
}
#endif

#endif /* ROUTING_IOS5_H */
