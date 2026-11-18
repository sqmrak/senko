#include "awg_utun.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(__APPLE__)

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef AF_SYSTEM
#define AF_SYSTEM 32
#endif
#ifndef PF_SYSTEM
#define PF_SYSTEM AF_SYSTEM
#endif
#ifndef SYSPROTO_CONTROL
#define SYSPROTO_CONTROL 2
#endif
#ifndef AF_SYS_CONTROL
#define AF_SYS_CONTROL 2
#endif
#ifndef UTUN_OPT_IFNAME
#define UTUN_OPT_IFNAME 2
#endif
#ifndef UTUN_OPT_SET_DELEGATE_INTERFACE
#define UTUN_OPT_SET_DELEGATE_INTERFACE 15
#endif

#define SENKO_MAX_KCTL_NAME 96

struct senko_ctl_info {
    unsigned int ctl_id;
    char ctl_name[SENKO_MAX_KCTL_NAME];
};

struct senko_sockaddr_ctl {
    unsigned char sc_len;
    unsigned char sc_family;
    unsigned short ss_sysaddr;
    unsigned int sc_id;
    unsigned int sc_unit;
    unsigned int sc_reserved[5];
};

#ifndef SENKO_CTLIOCGINFO
#define SENKO_CTLIOCGINFO _IOWR('N', 3, struct senko_ctl_info)
#endif

int awg_utun_open(char *ifname, size_t ifname_cap) {
    if (!ifname || ifname_cap < 2) {
        errno = EINVAL;
        return -1;
    }
    struct senko_ctl_info info;
    memset(&info, 0, sizeof info);
    strncpy(info.ctl_name, "com.apple.net.utun_control", sizeof info.ctl_name - 1);
    int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) return -1;
    if (ioctl(fd, SENKO_CTLIOCGINFO, &info) != 0) {
        close(fd);
        return -1;
    }
    struct senko_sockaddr_ctl addr;
    memset(&addr, 0, sizeof addr);
    addr.sc_len = sizeof addr;
    addr.sc_family = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    addr.sc_id = info.ctl_id;
    addr.sc_unit = 0;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return -1;
    }
    socklen_t got = (socklen_t)ifname_cap;
    if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname, &got) != 0 || !ifname[0]) {
        close(fd);
        return -1;
    }
    ifname[ifname_cap - 1] = '\0';
    return fd;
}

int awg_utun_set_delegate(int fd, const char *physical_ifname) {
    if (fd < 0) return -1;
    if (!physical_ifname || !physical_ifname[0]) {
        return setsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_SET_DELEGATE_INTERFACE, "", 0);
    }
    size_t len = strlen(physical_ifname);
    if (len > 15) return -1; /* IFNAMSIZ - 1 */
    return setsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_SET_DELEGATE_INTERFACE,
                      physical_ifname, (socklen_t)len);
}

static int addr_is_usable(const struct sockaddr *sa) {
    if (!sa) return 0;
    if (sa->sa_family == AF_INET) {
        uint32_t ip = ntohl(((const struct sockaddr_in *)sa)->sin_addr.s_addr);
        if (ip == 0) return 0;
        if ((ip & 0xff000000U) == 0x7f000000U) return 0; /* 127.0.0.0/8 */
        if ((ip & 0xffff0000U) == 0xa9fe0000U) return 0; /* 169.254.0.0/16 */
        return 1;
    }
    if (sa->sa_family == AF_INET6) {
        const unsigned char *a = ((const struct sockaddr_in6 *)sa)->sin6_addr.s6_addr;
        int all_zero = 1;
        for (int i = 0; i < 16; ++i) {
            if (a[i] != 0) { all_zero = 0; break; }
        }
        if (all_zero) return 0;
        if (a[15] == 1) {
            int lo = 1;
            for (int i = 0; i < 15; ++i) {
                if (a[i] != 0) { lo = 0; break; }
            }
            if (lo) return 0;
        }
        if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) return 0; /* fe80::/10 */
        return 1;
    }
    return 0;
}

int awg_utun_detect_live_physical(char *out, size_t out_cap) {
    if (!out || out_cap == 0) return -1;
    out[0] = '\0';
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return -1;
    char cell[32];
    cell[0] = '\0';
    for (struct ifaddrs *it = list; it; it = it->ifa_next) {
        if (!it->ifa_name || !it->ifa_addr) continue;
        if ((it->ifa_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING))
            continue;
        if (!addr_is_usable(it->ifa_addr)) continue;
        if (strcmp(it->ifa_name, "en0") == 0) {
            snprintf(out, out_cap, "en0");
            freeifaddrs(list);
            return 0;
        }
        if (!cell[0] && strncmp(it->ifa_name, "pdp_ip", 6) == 0) {
            snprintf(cell, sizeof cell, "%s", it->ifa_name);
        }
    }
    freeifaddrs(list);
    if (cell[0]) snprintf(out, out_cap, "%s", cell);
    return 0;
}

#else

int awg_utun_open(char *ifname, size_t ifname_cap) {
    (void)ifname;
    (void)ifname_cap;
    errno = ENOTSUP;
    return -1;
}

int awg_utun_set_delegate(int fd, const char *physical_ifname) {
    (void)fd;
    (void)physical_ifname;
    errno = ENOTSUP;
    return -1;
}

int awg_utun_detect_live_physical(char *out, size_t out_cap) {
    if (out && out_cap) out[0] = '\0';
    return -1;
}

#endif
