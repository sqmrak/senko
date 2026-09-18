#define _DEFAULT_SOURCE

#include "routing_exec.h"
#include "core/net_safe.h"
#include "core/dns_cache.h"
#include "core/dns_msg.h"
#include "pf_table.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <spawn.h>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif
#include "../common/senko_paths.h"

extern char **environ;

#define PF_CONF "/var/run/senko-pf.conf"
#define PF_ERR  "/var/tmp/senko-pf.err"
#define PF_OS   SENKO_JBROOT "/etc/pf.os"
#define PF_ANCHOR "com.apple/senko"
#define PF_CONF_CAP (512u * 1024u)

static dns_cache_t g_dns_cache;
static pf_table_t g_pf_table;
static uint32_t g_pf_cleanup_added[PF_TABLE_MAX_ADDRS];
static uint32_t g_pf_cleanup_deleted[PF_TABLE_MAX_ADDRS];


static int can_exec(const char *path) {
    return path && access(path, X_OK) == 0;
}

static const char *find_first(const char *const *paths, size_t n) {
    for (size_t i = 0; i < n; ++i) if (can_exec(paths[i])) return paths[i];
    return NULL;
}

static const char *find_path_command(const char *name, char *path, size_t cap) {
    const char *env = getenv("PATH");
    if (!env || !name || !path || cap == 0) return NULL;
    char copy[2048];
    snprintf(copy, sizeof copy, "%s", env);
    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        int n = snprintf(path, cap, "%s/%s", *dir ? dir : ".", name);
        if (n > 0 && (size_t)n < cap && can_exec(path)) return path;
    }
    return NULL;
}

const char *routing_find_ipfw(void) {
    static const char *p[] = { SENKO_JBROOT "/sbin/ipfw", SENKO_JBROOT "/usr/sbin/ipfw",
                               SENKO_JBROOT "/bin/ipfw", SENKO_USR_BIN "/ipfw",
                               "/sbin/ipfw", "/usr/sbin/ipfw", "/bin/ipfw",
                               "/usr/bin/ipfw", "/usr/local/sbin/ipfw",
                               "/usr/local/bin/ipfw" };
    const char *found = find_first(p, sizeof p / sizeof p[0]);
    if (found) return found;
    static char path[256];
    return find_path_command("ipfw", path, sizeof path);
}
const char *routing_find_pfctl(void) {
    static const char *p[] = { SENKO_JBROOT "/sbin/pfctl", SENKO_JBROOT "/usr/sbin/pfctl",
                               SENKO_JBROOT "/bin/pfctl", SENKO_USR_BIN "/pfctl",
                               "/sbin/pfctl", "/usr/sbin/pfctl", "/bin/pfctl",
                               "/usr/bin/pfctl", "/usr/local/sbin/pfctl",
                               "/usr/local/bin/pfctl" };
    const char *found = find_first(p, sizeof p / sizeof p[0]);
    if (found) return found;
    static char path[256];
    return find_path_command("pfctl", path, sizeof path);
}
static const char *find_sysctl(void) {
    static const char *p[] = { SENKO_JBROOT "/sbin/sysctl", SENKO_JBROOT "/usr/sbin/sysctl",
                               SENKO_USR_BIN "/sysctl", "/sbin/sysctl", "/usr/sbin/sysctl",
                               "/usr/bin/sysctl" };
    return find_first(p, sizeof p / sizeof p[0]);
}

int routing_spawn(const char *bin, char *const argv[]) {
    pid_t pid = 0;
    int rc = posix_spawn(&pid, bin, NULL, NULL, argv, environ);
    if (rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int fd_write_all(int fd, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int fd_read_all(int fd, void *data, size_t len) {
    uint8_t *p = (uint8_t *)data;
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static uint64_t monotonic_seconds(void) {
#if defined(__APPLE__)
    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0) mach_timebase_info(&timebase);
    uint64_t ticks = mach_absolute_time();
    long double nanoseconds = (long double)ticks * timebase.numer / timebase.denom;
    return (uint64_t)(nanoseconds / 1000000000.0L);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec;
#endif
}

/* read a sysctl value into buf; the helper prints one line on stdout */
static int sysctl_read(const char *sysctl, const char *name,
                       char *buf, size_t cap) {
    char *argv[] = { (char *)sysctl, (char *)"-n", (char *)name, NULL };
    int fds[2];
    if (pipe(fds) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, sysctl, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        return -1;
    }
    ssize_t n;
    do {
        n = read(fds[0], buf, cap - 1);
    } while (n < 0 && errno == EINTR);
    close(fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (n <= 0) return -1;
    buf[n] = '\0';
    buf[strcspn(buf, "\r\n ")] = '\0';
    return buf[0] ? 0 : -1;
}

int routing_scopedroute_disable(char *prev, size_t cap) {
    if (!prev || cap == 0) return -1;
    prev[0] = '\0';
    const char *sysctl = find_sysctl();
    if (!sysctl) return -1;
    if (sysctl_read(sysctl, "net.inet.ip.scopedroute", prev, cap) != 0)
        return -1;
    /* already off, so nothing has to be restored later */
    if (strcmp(prev, "0") == 0) {
        prev[0] = '\0';
        return 0;
    }
    char val[] = "net.inet.ip.scopedroute=0";
    char *argv[] = { (char *)sysctl, (char *)"-w", val, NULL };
    if (routing_spawn(sysctl, argv) != 0) {
        prev[0] = '\0';
        return -1;
    }
    return 0;
}

void routing_scopedroute_restore(const char *prev) {
    if (!prev || !prev[0]) return;
    const char *sysctl = find_sysctl();
    if (!sysctl) return;
    char val[64];
    if (snprintf(val, sizeof val, "net.inet.ip.scopedroute=%s", prev) >= (int)sizeof val)
        return;
    char *argv[] = { (char *)sysctl, (char *)"-w", val, NULL };
    (void)routing_spawn(sysctl, argv);
}

static int run_spawn_quiet(const char *bin, char *const argv[]) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null",
                                     O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null",
                                     O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, bin, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int run_spawn_pf_capture(const char *bin, char *const argv[]) {
    posix_spawn_file_actions_t fa;
    if (posix_spawn_file_actions_init(&fa) != 0) return -1;
    int action_rc = posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null",
                                                      O_WRONLY, 0);
    if (action_rc == 0)
        action_rc = posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, PF_ERR,
                                                      O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (action_rc != 0) {
        posix_spawn_file_actions_destroy(&fa);
        return -1;
    }
    pid_t pid = 0;
    int rc = posix_spawn(&pid, bin, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void read_pf_error(char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    FILE *fp = fopen(PF_ERR, "r");
    if (!fp) return;
    char line[256];
    size_t used = 0;
    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        size_t len = strlen(line);
        size_t sep = used ? 2 : 0;
        if (used + sep + len >= cap) break;
        if (sep) {
            out[used++] = ';';
            out[used++] = ' ';
        }
        memcpy(out + used, line, len);
        used += len;
        out[used] = '\0';
    }
    fclose(fp);
    unlink(PF_ERR);
}

static void ensure_pf_os_file(void) {
    if (access(PF_OS, R_OK) == 0) return;
    int fd = open(PF_OS, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        if (errno != EEXIST)
            fprintf(stderr, "senkod: cannot create %s: %s\n", PF_OS, strerror(errno));
        return;
    }
    static const char placeholder[] = "# senko does not use os fingerprints\n";
    (void)write(fd, placeholder, sizeof placeholder - 1);
    close(fd);
}

static int split_rule_words(char *rule, char *argv[], int max) {
    int n = 0;
    char *p = rule;
    while (n < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[n++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

static int ipfw_q(const char *ipfw, char *const tail[], int tailc) {
    char *argv[36];
    int n = 0;
    argv[n++] = (char *)ipfw;
    argv[n++] = (char *)"-q";
    for (int i = 0; i < tailc && n < 35; ++i) argv[n++] = tail[i];
    argv[n] = NULL;
    return routing_spawn(ipfw, argv);
}

static int write_file(const char *path, const char *buf, size_t len) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    size_t w = fwrite(buf, 1, len, fp);
    fclose(fp);
    return (w == len) ? 0 : -1;
}

static int pf_table_batch(const routing_exec_t *st, const char *operation,
                          const uint32_t *addresses, size_t count) {
    const char *pfctl;
    char path[] = "/var/tmp/senko-pf-table.XXXXXX";
    int fd;
    int rc = -1;
    if (!st || !operation || !addresses || count == 0 ||
        st->mode != ROUTING_MODE_PF || !st->pf_table_ready)
        return count == 0 ? 0 : -1;
    pfctl = routing_find_pfctl();
    if (!pfctl) return -1;
    fd = mkstemp(path);
    if (fd < 0) return -1;
    (void)unlink(path);
    for (size_t i = 0; i < count; ++i) {
        char address[INET_ADDRSTRLEN + 2];
        if (pf_table_ipv4_text(addresses[i], address, sizeof address) != 0 ||
            fd_write_all(fd, address, strlen(address)) != 0 ||
            fd_write_all(fd, "\n", 1) != 0)
            goto done;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) goto done;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) goto done;
    int action_rc = posix_spawn_file_actions_adddup2(&actions, fd, STDIN_FILENO);
    if (action_rc == 0)
        action_rc = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
                                                      "/dev/null", O_WRONLY, 0);
    if (action_rc == 0)
        action_rc = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                                      "/dev/null", O_WRONLY, 0);
    if (action_rc != 0) {
        posix_spawn_file_actions_destroy(&actions);
        goto done;
    }
    pid_t pid = 0;
#if defined(SENKO_ROOTLESS)
    char *argv[] = { (char *)pfctl, (char *)"-q", (char *)"-a",
                     (char *)PF_ANCHOR, (char *)"-t", (char *)"senko_bypass",
                     (char *)"-T", (char *)operation, (char *)"-f", (char *)"-", NULL };
#else
    char *argv[] = { (char *)pfctl, (char *)"-q", (char *)"-t",
                     (char *)"senko_bypass", (char *)"-T", (char *)operation,
                     (char *)"-f", (char *)"-", NULL };
#endif
    int spawn_rc = posix_spawn(&pid, pfctl, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn_rc != 0) goto done;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    rc = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
done:
    close(fd);
    return rc;
}


int routing_pick_free_port(int start, int end) {
    if (start <= 0) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
            close(fd);
            return -1;
        }
        socklen_t len = sizeof sa;
        int port = getsockname(fd, (struct sockaddr *)&sa, &len) == 0
            ? (int)ntohs(sa.sin_port) : -1;
        close(fd);
        return port;
    }
    if (start > 65535) return -1;
    if (end <= 0 || end > 65535) end = 65535;
    for (int port = start; port <= end; ++port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = inet_addr("127.0.0.1");
        sa.sin_port = htons((uint16_t)port);
        int ok = (bind(fd, (struct sockaddr *)&sa, sizeof sa) == 0);
        close(fd);
        if (ok) return port;
    }
    return -1;
}

int routing_pick_free_udp_port(int start, int end) {
    if (start <= 0) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
            close(fd);
            return -1;
        }
        socklen_t len = sizeof sa;
        int port = getsockname(fd, (struct sockaddr *)&sa, &len) == 0
            ? (int)ntohs(sa.sin_port) : -1;
        close(fd);
        return port;
    }
    if (start > 65535) return -1;
    if (end <= 0 || end > 65535) end = 65535;
    for (int port = start; port <= end; ++port) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) continue;
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = inet_addr("127.0.0.1");
        sa.sin_port = htons((uint16_t)port);
        int ok = (bind(fd, (struct sockaddr *)&sa, sizeof sa) == 0);
        close(fd);
        if (ok) return port;
    }
    return -1;
}

static int bind_dns_socket(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof local_addr);
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    local_addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&local_addr, sizeof local_addr) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void close_dns_socket(routing_exec_t *st) {
    if (!st || !st->dns_bound) return;
    close(st->dns_fd);
    st->dns_fd = -1;
    st->dns_bound = 0;
}

/* pf needs a real egress interface */

static size_t collect_ifaces(char ifnames[][32], size_t cap) {
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) != 0) return 0;
    size_t n = 0;
    for (struct ifaddrs *p = ifa; p && n < cap; p = p->ifa_next) {
        if (!p->ifa_name) continue;
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        int wanted = (strncmp(p->ifa_name, "en", 2) == 0 ||
                      strncmp(p->ifa_name, "pdp_ip", 6) == 0);
        if (!wanted) continue;
        int seen = 0;
        for (size_t i = 0; i < n; ++i) if (strcmp(ifnames[i], p->ifa_name) == 0) seen = 1;
        if (seen) continue;
        snprintf(ifnames[n], 32, "%s", p->ifa_name);
        n++;
    }
    freeifaddrs(ifa);
    return n;
}

/* wifi first, then cellular: that is the order the kernel installs the default
   route in, so the first match is the egress the tunnel will actually use */
int routing_exec_egress_snapshot(char *name, size_t name_cap,
                                 char *ip, size_t ip_cap) {
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

static void clear_ipfw(void) {
    const char *ipfw = routing_find_ipfw();
    if (!ipfw) return;
    char num[16];
    char del[] = "delete";
    for (int n = 12030; n >= 12000; --n) {
        snprintf(num, sizeof num, "%d", n);
        char *argv[] = { (char *)ipfw, (char *)"-q", del, num, NULL };
        routing_spawn(ipfw, argv);
    }
}

static int apply_ipfw(const char *ipfw, const char *server_ip,
                      const char *server_ips,
                      int redir_port, int socks_port, int dns_local_port) {
    const char *sysctl = find_sysctl();
    if (sysctl) {
        char *argv[] = { (char *)sysctl, (char *)"-w",
                         (char *)"net.inet.ip.fw.enable=1", NULL };
        routing_spawn(sysctl, argv);
    }
    clear_ipfw();

    routing_ipfw_rule_t rules[ROUTING_MAX_RULES];
    size_t count = 0;
    if (routing_ipfw_rules(server_ip, redir_port, socks_port, dns_local_port,
                           rules, ROUTING_MAX_RULES, &count) != ROUTING_OK)
        return -1;

    for (size_t i = 0; i < count; ++i) {
        char num[16], add[] = "add";
        char rulebuf[256];
        char *argv[36];
        snprintf(num, sizeof num, "%d", rules[i].number);
        snprintf(rulebuf, sizeof rulebuf, "%s", rules[i].rule_out);
        int nw = split_rule_words(rulebuf, &argv[0], 32);
        if (nw <= 0) return -1;
        char *head[] = { add, num };
        char *full[34];
        int fn = 0;
        full[fn++] = head[0];
        full[fn++] = head[1];
        for (int j = 0; j < nw && fn < 33; ++j) full[fn++] = argv[j];
        if (ipfw_q(ipfw, full, fn) != 0) {
            snprintf(rulebuf, sizeof rulebuf, "%s", rules[i].rule_plain);
            nw = split_rule_words(rulebuf, &argv[0], 32);
            if (nw <= 0) return -1;
            fn = 0;
            full[fn++] = head[0];
            full[fn++] = head[1];
            for (int j = 0; j < nw && fn < 33; ++j) full[fn++] = argv[j];
            if (ipfw_q(ipfw, full, fn) != 0) return -1;
        }
    }

    char ips_copy[4096];
    strncpy(ips_copy, server_ips, sizeof ips_copy - 1);
    ips_copy[sizeof ips_copy - 1] = '\0';

    char *tok = strtok(ips_copy, ", ");
    while (tok) {
        char ip[64];
        if (net_ipv4_literal(tok, ip, sizeof ip)) {
            char num[] = "12001", add[] = "add";
            char *out_rule[] = { add, num, (char *)"allow", (char *)"tcp",
                                   (char *)"from", (char *)"any", (char *)"to", ip,
                                   (char *)"out", NULL };
            if (ipfw_q(ipfw, out_rule, 9) != 0) {
                char *plain_rule[] = { add, num, (char *)"allow", (char *)"tcp",
                                         (char *)"from", (char *)"any", (char *)"to", ip,
                                         NULL };
                ipfw_q(ipfw, plain_rule, 8);
            }
        }
        tok = strtok(NULL, ", ");
    }

    return 0;
}

static void clear_pf(void) {
    const char *pfctl = routing_find_pfctl();
    if (!pfctl) return;
#if defined(SENKO_ROOTLESS)
    char *argv[] = { (char *)pfctl, (char *)"-q", (char *)"-a",
                     (char *)PF_ANCHOR, (char *)"-F", (char *)"all", NULL };
#else
    char *argv[] = { (char *)pfctl, (char *)"-q", (char *)"-F", (char *)"all", NULL };
#endif
    run_spawn_quiet(pfctl, argv);
}

static int apply_pf_mode(const char *pfctl, const char *server_ips,
                         const ruleset_t *rules,
                         const char ifnames[][32], size_t if_count,
                         int redir_port, int dns_local_port,
                         routing_pf_mode_t mode,
                         char *detail, size_t detail_cap) {
    if (detail && detail_cap > 0) detail[0] = '\0';
    ensure_pf_os_file();
    const char *sysctl = find_sysctl();
    if (sysctl) {
        char *argv[] = { (char *)sysctl, (char *)"-w",
                           (char *)"net.inet.ip.forwarding=1", NULL };
        routing_spawn(sysctl, argv);
    }

    char *conf = (char *)malloc(PF_CONF_CAP);
    size_t clen = 0;
    if (!conf) return -1;
#if defined(SENKO_ROOTLESS)
    routing_status_t conf_rc = routing_pf_anchor_conf_rules(
        server_ips, rules, ifnames, if_count, redir_port, dns_local_port,
        mode, conf, PF_CONF_CAP, &clen);
#else
    routing_status_t conf_rc = routing_pf_conf_rules(
        server_ips, rules, ifnames, if_count, redir_port, dns_local_port,
        mode, conf, PF_CONF_CAP, &clen);
#endif
    if (conf_rc != ROUTING_OK) {
        free(conf);
        return -1;
    }
    if (write_file(PF_CONF, conf, clen) != 0) {
        free(conf);
        return -1;
    }
    free(conf);

    char *enargv[] = { (char *)pfctl, (char *)"-q", (char *)"-e", NULL };
    int enable_rc = run_spawn_quiet(pfctl, enargv);
    if (enable_rc != 0) {
        char *compat_enargv[] = { (char *)pfctl, (char *)"-q", (char *)"-E", NULL };
        run_spawn_quiet(pfctl, compat_enargv);
    }
#if defined(SENKO_ROOTLESS)
    char *lfargv[] = { (char *)pfctl, (char *)"-q", (char *)"-a",
                       (char *)PF_ANCHOR, (char *)"-f", (char *)PF_CONF, NULL };
#else
    char *lfargv[] = { (char *)pfctl, (char *)"-q", (char *)"-f", (char *)PF_CONF, NULL };
#endif
    int rc = run_spawn_pf_capture(pfctl, lfargv);
    if (rc != 0 && detail && detail_cap > 0) {
        read_pf_error(detail, detail_cap);
        if (!detail[0]) snprintf(detail, detail_cap, "pfctl exit %d", rc);
    } else {
        unlink(PF_ERR);
    }
    return rc == 0 ? 0 : -1;
}


static int socks5_connect_to_dns(int socks_port, const char *dns_upstream) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(socks_port);

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return -1;
    }

    uint8_t greet[] = { 0x05, 0x01, 0x00 };
    if (fd_write_all(fd, greet, sizeof greet) != 0) { close(fd); return -1; }

    uint8_t resp[10];
    if (fd_read_all(fd, resp, 2) != 0 || resp[0] != 0x05 || resp[1] != 0x00) {
        close(fd);
        return -1;
    }

    struct in_addr dns_addr;
    if (!dns_upstream || inet_pton(AF_INET, dns_upstream, &dns_addr) != 1) {
        close(fd);
        return -1;
    }

    uint8_t req[10] = {
        0x05, 0x01, 0x00, 0x01,
        0, 0, 0, 0,
        0x00, 0x35 /* port 53 */
    };
    memcpy(req + 4, &dns_addr.s_addr, 4);
    if (fd_write_all(fd, req, sizeof req) != 0) { close(fd); return -1; }

    if (fd_read_all(fd, resp, 10) != 0 || resp[0] != 0x05 || resp[1] != 0x00) {
        close(fd);
        return -1;
    }

    return fd;
}

static int dns_sendto(int fd, const uint8_t *data, size_t len,
                      const struct sockaddr_in *address, socklen_t address_len) {
    ssize_t n;
    do {
        n = sendto(fd, data, len, 0, (const struct sockaddr *)address, address_len);
    } while (n < 0 && errno == EINTR);
    return n == (ssize_t)len ? 0 : -1;
}

static int dns_exchange(routing_exec_t *st, int *tcp_fd,
                        const uint8_t *query, size_t query_len,
                        uint8_t *response, size_t cap, size_t *response_len) {
    uint8_t length[2];
    if (*tcp_fd < 0) {
        *tcp_fd = socks5_connect_to_dns(st->socks_port, st->dns_upstream);
        if (*tcp_fd < 0) return -1;
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(*tcp_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(*tcp_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    }
    length[0] = (uint8_t)(query_len >> 8);
    length[1] = (uint8_t)query_len;
    if (fd_write_all(*tcp_fd, length, sizeof length) != 0 ||
        fd_write_all(*tcp_fd, query, query_len) != 0 ||
        fd_read_all(*tcp_fd, length, sizeof length) != 0)
        goto failed;
    size_t len = (size_t)(((uint16_t)length[0] << 8) | length[1]);
    if (len == 0 || len > cap || fd_read_all(*tcp_fd, response, len) != 0)
        goto failed;
    *response_len = len;
    return 0;
failed:
    close(*tcp_fd);
    *tcp_fd = -1;
    return -1;
}

static void dns_log_rule(routing_exec_t *st, size_t index,
                         const dns_question_t *question, rule_action_t action) {
    if (!st || !question || index >= RULESET_MAX_RULES) return;
    size_t byte = index / 8;
    uint8_t bit = (uint8_t)(1u << (index % 8));
    if ((st->rule_logged[byte] & bit) != 0) return;
    st->rule_logged[byte] |= bit;
    fprintf(stderr, "senkod: rule %s matched %s\n",
            rule_action_name(action), question->name);
}

static void pf_apply_changes(routing_exec_t *st,
                             const uint32_t *added, size_t added_count,
                             const uint32_t *deleted, size_t deleted_count) {
    if (deleted_count && pf_table_batch(st, "delete", deleted, deleted_count) != 0) {
        pf_table_mark_installed(&g_pf_table, deleted, deleted_count, 1);
        fprintf(stderr, "senkod: pf bypass batch delete failed\n");
    }
    if (added_count && pf_table_batch(st, "add", added, added_count) != 0) {
        pf_table_mark_installed(&g_pf_table, added, added_count, 0);
        fprintf(stderr, "senkod: pf bypass batch add failed\n");
    }
}

static void dns_update_pf(routing_exec_t *st, const dns_question_t *question,
                          rule_action_t action, const dns_response_info_t *info,
                          uint64_t now) {
    pf_table_changes_t changes;
    if (!st->pf_table_ready || !info || info->ipv4_count == 0 ||
        (action != RULE_ACTION_DIRECT && action != RULE_ACTION_PROXY))
        return;
    if (pf_table_record(&g_pf_table, question->name, action,
                        info->ipv4, info->ipv4_count, now, info->min_ttl,
                        &changes) != PF_TABLE_OK) {
        fprintf(stderr, "senkod: pf bypass shadow is full\n");
        return;
    }
    pf_apply_changes(st, changes.added, changes.added_count,
                     changes.deleted, changes.deleted_count);
}

static void dns_cleanup_pf(routing_exec_t *st, uint64_t now) {
    size_t added_count = 0;
    size_t deleted_count = 0;
    if (!st->pf_table_ready) return;
    if (pf_table_cleanup(&g_pf_table, now,
                         g_pf_cleanup_added, PF_TABLE_MAX_ADDRS, &added_count,
                         g_pf_cleanup_deleted, PF_TABLE_MAX_ADDRS, &deleted_count) != PF_TABLE_OK) {
        fprintf(stderr, "senkod: pf bypass cleanup overflowed\n");
        return;
    }
    pf_apply_changes(st, g_pf_cleanup_added, added_count,
                     g_pf_cleanup_deleted, deleted_count);
}

static int dns_questions_match(const dns_question_t *query,
                               const dns_question_t *response) {
    return query->type == response->type &&
           query->class_code == response->class_code &&
           strcmp(query->name, response->name) == 0;
}

static void *dns_forwarder_thread(void *arg) {
    routing_exec_t *st = (routing_exec_t *)arg;
    if (!st || !st->dns_bound) return NULL;
    int udp_fd = st->dns_fd;
    int tcp_fd = -1;
    uint64_t next_cleanup = monotonic_seconds() + 10;
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

    uint8_t query[DNS_CACHE_RESPONSE_MAX];
    uint8_t response[DNS_CACHE_RESPONSE_MAX];
    while (!st->dns_stop) {
        uint64_t now = monotonic_seconds();
        if (st->flush_dns_requested) {
            st->flush_dns_requested = 0;
            dns_cache_clear(&g_dns_cache);
            fprintf(stderr, "senkod: dns cache flushed on request\n");
        }
        if (st->flush_bypass_requested) {
            st->flush_bypass_requested = 0;
/* withdraw the addresses from pf before forgetting them, or the shadow and the
   live table disagree until every ttl runs out */
            size_t installed = pf_table_installed_addresses(
                &g_pf_table, g_pf_cleanup_deleted, PF_TABLE_MAX_ADDRS);
            if (installed)
                (void)pf_table_batch(st, "delete", g_pf_cleanup_deleted, installed);
            pf_table_clear(&g_pf_table);
            fprintf(stderr, "senkod: bypass table flushed on request (%zu address(es))\n",
                    installed);
        }
        if (now >= next_cleanup) {
            dns_cleanup_pf(st, now);
            next_cleanup = now + 10;
        }

        struct sockaddr_in client_address;
        socklen_t client_len = sizeof client_address;
        ssize_t query_len = recvfrom(udp_fd, query, sizeof query, 0,
                                     (struct sockaddr *)&client_address, &client_len);
        if (query_len <= 0) {
            if (query_len < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                errno != EINTR)
                fprintf(stderr, "senkod: DNS proxy recvfrom error: %s\n",
                        strerror(errno));
            continue;
        }

        dns_question_t question;
        if (dns_msg_parse_question(query, (size_t)query_len, &question) != DNS_MSG_OK) {
            fprintf(stderr, "senkod: DNS proxy rejected malformed query\n");
            continue;
        }
        size_t matched = SIZE_MAX;
        rule_action_t action = st->rules
            ? ruleset_match_domain(st->rules, question.name, &matched)
            : RULE_ACTION_PROXY;
        if (matched != SIZE_MAX) dns_log_rule(st, matched, &question, action);

        size_t response_len = 0;
        if (action == RULE_ACTION_BLOCK) {
            if (dns_msg_build_block(query, (size_t)query_len, st->block_response,
                                    response, sizeof response, &response_len) == DNS_MSG_OK)
                (void)dns_sendto(udp_fd, response, response_len,
                                 &client_address, client_len);
            continue;
        }

        rule_action_t cached_action;
        int stale = 0;
        dns_cache_status_t cached = dns_cache_get(
            &g_dns_cache, question.name, question.type, now, 0,
            response, sizeof response, &response_len, &cached_action, &stale);
        if (cached == DNS_CACHE_OK && cached_action == action) {
            response[0] = query[0];
            response[1] = query[1];
            (void)dns_sendto(udp_fd, response, response_len,
                             &client_address, client_len);
            continue;
        }

        int success = 0;
        for (int retries = 2; retries > 0 && !st->dns_stop; --retries) {
            if (dns_exchange(st, &tcp_fd, query, (size_t)query_len,
                             response, sizeof response, &response_len) != 0)
                continue;
            dns_question_t response_question;
            dns_response_info_t info;
            if (response_len < 2 || response[0] != query[0] || response[1] != query[1] ||
                dns_msg_parse_response_question(response, response_len,
                                                &response_question) != DNS_MSG_OK ||
                !dns_questions_match(&question, &response_question) ||
                dns_msg_response_info(response, response_len, &info) != DNS_MSG_OK ||
                dns_msg_clamp_ttls(response, response_len,
                                   DNS_CACHE_TTL_MIN, DNS_CACHE_TTL_MAX) != DNS_MSG_OK ||
                dns_msg_response_info(response, response_len, &info) != DNS_MSG_OK) {
                fprintf(stderr, "senkod: DNS proxy rejected malformed response\n");
                close(tcp_fd);
                tcp_fd = -1;
                continue;
            }
            now = monotonic_seconds();
            dns_update_pf(st, &question, action, &info, now);
            if (info.min_ttl > 0)
                (void)dns_cache_put(&g_dns_cache, question.name, question.type,
                                    response, response_len, now, info.min_ttl, action);
            (void)dns_sendto(udp_fd, response, response_len,
                             &client_address, client_len);
            success = 1;
            break;
        }

        if (!success) {
            cached = dns_cache_get(&g_dns_cache, question.name, question.type,
                                   monotonic_seconds(), 1, response,
                                   sizeof response, &response_len,
                                   &cached_action, &stale);
            if (cached == DNS_CACHE_OK && cached_action == action) {
                response[0] = query[0];
                response[1] = query[1];
                (void)dns_sendto(udp_fd, response, response_len,
                                 &client_address, client_len);
            } else {
                fprintf(stderr, "senkod: DNS proxy failed to forward query\n");
            }
        }
    }

    if (tcp_fd >= 0) close(tcp_fd);
    return NULL;
}

static int start_dns_forwarder(routing_exec_t *st) {
    if (st->dns_thread) return 0;
    if (!st->dns_bound) return -1;
    st->dns_stop = 0;
    if (pthread_create(&st->dns_thread, NULL, dns_forwarder_thread, st) != 0)
        return -1;
    return 0;
}

static void stop_dns_forwarder(routing_exec_t *st) {
    if (st->dns_thread) {
        st->dns_stop = 1;
        pthread_join(st->dns_thread, NULL);
        st->dns_thread = 0;
    }
    close_dns_socket(st);
}

rexec_status_t routing_exec_up(routing_exec_t *st, int socks_port,
                               const char *server_ip, const char *server_ips,
                               const char *dns_upstream, int dns_local_port,
                               dns_block_response_t block_response,
                               ruleset_t *rules, int force_pf_mode) {
    if (!st || !server_ip || !server_ips || !dns_upstream || dns_local_port <= 0)
        return REXEC_ERR_ARG;
    if (st->mode != ROUTING_MODE_NONE || st->dns_thread) {
        routing_exec_down(st);
    }
    memset(st, 0, sizeof *st);
    dns_cache_init(&g_dns_cache);
    pf_table_init(&g_pf_table);
    st->dns_fd = -1;
    st->socks_port = socks_port;
    st->rules = rules;
    st->block_response = block_response;
    int dns_end = dns_local_port + 100;
    if (dns_end > 65535) dns_end = 65535;
    int dns_port = routing_pick_free_udp_port(dns_local_port, dns_end);
    if (dns_port <= 0) dns_port = routing_pick_free_udp_port(0, 0);
    if (dns_port <= 0) return REXEC_ERR_PORT;
    int dns_fd = bind_dns_socket(dns_port);
    if (dns_fd < 0) return REXEC_ERR_PORT;
    st->dns_local_port = dns_port;
    st->dns_fd = dns_fd;
    st->dns_bound = 1;
    if (dns_port != dns_local_port)
        fprintf(stderr, "senkod: dns port %d busy, using %d\n",
                dns_local_port, dns_port);
    snprintf(st->dns_upstream, sizeof st->dns_upstream, "%s", dns_upstream);
    snprintf(st->server_ip, sizeof st->server_ip, "%s", server_ip);
    snprintf(st->server_ips, sizeof st->server_ips, "%s", server_ips);

    /* both backends redirect into Senko's transparent listener */
    int redir = routing_pick_free_port(0, 0);
    if (redir <= 0) {
        fprintf(stderr, "senkod: no free redirect port\n");
        routing_exec_down(st);
        return REXEC_ERR_PORT;
    }

    const char *pfctl = routing_find_pfctl();
    if (pfctl) {
        char ifnames[ROUTING_MAX_IFS][32];
        size_t if_count = collect_ifaces(ifnames, ROUTING_MAX_IFS);
        if (if_count == 0)
            fprintf(stderr, "senkod: pfctl found but no IPv4 en*/pdp_ip* interface\n");
        if (if_count > 0) {
            fprintf(stderr, "senkod: pf trying %zu interface(s):", if_count);
            for (size_t i = 0; i < if_count; ++i) fprintf(stderr, " %s", ifnames[i]);
            fprintf(stderr, "\n");
            char pf_detail[192];
            int last_pf_mode = -1;
/* a pinned variant runs alone: a tester who cannot stop the ladder after the
   first rejection cannot tell whether the rung they care about works */
            int first_mode = 0;
            int mode_count = ROUTING_PF_MODE_COUNT;
            if (force_pf_mode >= 0 && force_pf_mode < ROUTING_PF_MODE_COUNT) {
                first_mode = force_pf_mode;
                mode_count = force_pf_mode + 1;
                fprintf(stderr, "senkod: pf mode pinned to %s\n",
                        routing_pf_mode_name((routing_pf_mode_t)force_pf_mode));
            }
            for (int m = first_mode; m < mode_count; ++m) {
                clear_pf();
                last_pf_mode = m;
                int applied = apply_pf_mode(pfctl, server_ips, rules, ifnames, if_count,
                                            redir, st->dns_local_port,
                                            (routing_pf_mode_t)m,
                                            pf_detail, sizeof pf_detail);
                if (applied != 0)
                    snprintf(st->pf_last_error, sizeof st->pf_last_error, "%s: %s",
                             routing_pf_mode_name((routing_pf_mode_t)m),
                             pf_detail[0] ? pf_detail : "rejected");
                if (applied == 0) {
                    st->mode = ROUTING_MODE_PF;
                    st->redir_port = redir;
                    st->pf_table_ready = m != ROUTING_PF_COMPAT_RDR;
                    st->pf_mode = (routing_pf_mode_t)m;
                    st->pf_mode_valid = 1;
                    st->pf_rejected = m - first_mode;
                    fprintf(stderr, "senkod: pf mode %s accepted after %d rejected\n",
                            routing_pf_mode_name((routing_pf_mode_t)m),
                            st->pf_rejected);
                    if (start_dns_forwarder(st) != 0) {
                        routing_exec_down(st);
                        return REXEC_ERR_SPAWN;
                    }
                    return REXEC_OK;
                }
            }
            fprintf(stderr, "senkod: %s pf rule mode(s) rejected (last mode %d: %s)\n",
                    mode_count - first_mode == 1 ? "the pinned" : "all",
                    last_pf_mode, pf_detail[0] ? pf_detail : "unknown pfctl error");
            clear_pf();
        }
    } else {
        fprintf(stderr, "senkod: pfctl not found\n");
    }

    const char *ipfw = routing_find_ipfw();
    if (ipfw) {
        if (apply_ipfw(ipfw, server_ip, server_ips, redir, socks_port,
                       st->dns_local_port) == 0) {
            st->mode = ROUTING_MODE_IPFW;
            st->redir_port = redir;
            if (routing_scopedroute_disable(st->scoped_route_prev,
                                            sizeof st->scoped_route_prev) != 0)
                fprintf(stderr, "senkod: scopedroute tweak failed, "
                                "ipfw fwd may not reach the listener\n");
            if (start_dns_forwarder(st) != 0) {
                routing_exec_down(st);
                return REXEC_ERR_SPAWN;
            }
            return REXEC_OK;
        }
        fprintf(stderr, "senkod: ipfw rules rejected\n");
        clear_ipfw();
    } else {
        fprintf(stderr, "senkod: ipfw not found in known paths or PATH\n");
    }

    fprintf(stderr, "senkod: no routing backend (need pfctl+ifaces or ipfw); "
            "full-device will not redirect\n");
    routing_exec_down(st);
    return REXEC_ERR_NO_BACKEND;
}

void routing_exec_down(routing_exec_t *st) {
    if (!st) return;
    stop_dns_forwarder(st);
    routing_scopedroute_restore(st->scoped_route_prev);
    if (st->mode == ROUTING_MODE_IPFW) clear_ipfw();
    if (st->mode == ROUTING_MODE_PF)   clear_pf();
    if (st->mode == ROUTING_MODE_PF) unlink(PF_CONF);
    memset(st, 0, sizeof *st);
}

void routing_exec_dns_stats(uint64_t *hits, uint64_t *misses,
                            uint64_t *stale_hits, size_t *entries) {
    if (hits) *hits = g_dns_cache.hits;
    if (misses) *misses = g_dns_cache.misses;
    if (stale_hits) *stale_hits = g_dns_cache.stale_hits;
    if (entries) *entries = dns_cache_entry_count(&g_dns_cache);
}

void routing_exec_bypass_stats(pf_table_counts_t *counts,
                               uint64_t *evicted_addresses,
                               uint64_t *evicted_refs) {
    if (counts) pf_table_counts(&g_pf_table, counts);
    if (evicted_addresses) *evicted_addresses = g_pf_table.evicted_addresses;
    if (evicted_refs) *evicted_refs = g_pf_table.evicted_refs;
}

void routing_exec_flush_dns(routing_exec_t *st) {
    if (st && st->dns_thread) {
        st->flush_dns_requested = 1;
        return;
    }
    dns_cache_clear(&g_dns_cache);
}

void routing_exec_flush_bypass(routing_exec_t *st) {
    if (st && st->dns_thread) {
        st->flush_bypass_requested = 1;
        return;
    }
    pf_table_clear(&g_pf_table);
}

/* pf gets its ruleset as a file, so the file pfctl loaded is the honest answer.
   ipfw takes its rules as argument vectors, so those are rebuilt from the same
   writer the backend used */
int routing_exec_render(const routing_exec_t *st, char *buf, size_t cap,
                        size_t *len) {
    if (len) *len = 0;
    if (!st || !buf || cap == 0) return -1;
    buf[0] = '\0';

    if (st->mode == ROUTING_MODE_PF) {
        int fd = open(PF_CONF, O_RDONLY | O_NOFOLLOW);
        if (fd < 0) return -1;
        size_t off = 0;
        while (off + 1 < cap) {
            ssize_t got = read(fd, buf + off, cap - 1 - off);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) break;
            off += (size_t)got;
        }
        close(fd);
        buf[off] = '\0';
        if (len) *len = off;
        return off > 0 ? 0 : -1;
    }

    if (st->mode == ROUTING_MODE_IPFW) {
        routing_ipfw_rule_t rules[ROUTING_MAX_RULES];
        size_t count = 0;
        if (routing_ipfw_rules(st->server_ip, st->redir_port, st->socks_port,
                               st->dns_local_port, rules, ROUTING_MAX_RULES,
                               &count) != ROUTING_OK)
            return -1;
        size_t off = 0;
        for (size_t i = 0; i < count; ++i) {
            int n = snprintf(buf + off, cap - off, "ipfw %s\n", rules[i].rule_out);
            if (n < 0 || (size_t)n >= cap - off) break;
            off += (size_t)n;
        }
        if (len) *len = off;
        return off > 0 ? 0 : -1;
    }

    return -1;
}

void routing_exec_bypass_add_ipv4(routing_exec_t *st, const char *ip) {
    if (!st || !ip || !ip[0] || st->mode == ROUTING_MODE_NONE) return;

    if (st->mode == ROUTING_MODE_PF && st->pf_table_ready) {
        const char *pfctl = routing_find_pfctl();
        if (!pfctl) return;
        char addr[64];
        snprintf(addr, sizeof addr, "%s/32", ip);
        char *argv[] = { (char *)pfctl, (char *)"-t", (char *)"senko_bypass",
                         (char *)"-T", (char *)"add", addr, NULL };
        (void)run_spawn_quiet(pfctl, argv);
        return;
    }

    if (st->mode == ROUTING_MODE_IPFW) {
        const char *ipfw = routing_find_ipfw();
        if (!ipfw || !st->server_ip[0]) return;
        if (net_ip_list_contains(st->server_ips, ip)) return;
        size_t len = strlen(st->server_ips);
        size_t iplen = strlen(ip);
        if (len + iplen + 2 >= sizeof st->server_ips) return;
        if (len > 0) {
            st->server_ips[len++] = ',';
            st->server_ips[len] = '\0';
        }
        memcpy(st->server_ips + len, ip, iplen + 1);
        /* an unnumbered rule lands after the catch-all fwd, where it can never
           match, and outside the range clear_ipfw sweeps on teardown */
        char rule[256];
        snprintf(rule, sizeof rule, "add %d allow tcp from any to %s out",
                 ROUTING_IPFW_BASE + 1, ip);
        char *argv[36];
        int argc = split_rule_words(rule, argv, 36);
        if (argc <= 0) return;
        char *full[38];
        int n = 0;
        full[n++] = (char *)ipfw;
        full[n++] = (char *)"-q";
        for (int i = 0; i < argc && n < 37; ++i) full[n++] = argv[i];
        full[n] = NULL;
        (void)routing_spawn(ipfw, full);
    }
}

void routing_exec_clear_stale(void) {
    /* clear both backends because a crash may leave either one active */
    clear_ipfw();
    clear_pf();
    unlink(PF_CONF);
}
