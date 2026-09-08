#define _DEFAULT_SOURCE

#include "routing_ios5.h"
#include "core/net_safe.h"

#include <errno.h>
#include <fcntl.h>
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

#define IOS5_RULE_MIN 12000
#define IOS5_RULE_MAX 12098

static int can_exec(const char *path) {
    return path && access(path, X_OK) == 0;
}

static const char *find_ipfw(void) {
    static const char *p[] = { "/sbin/ipfw", "/usr/sbin/ipfw", "/bin/ipfw",
                               "/usr/bin/ipfw" };
    for (size_t i = 0; i < sizeof p / sizeof p[0]; ++i)
        if (can_exec(p[i])) return p[i];
    return NULL;
}

static const char *find_sysctl(void) {
    static const char *p[] = { "/sbin/sysctl", "/usr/sbin/sysctl",
                               "/usr/bin/sysctl" };
    for (size_t i = 0; i < sizeof p / sizeof p[0]; ++i)
        if (can_exec(p[i])) return p[i];
    return NULL;
}

static int run_spawn(const char *bin, char *const argv[]) {
    pid_t pid = 0;
    int rc = posix_spawn(&pid, bin, NULL, NULL, argv, environ);
    if (rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int ipfw_del(int number) {
    const char *ipfw = find_ipfw();
    if (!ipfw) return -1;
    char num[16];
    snprintf(num, sizeof num, "%d", number);
    char *argv[] = { (char *)ipfw, (char *)"-q", (char *)"delete", num, NULL };
    return run_spawn(ipfw, argv);
}

static int ipfw_add(const char *body) {
    const char *ipfw = find_ipfw();
    if (!ipfw) return -1;
    char rule[384];
    int n = snprintf(rule, sizeof rule, "%s", body);
    if (n < 0 || (size_t)n >= sizeof rule) return -1;
    char *save = NULL;
    char *words[40];
    int nw = 0;
    for (char *tok = strtok_r(rule, " ", &save); tok && nw < 39;
         tok = strtok_r(NULL, " ", &save))
        words[nw++] = tok;
    if (nw < 2) return -1;
    char *argv[44];
    int an = 0;
    argv[an++] = (char *)ipfw;
    argv[an++] = (char *)"-q";
    argv[an++] = (char *)"add";
    for (int i = 0; i < nw && an < 42; ++i) argv[an++] = words[i];
    argv[an] = NULL;
    return run_spawn(ipfw, argv);
}

void routing_ios5_clear_rules(void) {
    for (int n = IOS5_RULE_MAX; n >= IOS5_RULE_MIN; --n)
        (void)ipfw_del(n);
}

static int add_bypasses(routing_ios5_t *st) {
    char rule[256];
    if (snprintf(rule, sizeof rule,
                 "%d allow tcp from any to 127.0.0.1", IOS5_RULE_MIN) >= (int)sizeof rule)
        return -1;
    if (ipfw_add(rule) != 0) return -1;

    if (snprintf(rule, sizeof rule,
                 "%d allow tcp from any to 10.0.0.0/8", IOS5_RULE_MIN + 1) >= (int)sizeof rule)
        return -1;
    if (ipfw_add(rule) != 0) return -1;
    if (snprintf(rule, sizeof rule,
                 "%d allow tcp from any to 172.16.0.0/12", IOS5_RULE_MIN + 2) >= (int)sizeof rule)
        return -1;
    if (ipfw_add(rule) != 0) return -1;
    if (snprintf(rule, sizeof rule,
                 "%d allow tcp from any to 192.168.0.0/16", IOS5_RULE_MIN + 3) >= (int)sizeof rule)
        return -1;
    if (ipfw_add(rule) != 0) return -1;

    char ips[4096];
    snprintf(ips, sizeof ips, "%s", st->server_ips);
    int slot = IOS5_RULE_MIN + 4;
    char *save = NULL;
    for (char *tok = strtok_r(ips, ", ", &save); tok && slot <= IOS5_RULE_MAX - 2;
         tok = strtok_r(NULL, ", ", &save)) {
        char ip[64];
        if (!net_ipv4_literal(tok, ip, sizeof ip)) continue;
        if (snprintf(rule, sizeof rule,
                     "%d allow tcp from any to %s", slot, ip) >= (int)sizeof rule)
            return -1;
        if (ipfw_add(rule) != 0) return -1;
        slot++;
    }
    st->next_bypass_slot = slot;
    return 0;
}

static int pick_free_port(void) {
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

static int save_scoped_route(routing_ios5_t *st) {
    const char *sysctl = find_sysctl();
    if (!sysctl) return -1;
    char *argv[] = { (char *)sysctl, (char *)"-n",
                     (char *)"net.inet.ip.scopedroute", NULL };
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
    char buf[32] = {0};
    ssize_t n = read(fds[0], buf, sizeof buf - 1);
    close(fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (n <= 0) return -1;
    buf[strcspn(buf, "\r\n ")] = '\0';
    snprintf(st->scoped_route_prev, sizeof st->scoped_route_prev, "%s", buf);
    st->scoped_route_saved = 1;

    char val[32];
    snprintf(val, sizeof val, "net.inet.ip.scopedroute=0");
    char *setargv[] = { (char *)sysctl, (char *)"-w", val, NULL };
    return run_spawn(sysctl, setargv);
}

static void restore_scoped_route(routing_ios5_t *st) {
    if (!st->scoped_route_saved) return;
    const char *sysctl = find_sysctl();
    if (!sysctl) return;
    char val[48];
    snprintf(val, sizeof val, "net.inet.ip.scopedroute=%s", st->scoped_route_prev);
    char *argv[] = { (char *)sysctl, (char *)"-w", val, NULL };
    (void)run_spawn(sysctl, argv);
    st->scoped_route_saved = 0;
}

int routing_ios5_up(routing_ios5_t *st, int socks_port,
                    const char *server_ip, const char *server_ips) {
    if (!st || !server_ip || !server_ips) return -1;
    memset(st, 0, sizeof *st);
    if (!find_ipfw()) {
        fprintf(stderr, "senkod: ios5 backend: ipfw not found\n");
        return -1;
    }

    int redir = pick_free_port();
    if (redir <= 0) return -1;

    routing_ios5_clear_rules();

    st->socks_port = socks_port;
    snprintf(st->server_ips, sizeof st->server_ips, "%s", server_ips);
    if (!strstr(st->server_ips, server_ip)) {
        size_t len = strlen(st->server_ips);
        size_t iplen = strlen(server_ip);
        if (len && len + 1 + iplen + 1 < sizeof st->server_ips) {
            st->server_ips[len++] = ',';
            memcpy(st->server_ips + len, server_ip, iplen + 1);
        }
    }

    if (add_bypasses(st) != 0) {
        fprintf(stderr, "senkod: ios5 backend: bypass rules rejected\n");
        routing_ios5_clear_rules();
        return -1;
    }

    char rule[256];
    if (snprintf(rule, sizeof rule,
                 "%d check-state", IOS5_RULE_MAX - 1) < (int)sizeof rule &&
        ipfw_add(rule) != 0) {
        routing_ios5_clear_rules();
        return -1;
    }

    /* dynamic states retain fwd action for replies without re-forwarding them */
    if (snprintf(rule, sizeof rule,
                 "%d fwd 127.0.0.1,%d tcp from any to any out setup keep-state",
                 IOS5_RULE_MAX, redir) >= (int)sizeof rule ||
        ipfw_add(rule) != 0) {
        fprintf(stderr, "senkod: ios5 backend: fwd rule rejected\n");
        routing_ios5_clear_rules();
        return -1;
    }

    if (save_scoped_route(st) != 0)
        fprintf(stderr, "senkod: ios5 backend: scopedroute tweak failed\n");

    st->redir_port = redir;
    st->active = 1;
    fprintf(stderr, "senkod: ios5 ipfw backend on 127.0.0.1:%d\n", redir);
    return 0;
}

void routing_ios5_down(routing_ios5_t *st) {
    if (!st || !st->active) return;
    routing_ios5_clear_rules();
    restore_scoped_route(st);
    st->active = 0;
}

void routing_ios5_bypass_add_ipv4(routing_ios5_t *st, const char *ip) {
    if (!st || !st->active || !ip || !ip[0]) return;
    if (strstr(st->server_ips, ip) != NULL) return;
    if (st->next_bypass_slot > IOS5_RULE_MAX - 2) return;
    size_t len = strlen(st->server_ips);
    size_t iplen = strlen(ip);
    if (len + iplen + 2 >= sizeof st->server_ips) return;
    if (len > 0) {
        st->server_ips[len++] = ',';
        st->server_ips[len] = '\0';
    }
    memcpy(st->server_ips + len, ip, iplen + 1);
    char rule[256];
    if (snprintf(rule, sizeof rule,
                 "%d allow tcp from any to %s", st->next_bypass_slot, ip) >= (int)sizeof rule)
        return;
    if (ipfw_add(rule) == 0)
        st->next_bypass_slot++;
}
