#include "pf_natlook.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sockio.h>
#endif

/* ios/macos pfioc_natlook layout (84 bytes). port fields are 4-byte slots */
struct pfioc_natlook_nl {
    uint8_t saddr[16];
    uint8_t daddr[16];
    uint8_t rsaddr[16];
    uint8_t rdaddr[16];
    uint8_t sxport[4];
    uint8_t dxport[4];
    uint8_t rsxport[4];
    uint8_t rdxport[4];
    uint8_t af;
    uint8_t proto;
    uint8_t proto_variant;
    uint8_t direction;
};

#ifndef PF_OUT
#define PF_OUT 2
#endif
#ifndef PF_IN
#define PF_IN  1
#endif

#if defined(__APPLE__)
#define PFIOC_NATLOOK_LEN (4 * 16 + 4 * 4 + 4)
#ifndef IOC_INOUT
#define IOC_INOUT 0x80000000u
#endif
#ifndef IOCPARM_MASK
#define IOCPARM_MASK 0x1fffu
#endif
#ifndef DIOCNATLOOK
#define DIOCNATLOOK (IOC_INOUT | ((PFIOC_NATLOOK_LEN & IOCPARM_MASK) << 16) | ('D' << 8) | 23)
#endif
#endif

#ifndef DIOCNATLOOK
#define DIOCNATLOOK _IOWR('D', 23, struct pfioc_natlook_nl)
#endif

static int g_pf_fd = -1;

extern char **environ;

static int pf_fd_get(void) {
    if (g_pf_fd >= 0) return g_pf_fd;
    g_pf_fd = open("/dev/pf", O_RDWR);
    return g_pf_fd;
}

static const char *find_pfctl(void) {
    static const char *paths[] = {
        "/sbin/pfctl", "/usr/sbin/pfctl", "/bin/pfctl", "/usr/bin/pfctl"
    };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; ++i)
        if (access(paths[i], X_OK) == 0) return paths[i];
    return NULL;
}

void pf_natlook_close(void) {
    if (g_pf_fd >= 0) {
        close(g_pf_fd);
        g_pf_fd = -1;
    }
}

static void put_v4(struct pfioc_natlook_nl *nl, const struct in_addr *a,
                   uint8_t *addr_slot, uint8_t *port_slot, uint16_t port) {
    memset(addr_slot, 0, 16);
    memcpy(addr_slot, a, sizeof *a);
    memset(port_slot, 0, 4);
    port_slot[0] = (uint8_t)(port >> 8);
    port_slot[1] = (uint8_t)(port & 0xff);
    (void)nl;
}

static int natlook_try_one(int pffd, struct pfioc_natlook_nl *nl, int direction) {
    nl->af = AF_INET;
    nl->proto = IPPROTO_TCP;
    nl->proto_variant = 0;
    nl->direction = (uint8_t)direction;
    if (ioctl(pffd, DIOCNATLOOK, nl) == 0)
        return 0;
    return -1;
}

static int natlook_try(int pffd, const struct pfioc_natlook_nl *base,
                       struct pfioc_natlook_nl *out) {
    struct pfioc_natlook_nl nl = *base;
    if (natlook_try_one(pffd, &nl, PF_OUT) == 0) {
        *out = nl;
        return 0;
    }
    nl = *base;
    if (natlook_try_one(pffd, &nl, PF_IN) == 0) {
        *out = nl;
        return 0;
    }

    nl = *base;
    memcpy(nl.saddr, base->daddr, 16);
    memcpy(nl.daddr, base->saddr, 16);
    memcpy(nl.sxport, base->dxport, 4);
    memcpy(nl.dxport, base->sxport, 4);
    if (natlook_try_one(pffd, &nl, PF_OUT) == 0) {
        *out = nl;
        return 0;
    }
    nl = *base;
    memcpy(nl.saddr, base->daddr, 16);
    memcpy(nl.daddr, base->saddr, 16);
    memcpy(nl.sxport, base->dxport, 4);
    memcpy(nl.dxport, base->sxport, 4);
    if (natlook_try_one(pffd, &nl, PF_IN) == 0) {
        *out = nl;
        return 0;
    }
    return -1;
}

static int parse_rdr_line(const char *line, uint16_t redir_port, uint16_t client_port,
                          char *host, size_t host_cap, uint16_t *port) {
    char marker[64];
    snprintf(marker, sizeof marker, " -> 127.0.0.1:%u -> ",
             (unsigned)client_port);
    const char *p = strstr(line, marker);
    if (p) {
        p += strlen(marker);
        char ip[64];
        size_t n = 0;
        while (*p && *p != ':' && *p != ' ' && n + 1 < sizeof ip)
            ip[n++] = *p++;
        ip[n] = '\0';
        if (*p != ':' || n == 0) return -1;
        p++;
        char *end = NULL;
        unsigned long parsed = strtoul(p, &end, 10);
        if (end == p || parsed == 0 || parsed > 65535) return -1;
        struct in_addr tmp;
        if (inet_pton(AF_INET, ip, &tmp) != 1) return -1;
        snprintf(host, host_cap, "%s", ip);
        *port = (uint16_t)parsed;
        return 0;
    }

    snprintf(marker, sizeof marker, "127.0.0.1:%u <- ", (unsigned)redir_port);
    p = strstr(line, marker);
    if (!p) return -1;
    p += strlen(marker);

    snprintf(marker, sizeof marker, " <- 127.0.0.1:%u", (unsigned)client_port);
    if (!strstr(p, marker)) return -1;

    char ip[64];
    size_t n = 0;
    while (*p && *p != ':' && *p != ' ' && n + 1 < sizeof ip)
        ip[n++] = *p++;
    ip[n] = '\0';
    if (*p != ':' || n == 0) return -1;
    p++;

    char *end = NULL;
    unsigned long parsed = strtoul(p, &end, 10);
    if (end == p || parsed == 0 || parsed > 65535) return -1;
    struct in_addr tmp;
    if (inet_pton(AF_INET, ip, &tmp) != 1) return -1;

    snprintf(host, host_cap, "%s", ip);
    *port = (uint16_t)parsed;
    return 0;
}

#ifdef SENKO_HOST_TEST
int pf_natlook_parse_state_line_for_test(const char *line, uint16_t redir_port,
                                         uint16_t client_port, char *host,
                                         size_t host_cap, uint16_t *port) {
    return parse_rdr_line(line, redir_port, client_port, host, host_cap, port);
}
#endif

static int pfctl_state_dest(uint16_t redir_port, uint16_t client_port,
                            char *host, size_t host_cap, uint16_t *port) {
    const char *pfctl = find_pfctl();
    if (!pfctl) return -1;

    int fds[2];
    if (pipe(fds) != 0) return -1;

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);

    char *argv[] = { (char *)pfctl, (char *)"-s", (char *)"state", NULL };
    pid_t pid = 0;
    int rc = posix_spawn(&pid, pfctl, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        return -1;
    }

    FILE *fp = fdopen(fds[0], "r");
    if (!fp) {
        close(fds[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    int found = -1;
    char line[512];
    while (fgets(line, sizeof line, fp)) {
        if (parse_rdr_line(line, redir_port, client_port, host, host_cap, port) == 0) {
            found = 0;
            break;
        }
    }
    fclose(fp);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

    return found == 0 ? 0 : -1;
}

int pf_natlook_dest(int accepted_fd, const struct sockaddr_in *clientaddr,
                    uint16_t redir_port,
                    char *host, size_t host_cap, uint16_t *port) {
    if (!host || host_cap == 0 || !port || accepted_fd < 0 || !clientaddr)
        return -1;

    int pffd = pf_fd_get();
    if (pffd < 0) return -1;

    struct sockaddr_in local;
    socklen_t slen = sizeof local;
    if (getsockname(accepted_fd, (struct sockaddr *)&local, &slen) != 0)
        return -1;

    struct pfioc_natlook_nl nl;
    memset(&nl, 0, sizeof nl);
    put_v4(&nl, &clientaddr->sin_addr, nl.saddr, nl.sxport, clientaddr->sin_port);
    put_v4(&nl, &local.sin_addr, nl.daddr, nl.dxport, local.sin_port);

    struct pfioc_natlook_nl res;
    memset(&res, 0, sizeof res);
    if (natlook_try(pffd, &nl, &res) != 0) {
        uint16_t client_port = ntohs(clientaddr->sin_port);
        uint16_t bind_port = redir_port ? redir_port : ntohs(local.sin_port);
        if (pfctl_state_dest(bind_port, client_port, host, host_cap, port) == 0)
            return 0;
        char client[64], local_s[64];
        if (!inet_ntop(AF_INET, &clientaddr->sin_addr, client, sizeof client))
            snprintf(client, sizeof client, "?");
        if (!inet_ntop(AF_INET, &local.sin_addr, local_s, sizeof local_s))
            snprintf(local_s, sizeof local_s, "?");
        fprintf(stderr, "senkod: natlook failed client=%s:%u local=%s:%u\n",
                client, (unsigned)client_port,
                local_s, (unsigned)ntohs(local.sin_port));
        return -1;
    }

    struct in_addr rd;
    memcpy(&rd, res.rdaddr, sizeof rd);
    if (!inet_ntop(AF_INET, &rd, host, (socklen_t)host_cap))
        return -1;
    *port = (uint16_t)(((uint16_t)res.rdxport[0] << 8) | (uint16_t)res.rdxport[1]);
    return 0;
}
