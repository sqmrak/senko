#define _DEFAULT_SOURCE

#include "egress.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static int egress_rank(const struct ifaddrs *p, int allow_v6) {
    if (!p->ifa_name || !p->ifa_addr) return 0;
    if (!(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) return 0;
    int family = p->ifa_addr->sa_family;
    if (family == AF_INET6) {
        if (!allow_v6) return 0;
        const struct in6_addr *a = &((const struct sockaddr_in6 *)p->ifa_addr)->sin6_addr;
        /* link-local and unique-local addresses have no route off the link */
        if (IN6_IS_ADDR_LINKLOCAL(a) || IN6_IS_ADDR_LOOPBACK(a) || (a->s6_addr[0] & 0xfe) == 0xfc)
            return 0;
    } else if (family == AF_INET) {
        const unsigned char *b = (const unsigned char *)&((const struct sockaddr_in *)p->ifa_addr)->sin_addr;
        if (b[0] == 169 && b[1] == 254) return 0;
    } else {
        return 0;
    }
    if (strncmp(p->ifa_name, "en", 2) == 0) return 2;
    if (strncmp(p->ifa_name, "pdp_ip", 6) == 0) return 1;
    return 0;
}

int egress_interface(char *name, size_t name_cap) {
    struct ifaddrs *ifa = NULL;
    if (name && name_cap) name[0] = '\0';
    if (!name || name_cap < 2 || getifaddrs(&ifa) != 0) return -1;
    int best = 0;
    for (struct ifaddrs *p = ifa; p; p = p->ifa_next) {
        int rank = egress_rank(p, 1);
        if (rank <= best) continue;
        snprintf(name, name_cap, "%s", p->ifa_name);
        best = rank;
    }
    freeifaddrs(ifa);
    return best ? 0 : -1;
}

int egress_snapshot(char *name, size_t name_cap, char *ip, size_t ip_cap) {
    struct ifaddrs *ifa = NULL;
    if (name && name_cap) name[0] = '\0';
    if (ip && ip_cap) ip[0] = '\0';
    if (!name || name_cap < 2 || !ip || ip_cap < INET_ADDRSTRLEN) return -1;
    if (getifaddrs(&ifa) != 0) return -1;

    int best = 0; /* 2 is wifi, 1 is cellular */
    for (struct ifaddrs *p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_name || !p->ifa_addr) continue;
        if (p->ifa_addr->sa_family != AF_INET) continue;
        if (!(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) continue;
        int rank = 0;
        if (strncmp(p->ifa_name, "en", 2) == 0) rank = 2;
        else if (strncmp(p->ifa_name, "pdp_ip", 6) == 0) rank = 1;
        if (rank <= best) continue;
        struct sockaddr_in *sin = (struct sockaddr_in *)p->ifa_addr;
        char addr[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &sin->sin_addr, addr, sizeof addr)) continue;
        /* a self-assigned address means the interface is up without a usable route */
        if (strncmp(addr, "169.254.", 8) == 0) continue;
        snprintf(name, name_cap, "%s", p->ifa_name);
        snprintf(ip, ip_cap, "%s", addr);
        best = rank;
    }
    freeifaddrs(ifa);
    return best ? 0 : -1;
}
