#define _DEFAULT_SOURCE

#include "vpn_conflict.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>

int vpn_ifname_is_tunnel(const char *ifname) {
    if (!ifname) return 0;
    return strncmp(ifname, "utun", 4) == 0 || strncmp(ifname, "ppp", 3) == 0 ||
           strncmp(ifname, "ipsec", 5) == 0;
}

/* 169.254/16 is what an interface gets when nothing configured it */
static int usable_ipv4(const struct sockaddr_in *sin) {
    uint32_t a = ntohl(sin->sin_addr.s_addr);
    return a != 0 && (a >> 24) != 127 && (a >> 16) != 0xa9fe;
}

size_t vpn_conflict_scan(const char *own, vpn_iface_t *out, size_t max) {
    size_t count = 0;
    struct ifaddrs *all = NULL;
    if (!out || !max || getifaddrs(&all) != 0) return 0;
    for (struct ifaddrs *it = all; it && count < max; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        if (!vpn_ifname_is_tunnel(it->ifa_name) || (own && !strcmp(own, it->ifa_name)))
            continue;
        if (!(it->ifa_flags & IFF_UP) ||
            !usable_ipv4((const struct sockaddr_in *)it->ifa_addr))
            continue;
        int seen = 0;
        for (size_t i = 0; i < count; ++i)
            if (!strcmp(out[i].ifname, it->ifa_name)) seen = 1;
        if (seen) continue;
        memset(&out[count], 0, sizeof out[count]);
        snprintf(out[count].ifname, sizeof out[count].ifname, "%s", it->ifa_name);
        inet_ntop(AF_INET, &((const struct sockaddr_in *)it->ifa_addr)->sin_addr,
                  out[count].address, sizeof out[count].address);
        ++count;
    }
    freeifaddrs(all);
    return count;
}

void vpn_conflict_text(const vpn_iface_t *list, size_t count, char *out, size_t cap) {
    if (!out || !cap) return;
    out[0] = '\0';
    if (!list || !count) return;
    size_t used = (size_t)snprintf(out, cap, "another VPN is active on this device (");
    for (size_t i = 0; i < count && used < cap; ++i) {
        int n = snprintf(out + used, cap - used, "%s%s %s", i ? ", " : "",
                         list[i].ifname, list[i].address);
        if (n < 0) break;
        used += (size_t)n;
    }
    if (used < cap)
        snprintf(out + used, cap - used, "), turn it off before connecting senko");
}

void vpn_conflict_explain(char *reason, size_t cap, const char *own) {
    if (!reason || !cap) return;
    vpn_iface_t found[VPN_CONFLICT_MAX];
    size_t n = vpn_conflict_scan(own, found, VPN_CONFLICT_MAX);
    if (!n) return;
    char text[160];
    vpn_conflict_text(found, n, text, sizeof text);
    size_t used = strlen(reason);
    if (used < cap) snprintf(reason + used, cap - used, ". %s", text);
}
