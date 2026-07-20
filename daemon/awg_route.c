#include "awg_route.h"
#include "awg_pfroute.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#if defined(__APPLE__)
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioccom.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
struct awg_in6_addrlifetime {
    uint32_t expire;
    uint32_t preferred;
    uint32_t valid_lifetime;
    uint32_t preferred_lifetime;
};

struct awg_in6_aliasreq {
    char ifra_name[IFNAMSIZ];
    struct sockaddr_in6 ifra_addr;
    struct sockaddr_in6 ifra_dstaddr;
    struct sockaddr_in6 ifra_prefixmask;
    int ifra_flags;
    struct awg_in6_addrlifetime ifra_lifetime;
};

#define AWG_SIOCAIFADDR_IN6 _IOW('i', 26, struct awg_in6_aliasreq)
#endif

static int copy_checked(char *out, size_t cap, const char *value) {
    if (!value || !value[0] || strlen(value) >= cap) return -1;
    snprintf(out, cap, "%s", value);
    return 0;
}

static int valid_ifname(const char *ifname) {
    if (!ifname || !ifname[0] || strlen(ifname) >= 32) return 0;
    for (const char *p = ifname; *p; ++p)
        if (!(('a' <= *p && *p <= 'z') || ('A' <= *p && *p <= 'Z') ||
              ('0' <= *p && *p <= '9')))
            return 0;
    return 1;
}

static int address_without_prefix(const char *input, char *out, size_t cap, int family) {
    const char *slash = strchr(input, '/');
    size_t len = slash ? (size_t)(slash - input) : strlen(input);
    if (len == 0 || len >= cap) return -1;
    memcpy(out, input, len);
    out[len] = '\0';
    unsigned char raw[16];
    return inet_pton(family, out, raw) == 1 ? 0 : -1;
}

int awg_route_plan_for_interface(const awg_config_t *cfg, const char *ifname,
                                 awg_route_plan_t *plan) {
    if (!cfg || !plan || !valid_ifname(ifname)) return -1;
    memset(plan, 0, sizeof *plan);
    if (copy_checked(plan->ifname, sizeof plan->ifname, ifname) != 0)
        return -1;
    plan->mtu = cfg->mtu;
    for (size_t i = 0; i < cfg->address_count; ++i) {
        if (!plan->has_ipv4 && address_without_prefix(cfg->addresses[i], plan->ipv4,
                                                       sizeof plan->ipv4, AF_INET) == 0)
            plan->has_ipv4 = 1;
        if (!plan->has_ipv6 && address_without_prefix(cfg->addresses[i], plan->ipv6,
                                                       sizeof plan->ipv6, AF_INET6) == 0)
            plan->has_ipv6 = 1;
    }
    return plan->has_ipv4 || plan->has_ipv6 ? 0 : -1;
}

int awg_route_plan_build(const awg_config_t *cfg, const char *ifname,
                         const char *endpoint, const char *gateway,
                         awg_route_plan_t *plan) {
    unsigned char raw[4];
    if (awg_route_plan_for_interface(cfg, ifname, plan) != 0 ||
        inet_pton(AF_INET, endpoint, raw) != 1 || inet_pton(AF_INET, gateway, raw) != 1 ||
        copy_checked(plan->endpoint, sizeof plan->endpoint, endpoint) != 0 ||
        copy_checked(plan->gateway, sizeof plan->gateway, gateway) != 0)
        return -1;
    return 0;
}

#if defined(__APPLE__)
static int set_ifaddr4(int fd, const char *ifname, const char *ip, unsigned long request) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_len = sizeof *sin;
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &sin->sin_addr) != 1) return -1;
    return ioctl(fd, request, &ifr);
}

static int set_ifaddr6(const char *ifname, const char *ip) {
    struct awg_in6_aliasreq request;
    memset(&request, 0, sizeof request);
    snprintf(request.ifra_name, sizeof request.ifra_name, "%s", ifname);
    request.ifra_addr.sin6_len = sizeof request.ifra_addr;
    request.ifra_addr.sin6_family = AF_INET6;
    request.ifra_prefixmask.sin6_len = sizeof request.ifra_prefixmask;
    request.ifra_prefixmask.sin6_family = AF_INET6;
    memset(request.ifra_prefixmask.sin6_addr.s6_addr, 0xff,
           sizeof request.ifra_prefixmask.sin6_addr.s6_addr);
    request.ifra_lifetime.valid_lifetime = UINT32_MAX;
    request.ifra_lifetime.preferred_lifetime = UINT32_MAX;
    if (inet_pton(AF_INET6, ip, &request.ifra_addr.sin6_addr) != 1) return -1;
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int rc = ioctl(fd, AWG_SIOCAIFADDR_IN6, &request);
    close(fd);
    return rc;
}

int awg_route_interface_up(const awg_route_plan_t *plan) {
    if (!plan || !plan->has_ipv4) return -1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int ok = set_ifaddr4(fd, plan->ifname, plan->ipv4, SIOCSIFADDR) == 0 &&
             set_ifaddr4(fd, plan->ifname, plan->ipv4, SIOCSIFDSTADDR) == 0;
    if (ok) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof ifr);
        snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", plan->ifname);
        struct sockaddr_in *mask = (struct sockaddr_in *)&ifr.ifr_addr;
        mask->sin_len = sizeof *mask;
        mask->sin_family = AF_INET;
        mask->sin_addr.s_addr = htonl(0xffffffffU);
        ok = ioctl(fd, SIOCSIFNETMASK, &ifr) == 0;
        if (ok && ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
            ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
            ok = ioctl(fd, SIOCSIFFLAGS, &ifr) == 0;
        }
        if (ok) {
            memset(&ifr, 0, sizeof ifr);
            snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", plan->ifname);
            ifr.ifr_mtu = plan->mtu;
            ok = ioctl(fd, SIOCSIFMTU, &ifr) == 0;
        }
    }
    close(fd);
    if (ok && plan->has_ipv6) ok = set_ifaddr6(plan->ifname, plan->ipv6) == 0;
    return ok ? 0 : -1;
}

static void awg_route_interface_down(const awg_route_plan_t *plan) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", plan->ifname);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
        ifr.ifr_flags &= (short)~IFF_UP;
        (void)ioctl(fd, SIOCSIFFLAGS, &ifr);
    }
    close(fd);
}

int awg_route_plan_up(const awg_route_plan_t *plan) {
    if (!plan || !plan->has_ipv4) return -1;
    if (awg_route_interface_up(plan) != 0 ||
        awg_pfroute_host4(1, plan->endpoint, plan->gateway) != 0 ||
        awg_pfroute_net4_if(1, "0.0.0.0", "128.0.0.0", plan->ifname) != 0 ||
        awg_pfroute_net4_if(1, "128.0.0.0", "128.0.0.0", plan->ifname) != 0 ||
        (plan->has_ipv6 && (awg_pfroute_net6_if(1, "::", "8000::", plan->ifname) != 0 ||
                            awg_pfroute_net6_if(1, "8000::", "8000::", plan->ifname) != 0)))
        goto rollback;
    return 0;
rollback:
    awg_route_plan_down(plan);
    return -1;
}

void awg_route_plan_down(const awg_route_plan_t *plan) {
    if (!plan) return;
    if (plan->has_ipv4) {
        if (plan->has_ipv6) {
            (void)awg_pfroute_net6_if(0, "8000::", "8000::", plan->ifname);
            (void)awg_pfroute_net6_if(0, "::", "8000::", plan->ifname);
        }
        (void)awg_pfroute_net4_if(0, "128.0.0.0", "128.0.0.0", plan->ifname);
        (void)awg_pfroute_net4_if(0, "0.0.0.0", "128.0.0.0", plan->ifname);
        (void)awg_pfroute_host4(0, plan->endpoint, plan->gateway);
        awg_route_interface_down(plan);
    }
}

int awg_route_gateway_for_endpoint(const char *endpoint, char *gateway, size_t gateway_cap) {
    return awg_pfroute_gateway4(endpoint, gateway, gateway_cap);
}
#else
int awg_route_plan_up(const awg_route_plan_t *plan) { (void)plan; return -1; }
void awg_route_plan_down(const awg_route_plan_t *plan) { (void)plan; }
int awg_route_gateway_for_endpoint(const char *endpoint, char *gateway, size_t gateway_cap) {
    (void)endpoint; (void)gateway; (void)gateway_cap; return -1;
}
int awg_route_interface_up(const awg_route_plan_t *plan) { (void)plan; return -1; }
#endif
