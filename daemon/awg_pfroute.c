#include "awg_pfroute.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#if defined(__APPLE__)
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#define AWG_RTM_VERSION 5
#define AWG_RTM_GET 4
#define AWG_RTM_ADD 1
#define AWG_RTM_DELETE 2
#define AWG_RTA_DST 0x1
#define AWG_RTA_GATEWAY 0x2
#define AWG_RTA_NETMASK 0x4
#define AWG_RTF_UP 0x1
#define AWG_RTF_GATEWAY 0x2
#define AWG_RTF_HOST 0x4
#define AWG_RTF_STATIC 0x800
#define AWG_AF_LINK 18

static size_t round_sockaddr(size_t length) {
    return (length + sizeof(unsigned int) - 1) & ~(sizeof(unsigned int) - 1);
}

struct awg_rt_metrics {
    unsigned int values[14];
};

struct awg_rt_msghdr {
    unsigned short rtm_msglen;
    unsigned char rtm_version;
    unsigned char rtm_type;
    unsigned short rtm_index;
    int rtm_flags;
    int rtm_addrs;
    int rtm_pid;
    int rtm_seq;
    int rtm_errno;
    int rtm_use;
    unsigned int rtm_inits;
    struct awg_rt_metrics rtm_rmx;
};

static int route_get4(const char *destination, unsigned char *buffer, size_t buffer_cap,
                      ssize_t *got_out) {
    if (!destination || !buffer || buffer_cap < sizeof(struct awg_rt_msghdr) + sizeof(struct sockaddr_in))
        return -1;
    memset(buffer, 0, buffer_cap);
    struct awg_rt_msghdr *hdr = (struct awg_rt_msghdr *)buffer;
    struct sockaddr_in *dst = (struct sockaddr_in *)(buffer + sizeof *hdr);
    dst->sin_len = sizeof *dst;
    dst->sin_family = AF_INET;
    if (inet_pton(AF_INET, destination, &dst->sin_addr) != 1) return -1;
    hdr->rtm_msglen = (unsigned short)(sizeof *hdr + sizeof *dst);
    hdr->rtm_version = AWG_RTM_VERSION;
    hdr->rtm_type = AWG_RTM_GET;
    hdr->rtm_addrs = AWG_RTA_DST;
    hdr->rtm_pid = getpid();
    hdr->rtm_seq = 1;
    int fd = socket(AF_ROUTE, SOCK_RAW, 0);
    if (fd < 0) return -1;
    if (write(fd, buffer, hdr->rtm_msglen) != hdr->rtm_msglen) { close(fd); return -1; }
    ssize_t got = read(fd, buffer, buffer_cap);
    close(fd);
    if (got < (ssize_t)sizeof *hdr) return -1;
    hdr = (struct awg_rt_msghdr *)buffer;
    if (hdr->rtm_errno != 0 || hdr->rtm_msglen > (unsigned)got) return -1;
    *got_out = got;
    return 0;
}

static const struct sockaddr *route_gateway(const unsigned char *buffer, ssize_t got) {
    const struct awg_rt_msghdr *hdr = (const struct awg_rt_msghdr *)buffer;
    const unsigned char *cursor = buffer + sizeof *hdr;
    const unsigned char *end = buffer + hdr->rtm_msglen;
    if (got < (ssize_t)sizeof *hdr || hdr->rtm_msglen > (unsigned)got) return NULL;
    for (int bit = 0; bit < 32 && cursor < end; ++bit) {
        if (!(hdr->rtm_addrs & (1 << bit))) continue;
        const struct sockaddr *sa = (const struct sockaddr *)cursor;
        size_t length = sa->sa_len ? sa->sa_len : sizeof(long);
        if (length > (size_t)(end - cursor)) return NULL;
        if ((1 << bit) == AWG_RTA_GATEWAY) return sa;
        cursor += round_sockaddr(length);
    }
    return NULL;
}

int awg_pfroute_probe_get4(const char *destination, char *detail, size_t detail_cap) {
    if (!destination || !detail || detail_cap == 0) return -1;
    unsigned char buffer[256];
    ssize_t got = 0;
    if (route_get4(destination, buffer, sizeof buffer, &got) != 0) return -1;
    const struct awg_rt_msghdr *hdr = (const struct awg_rt_msghdr *)buffer;
    const struct sockaddr *gateway = route_gateway(buffer, got);
    char gw[64] = "-";
    if (gateway && gateway->sa_family == AF_INET && gateway->sa_len >= sizeof(struct sockaddr_in))
        (void)inet_ntop(AF_INET, &((const struct sockaddr_in *)gateway)->sin_addr, gw, sizeof gw);
    snprintf(detail, detail_cap, "reply=%ld msglen=%u version=%u type=%u errno=%d gateway=%s",
             (long)got, hdr->rtm_msglen, hdr->rtm_version, hdr->rtm_type, hdr->rtm_errno, gw);
    return gateway && gateway->sa_family == AF_INET ? 0 : -1;
}

int awg_pfroute_gateway4(const char *destination, char *gateway, size_t gateway_cap) {
    unsigned char buffer[256];
    ssize_t got = 0;
    if (!gateway || gateway_cap == 0 ||
        route_get4(destination, buffer, sizeof buffer, &got) != 0)
        return -1;
    const struct sockaddr *sa = route_gateway(buffer, got);
    if (!sa || sa->sa_family != AF_INET || sa->sa_len < sizeof(struct sockaddr_in)) return -1;
    return inet_ntop(AF_INET, &((const struct sockaddr_in *)sa)->sin_addr, gateway, gateway_cap) ? 0 : -1;
}

static int route_host_change(int type, const char *destination, const char *gateway) {
    unsigned char buffer[256]; memset(buffer, 0, sizeof buffer);
    struct awg_rt_msghdr *hdr = (struct awg_rt_msghdr *)buffer;
    struct sockaddr_in *dst = (struct sockaddr_in *)(buffer + sizeof *hdr);
    struct sockaddr_in *gw = (struct sockaddr_in *)((unsigned char *)dst + sizeof *dst);
    dst->sin_len = sizeof *dst; dst->sin_family = AF_INET;
    gw->sin_len = sizeof *gw; gw->sin_family = AF_INET;
    if (inet_pton(AF_INET, destination, &dst->sin_addr) != 1 ||
        inet_pton(AF_INET, gateway, &gw->sin_addr) != 1) return -1;
    hdr->rtm_msglen = (unsigned short)(sizeof *hdr + sizeof *dst + sizeof *gw);
    hdr->rtm_version = AWG_RTM_VERSION; hdr->rtm_type = (unsigned char)type;
    hdr->rtm_flags = AWG_RTF_UP | AWG_RTF_GATEWAY | AWG_RTF_HOST | AWG_RTF_STATIC;
    hdr->rtm_addrs = AWG_RTA_DST | AWG_RTA_GATEWAY; hdr->rtm_pid = getpid(); hdr->rtm_seq = 7 + type;
    int fd = socket(AF_ROUTE, SOCK_RAW, 0); if (fd < 0) return -1;
    if (write(fd, buffer, hdr->rtm_msglen) != hdr->rtm_msglen) { close(fd); return -1; }
    ssize_t got = read(fd, buffer, sizeof buffer); close(fd);
    if (got < (ssize_t)sizeof *hdr) return -1;
    return ((struct awg_rt_msghdr *)buffer)->rtm_errno == 0 ? 0 : -1;
}

int awg_pfroute_host4(int add, const char *destination, const char *gateway) {
    return route_host_change(add ? AWG_RTM_ADD : AWG_RTM_DELETE, destination, gateway);
}

int awg_pfroute_net4_if(int add, const char *network, const char *netmask, const char *ifname) {
    unsigned int index = if_nametoindex(ifname);
    if (!index || index > 0xffff) return -1;
    unsigned char buffer[256]; memset(buffer, 0, sizeof buffer);
    struct awg_rt_msghdr *hdr = (struct awg_rt_msghdr *)buffer;
    struct sockaddr_in *dst = (struct sockaddr_in *)(buffer + sizeof *hdr);
    unsigned char *gw = (unsigned char *)dst + sizeof *dst;
    struct sockaddr_in *mask = (struct sockaddr_in *)(gw + 8);
    dst->sin_len = sizeof *dst; dst->sin_family = AF_INET;
    mask->sin_len = sizeof *mask; mask->sin_family = AF_INET;
    if (inet_pton(AF_INET, network, &dst->sin_addr) != 1 || inet_pton(AF_INET, netmask, &mask->sin_addr) != 1) return -1;
    gw[0] = 8; gw[1] = AWG_AF_LINK;
    gw[2] = (unsigned char)index; gw[3] = (unsigned char)(index >> 8);
    hdr->rtm_msglen = (unsigned short)(sizeof *hdr + sizeof *dst + 8 + sizeof *mask);
    hdr->rtm_version = AWG_RTM_VERSION; hdr->rtm_type = add ? AWG_RTM_ADD : AWG_RTM_DELETE;
    hdr->rtm_flags = AWG_RTF_UP | AWG_RTF_STATIC; hdr->rtm_addrs = AWG_RTA_DST | AWG_RTA_GATEWAY | AWG_RTA_NETMASK; hdr->rtm_pid = getpid();
    int fd = socket(AF_ROUTE, SOCK_RAW, 0); if (fd < 0) return -1;
    if (write(fd, buffer, hdr->rtm_msglen) != hdr->rtm_msglen) { close(fd); return -1; }
    ssize_t got = read(fd, buffer, sizeof buffer); close(fd);
    return got >= (ssize_t)sizeof *hdr && ((struct awg_rt_msghdr *)buffer)->rtm_errno == 0 ? 0 : -1;
}

int awg_pfroute_net6_if(int add, const char *network, const char *netmask, const char *ifname) {
    unsigned int index = if_nametoindex(ifname);
    if (!index || index > 0xffff) return -1;
    unsigned char buffer[256]; memset(buffer, 0, sizeof buffer);
    struct awg_rt_msghdr *hdr = (struct awg_rt_msghdr *)buffer;
    struct sockaddr_in6 *dst = (struct sockaddr_in6 *)(buffer + sizeof *hdr);
    unsigned char *gw = (unsigned char *)dst + sizeof *dst;
    struct sockaddr_in6 *mask = (struct sockaddr_in6 *)(gw + 8);
    dst->sin6_len = sizeof *dst; dst->sin6_family = AF_INET6;
    mask->sin6_len = sizeof *mask; mask->sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, network, &dst->sin6_addr) != 1 ||
        inet_pton(AF_INET6, netmask, &mask->sin6_addr) != 1)
        return -1;
    gw[0] = 8; gw[1] = AWG_AF_LINK;
    gw[2] = (unsigned char)index; gw[3] = (unsigned char)(index >> 8);
    hdr->rtm_msglen = (unsigned short)(sizeof *hdr + sizeof *dst + 8 + sizeof *mask);
    hdr->rtm_version = AWG_RTM_VERSION; hdr->rtm_type = add ? AWG_RTM_ADD : AWG_RTM_DELETE;
    hdr->rtm_flags = AWG_RTF_UP | AWG_RTF_STATIC;
    hdr->rtm_addrs = AWG_RTA_DST | AWG_RTA_GATEWAY | AWG_RTA_NETMASK;
    hdr->rtm_index = (unsigned short)index; hdr->rtm_pid = getpid();
    int fd = socket(AF_ROUTE, SOCK_RAW, 0); if (fd < 0) return -1;
    if (write(fd, buffer, hdr->rtm_msglen) != hdr->rtm_msglen) { close(fd); return -1; }
    ssize_t got = read(fd, buffer, sizeof buffer); close(fd);
    return got >= (ssize_t)sizeof *hdr && ((struct awg_rt_msghdr *)buffer)->rtm_errno == 0 ? 0 : -1;
}

int awg_pfroute_probe_host4(const char *destination, const char *gateway, char *detail, size_t detail_cap) {
    int add = route_host_change(AWG_RTM_ADD, destination, gateway);
    int del = add == 0 ? route_host_change(AWG_RTM_DELETE, destination, gateway) : -1;
    snprintf(detail, detail_cap, "host add=%d delete=%d", add, del);
    return add == 0 && del == 0 ? 0 : -1;
}
#else
int awg_pfroute_probe_get4(const char *destination, char *detail, size_t detail_cap) {
    (void)destination;
    if (detail && detail_cap) snprintf(detail, detail_cap, "pf_route unavailable");
    return -1;
}
int awg_pfroute_probe_host4(const char *destination, const char *gateway, char *detail, size_t detail_cap) {
    (void)destination; (void)gateway; if (detail && detail_cap) snprintf(detail, detail_cap, "pf_route unavailable"); return -1;
}
int awg_pfroute_host4(int add, const char *destination, const char *gateway) { (void)add; (void)destination; (void)gateway; return -1; }
int awg_pfroute_net4_if(int add, const char *network, const char *netmask, const char *ifname) { (void)add; (void)network; (void)netmask; (void)ifname; return -1; }
int awg_pfroute_net6_if(int add, const char *network, const char *netmask, const char *ifname) { (void)add; (void)network; (void)netmask; (void)ifname; return -1; }
#endif
