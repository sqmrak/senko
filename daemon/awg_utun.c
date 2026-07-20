#include "awg_utun.h"

#include <errno.h>
#include <string.h>

#if defined(__APPLE__)

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

#else

int awg_utun_open(char *ifname, size_t ifname_cap) {
    (void)ifname;
    (void)ifname_cap;
    errno = ENOTSUP;
    return -1;
}

#endif
