#define _DEFAULT_SOURCE

#include "routing_exec.h"
#include "core/net_safe.h"

#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <spawn.h>

extern char **environ;

#define PF_CONF "/var/run/senko-pf.conf"
#define PF_ERR  "/var/tmp/senko-pf.err"
#define PF_OS   "/etc/pf.os"

/* check common jailbreak paths */

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

static const char *find_ipfw(void) {
    static const char *p[] = { "/sbin/ipfw", "/usr/sbin/ipfw", "/bin/ipfw",
                               "/usr/bin/ipfw", "/usr/local/sbin/ipfw",
                               "/usr/local/bin/ipfw" };
    const char *found = find_first(p, sizeof p / sizeof p[0]);
    if (found) return found;
    static char path[256];
    return find_path_command("ipfw", path, sizeof path);
}
static const char *find_pfctl(void) {
    static const char *p[] = { "/sbin/pfctl", "/usr/sbin/pfctl", "/bin/pfctl",
                               "/usr/bin/pfctl", "/usr/local/sbin/pfctl",
                               "/usr/local/bin/pfctl" };
    const char *found = find_first(p, sizeof p / sizeof p[0]);
    if (found) return found;
    static char path[256];
    return find_path_command("pfctl", path, sizeof path);
}
static const char *find_sysctl(void) {
    static const char *p[] = { "/sbin/sysctl", "/usr/sbin/sysctl", "/usr/bin/sysctl" };
    return find_first(p, sizeof p / sizeof p[0]);
}

static int run_spawn(const char *bin, char *const argv[]) {
    pid_t pid = 0;
    int rc = posix_spawn(&pid, bin, NULL, NULL, argv, environ);
    if (rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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
    return run_spawn(ipfw, argv);
}

static int write_file(const char *path, const char *buf, size_t len) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    size_t w = fwrite(buf, 1, len, fp);
    fclose(fp);
    return (w == len) ? 0 : -1;
}

/* probe ports before generating routing rules */

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

static void clear_ipfw(void) {
    const char *ipfw = find_ipfw();
    if (!ipfw) return;
    char num[16];
    char del[] = "delete";
    for (int n = 12030; n >= 12000; --n) {
        snprintf(num, sizeof num, "%d", n);
        char *argv[] = { (char *)ipfw, (char *)"-q", del, num, NULL };
        run_spawn(ipfw, argv);
    }
}

static int apply_ipfw(const char *ipfw, const char *server_ip,
                      const char *server_ips,
                      int redir_port, int socks_port, int dns_local_port) {
    const char *sysctl = find_sysctl();
    if (sysctl) {
        char *argv[] = { (char *)sysctl, (char *)"-w",
                         (char *)"net.inet.ip.fw.enable=1", NULL };
        run_spawn(sysctl, argv);
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
    const char *pfctl = find_pfctl();
    if (!pfctl) return;
    char *argv[] = { (char *)pfctl, (char *)"-q", (char *)"-F", (char *)"all", NULL };
    run_spawn_quiet(pfctl, argv);
}

static int apply_pf_mode(const char *pfctl, const char *server_ips,
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
        run_spawn(sysctl, argv);
    }

    char conf[8192]; size_t clen = 0;
    if (routing_pf_conf(server_ips, ifnames, if_count, redir_port, dns_local_port,
                        mode, conf, sizeof conf, &clen) != ROUTING_OK)
        return -1;
    if (write_file(PF_CONF, conf, clen) != 0) return -1;

    char *enargv[] = { (char *)pfctl, (char *)"-q", (char *)"-e", NULL };
    int enable_rc = run_spawn_quiet(pfctl, enargv);
    if (enable_rc != 0) {
        char *compat_enargv[] = { (char *)pfctl, (char *)"-q", (char *)"-E", NULL };
        run_spawn_quiet(pfctl, compat_enargv);
    }
    char *lfargv[] = { (char *)pfctl, (char *)"-q", (char *)"-f", (char *)PF_CONF, NULL };
    int rc = run_spawn_pf_capture(pfctl, lfargv);
    if (rc != 0 && detail && detail_cap > 0) {
        read_pf_error(detail, detail_cap);
        if (!detail[0]) snprintf(detail, detail_cap, "pfctl exit %d", rc);
    } else {
        unlink(PF_ERR);
    }
    return rc == 0 ? 0 : -1;
}

/* forward DNS over TCP through SOCKS */

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
    if (write(fd, greet, 3) != 3) { close(fd); return -1; }

    uint8_t resp[10];
    if (read(fd, resp, 2) != 2 || resp[0] != 0x05 || resp[1] != 0x00) {
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
    if (write(fd, req, 10) != 10) { close(fd); return -1; }

    if (read(fd, resp, 10) != 10 || resp[0] != 0x05 || resp[1] != 0x00) {
        close(fd);
        return -1;
    }

    return fd;
}

static void *dns_forwarder_thread(void *arg) {
    routing_exec_t *st = (routing_exec_t *)arg;
    if (!st || !st->dns_bound) return NULL;
    int udp_fd = st->dns_fd;

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    uint8_t buf[2048];
    int tcp_fd = -1;

    while (!st->dns_stop) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof client_addr;
        ssize_t n = recvfrom(udp_fd, buf, sizeof buf, 0, (struct sockaddr *)&client_addr, &client_len);
        if (n <= 0) {
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "senkod: DNS proxy recvfrom error: %s\n", strerror(errno));
                fflush(stderr);
            }
            continue;
        }

        int retries = 2;
        int success = 0;
        while (retries > 0 && !st->dns_stop) {
            if (tcp_fd < 0) {
                tcp_fd = socks5_connect_to_dns(st->socks_port, st->dns_upstream);
                if (tcp_fd < 0) {
                    fprintf(stderr, "senkod: DNS proxy SOCKS5 connect to %s:53 failed\n",
                            st->dns_upstream);
                    fflush(stderr);
                    retries--;
                    continue;
                }
                struct timeval tcp_tv;
                tcp_tv.tv_sec = 5; /* 5s idle timeout */
                tcp_tv.tv_usec = 0;
                setsockopt(tcp_fd, SOL_SOCKET, SO_RCVTIMEO, &tcp_tv, sizeof tcp_tv);
                setsockopt(tcp_fd, SOL_SOCKET, SO_SNDTIMEO, &tcp_tv, sizeof tcp_tv);
            }

            uint8_t len_hdr[2];
            len_hdr[0] = (uint8_t)((n >> 8) & 0xFF);
            len_hdr[1] = (uint8_t)(n & 0xFF);

            if (write(tcp_fd, len_hdr, 2) != 2 || write(tcp_fd, buf, n) != n) {
                fprintf(stderr, "senkod: DNS proxy TCP write failed, reconnecting: %s\n", strerror(errno));
                fflush(stderr);
                close(tcp_fd);
                tcp_fd = -1;
                retries--;
                continue;
            }

            if (read(tcp_fd, len_hdr, 2) != 2) {
                fprintf(stderr, "senkod: DNS proxy TCP read len failed, reconnecting: %s\n", strerror(errno));
                fflush(stderr);
                close(tcp_fd);
                tcp_fd = -1;
                retries--;
                continue;
            }

            uint16_t resp_len = (uint16_t)((len_hdr[0] << 8) | len_hdr[1]);
            if (resp_len > sizeof buf) {
                fprintf(stderr, "senkod: DNS proxy TCP response too large: %d\n", resp_len);
                fflush(stderr);
                close(tcp_fd);
                tcp_fd = -1;
                break;
            }

            ssize_t read_bytes = 0;
            int read_ok = 1;
            while (read_bytes < resp_len) {
                ssize_t r = read(tcp_fd, buf + read_bytes, resp_len - read_bytes);
                if (r <= 0) {
                    fprintf(stderr, "senkod: DNS proxy TCP read response failed: %s\n", strerror(errno));
                    fflush(stderr);
                    read_ok = 0;
                    break;
                }
                read_bytes += r;
            }

            if (!read_ok) {
                close(tcp_fd);
                tcp_fd = -1;
                retries--;
                continue;
            }

            sendto(udp_fd, buf, resp_len, 0, (struct sockaddr *)&client_addr, client_len);
            success = 1;
            break;
        }

        if (!success) {
            fprintf(stderr, "senkod: DNS proxy failed to forward query\n");
            fflush(stderr);
        }
    }

    if (tcp_fd >= 0) {
        close(tcp_fd);
    }
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
                               const char *dns_upstream, int dns_local_port) {
    if (!st || !server_ip || !server_ips || !dns_upstream || dns_local_port <= 0)
        return REXEC_ERR_ARG;
    if (st->mode != ROUTING_MODE_NONE || st->dns_thread) {
        routing_exec_down(st);
    }
    memset(st, 0, sizeof *st);
    st->dns_fd = -1;
    st->socks_port = socks_port;
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

    const char *pfctl = find_pfctl();
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
            for (int m = 0; m < ROUTING_PF_MODE_COUNT; ++m) {
                clear_pf();
                last_pf_mode = m;
                if (apply_pf_mode(pfctl, server_ips, ifnames, if_count,
                                  redir, st->dns_local_port,
                                  (routing_pf_mode_t)m,
                                  pf_detail, sizeof pf_detail) == 0) {
                    st->mode = ROUTING_MODE_PF;
                    st->redir_port = redir;
                    st->use_internal_tproxy = 1;
                    st->pf_table_ready = m != ROUTING_PF_COMPAT_RDR;
                    if (start_dns_forwarder(st) != 0) {
                        routing_exec_down(st);
                        return REXEC_ERR_SPAWN;
                    }
                    return REXEC_OK;
                }
            }
            fprintf(stderr, "senkod: all pf rule modes were rejected (last mode %d: %s)\n",
                    last_pf_mode, pf_detail[0] ? pf_detail : "unknown pfctl error");
            clear_pf();
        }
    } else {
        fprintf(stderr, "senkod: pfctl not found\n");
    }

    const char *ipfw = find_ipfw();
    if (ipfw) {
        if (apply_ipfw(ipfw, server_ip, server_ips, redir, socks_port,
                       st->dns_local_port) == 0) {
            st->mode = ROUTING_MODE_IPFW;
            st->redir_port = redir;
            st->use_internal_tproxy = 1;
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
    if (st->mode == ROUTING_MODE_IPFW) clear_ipfw();
    if (st->mode == ROUTING_MODE_PF)   clear_pf();
    if (st->mode == ROUTING_MODE_PF) unlink(PF_CONF);
    memset(st, 0, sizeof *st);
}

void routing_exec_bypass_add_ipv4(routing_exec_t *st, const char *ip) {
    if (!st || !ip || !ip[0] || st->mode == ROUTING_MODE_NONE) return;

    if (st->mode == ROUTING_MODE_PF && st->pf_table_ready) {
        const char *pfctl = find_pfctl();
        if (!pfctl) return;
        char addr[64];
        snprintf(addr, sizeof addr, "%s/32", ip);
        char *argv[] = { (char *)pfctl, (char *)"-t", (char *)"senko_bypass",
                         (char *)"-T", (char *)"add", addr, NULL };
        (void)run_spawn_quiet(pfctl, argv);
        return;
    }

    if (st->mode == ROUTING_MODE_IPFW) {
        const char *ipfw = find_ipfw();
        if (!ipfw || !st->server_ip[0]) return;
        if (strstr(st->server_ips, ip) != NULL) return;
        size_t len = strlen(st->server_ips);
        size_t iplen = strlen(ip);
        if (len + iplen + 2 >= sizeof st->server_ips) return;
        if (len > 0) {
            st->server_ips[len++] = ',';
            st->server_ips[len] = '\0';
        }
        memcpy(st->server_ips + len, ip, iplen + 1);
        char rule[256];
        snprintf(rule, sizeof rule, "allow tcp from me to %s out", ip);
        char *argv[36];
        int argc = split_rule_words(rule, argv, 36);
        if (argc <= 0) return;
        char *full[37];
        int n = 0;
        full[n++] = (char *)ipfw;
        full[n++] = (char *)"add";
        for (int i = 0; i < argc && n < 36; ++i) full[n++] = argv[i];
        full[n] = NULL;
        (void)run_spawn(ipfw, full);
    }
}

void routing_exec_clear_stale(void) {
    /* clear both backends because a crash may leave either one active */
    clear_ipfw();
    clear_pf();
    unlink(PF_CONF);
}
