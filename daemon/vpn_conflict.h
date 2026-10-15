#ifndef SENKO_VPN_CONFLICT_H
#define SENKO_VPN_CONFLICT_H

#include <netinet/in.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* another vpn on the device is the usual reason a tunnel cannot be routed: it
   owns the default route, or its interface answers the server lookup. the
   daemon names it instead of passing a bare routing socket error on */

#define VPN_CONFLICT_MAX 4

typedef struct {
    char ifname[16];
    char address[INET_ADDRSTRLEN];
} vpn_iface_t;

/* utun, ppp and ipsec are the interfaces a vpn client creates */
int vpn_ifname_is_tunnel(const char *ifname);

/* tunnel interfaces that are up and hold a usable ipv4 address, other than
   own. the system's own utun interfaces carry only ipv6 link local addresses
   and are not listed. returns how many out received */
size_t vpn_conflict_scan(const char *own, vpn_iface_t *out, size_t max);

/* the sentence naming the interfaces, or an empty string for none */
void vpn_conflict_text(const vpn_iface_t *list, size_t count, char *out, size_t cap);

/* appends the sentence to a failure reason when another vpn is up */
void vpn_conflict_explain(char *reason, size_t cap, const char *own);

#ifdef __cplusplus
}
#endif

#endif
