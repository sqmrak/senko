/* restore daemon after a failed launchd start */
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define PLIST  "/Library/LaunchDaemons/com.senko.senkod.plist"
#define SOCK   "/var/tmp/senkod.sock"
#define BIN    "/usr/bin/senkod"
#define CFG    "/var/root/Library/Preferences/senko.cfg"
#define KLOG   "/var/log/senko-kick.log"
#define LABEL  "com.senko.senkod"
#define AWG_BIN "/usr/bin/senkoawgd"
#define CTL_BIN "/usr/bin/senkoctl"
#define AWG_PID "/var/run/senkoawgd.pid"
#define AWG_LOG "/var/log/senkoawgd.log"
#define AWG_STATUS "/var/run/senkoawgd.status"
#define AWG_ACTIVE_CONFIG "/var/run/senkoawgd.config"
#define AWG_CONFIG_DIR "/var/mobile/Library/Preferences/Senko/"
#define VPN_ICON_STATE "/var/mobile/Library/Preferences/com.senko.vpnicon.state"
/* /tmp is readable by the mobile ui */
#define UPDATE_LOG "/tmp/senko-update.log"
#define UPDATE_MAX_BYTES (64 * 1024 * 1024)
#define UPDATE_AWG_MARKER "/var/run/senkoawgd.upgrade"
#define KICK_LOCK "/var/tmp/senko-kick.lock"

static int acquire_kick_lock(void) {
    int fd = open(KICK_LOCK, O_WRONLY | O_CREAT, 0600);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void klog(const char *msg) {
    int fd = open(KLOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    dprintf(fd, "senko-kick: %s\n", msg);
    close(fd);
    /* keep a copy on stderr when launched from a console */
    fprintf(stderr, "senko-kick: %s\n", msg);
}

static int run_argv(char *const argv[]) {
    pid_t pid = 0;
    int rc = posix_spawn(&pid, argv[0], NULL, NULL, argv, environ);
    if (rc != 0) return -1;
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
}

static int run_capture_text(char *const argv[], char *out, size_t cap) {
    if (!argv || !argv[0] || !out || cap == 0) return -1;
    out[0] = '\0';
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    posix_spawn_file_actions_t fa;
    if (posix_spawn_file_actions_init(&fa) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    int action_rc = posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
    if (action_rc == 0)
        action_rc = posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null",
                                                      O_WRONLY, 0);
    if (action_rc == 0)
        action_rc = posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    if (action_rc == 0)
        action_rc = posix_spawn_file_actions_addclose(&fa, pipefd[1]);
    pid_t pid = 0;
    int spawn_rc = action_rc == 0
        ? posix_spawn(&pid, argv[0], &fa, NULL, argv, environ)
        : -1;
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[1]);
    if (spawn_rc != 0) {
        close(pipefd[0]);
        return -1;
    }

    size_t used = 0;
    int truncated = 0;
    for (;;) {
        char buf[256];
        ssize_t n = read(pipefd[0], buf, sizeof buf);
        if (n > 0) {
            size_t keep = (size_t)n;
            size_t room = used < cap - 1 ? cap - 1 - used : 0;
            if (keep > room) {
                keep = room;
                truncated = 1;
            }
            if (keep > 0) {
                memcpy(out + used, buf, keep);
                used += keep;
                out[used] = '\0';
            }
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(status)) return -1;
    if (truncated) return -1;
    out[strcspn(out, "\r\n")] = '\0';
    return WEXITSTATUS(status);
}

static int run_logged(char *const argv[]) {
    posix_spawn_file_actions_t fa;
    if (posix_spawn_file_actions_init(&fa) != 0) return -1;
    int action_rc = posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, UPDATE_LOG,
                                                      O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (action_rc == 0)
        action_rc = posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, UPDATE_LOG,
                                                      O_WRONLY | O_CREAT | O_APPEND, 0644);
    pid_t pid = 0;
    int spawn_rc = action_rc == 0
        ? posix_spawn(&pid, argv[0], &fa, NULL, argv, environ)
        : -1;
    posix_spawn_file_actions_destroy(&fa);
    if (spawn_rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int find_bin(const char *const *cands, char *out, size_t cap) {
    for (int i = 0; cands[i]; ++i) {
        if (access(cands[i], X_OK) == 0) {
            snprintf(out, cap, "%s", cands[i]);
            return 0;
        }
    }
    return -1;
}

/* accept mobile and temporary paths, including their /private aliases */
static int update_path_ok(const char *path) {
    if (!path || !path[0] || strstr(path, "..") != NULL) return 0;
    int under = 0;
    if (strncmp(path, "/var/mobile/", 12) == 0) under = 1;
    else if (strncmp(path, "/private/var/mobile/", 20) == 0) under = 1;
    else if (strncmp(path, "/tmp/", 5) == 0) under = 1;
    else if (strncmp(path, "/private/tmp/", 13) == 0) under = 1;
    else if (strncmp(path, "/var/tmp/", 9) == 0) under = 1;
    else if (strncmp(path, "/private/var/tmp/", 17) == 0) under = 1;
    if (!under) return 0;
    size_t len = strlen(path);
    if (len < 5) return 0;
    /* accept .deb paths in any case */
    const char *ext = path + len - 4;
    if (!((ext[0] == '.' ) &&
          (ext[1] == 'd' || ext[1] == 'D') &&
          (ext[2] == 'e' || ext[2] == 'E') &&
          (ext[3] == 'b' || ext[3] == 'B')))
        return 0;
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    return st.st_size > 0 && st.st_size <= UPDATE_MAX_BYTES;
}

/* copy to a private temporary file */
static int update_stage_copy(const char *src, char *dst, size_t dstcap) {
    if (!src || !dst || dstcap < 40) return -1;
    char tmpl[] = "/tmp/senko-update-XXXXXX";
    int out = mkstemp(tmpl);
    if (out < 0) return -1;
    int in = open(src, O_RDONLY);
    if (in < 0) {
        close(out);
        unlink(tmpl);
        return -1;
    }
    char buf[8192];
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(in);
            close(out);
            unlink(tmpl);
            return -1;
        }
        if (n == 0) break;
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                close(in);
                close(out);
                unlink(tmpl);
                return -1;
            }
            off += w;
        }
    }
    close(in);
    (void)fchmod(out, 0644);
    if (fsync(out) != 0) {
        /* old jailbreak filesystems may not support fsync */
    }
    close(out);
    if (strlen(tmpl) + 1 > dstcap) {
        unlink(tmpl);
        return -1;
    }
    memcpy(dst, tmpl, strlen(tmpl) + 1);
    return 0;
}

/* a live status reply proves that the listener is more than just a socket file */
static int sock_alive(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK, sizeof addr.sun_path - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return 0;
    }
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    const char *req = "STATUS\n";
    if (write(fd, req, 7) != 7) {
        close(fd);
        return 0;
    }
    char buf[96];
    size_t tot = 0;
    while (tot + 1 < sizeof buf) {
        ssize_t n = read(fd, buf + tot, sizeof buf - 1 - tot);
        if (n > 0) {
            tot += (size_t)n;
            buf[tot] = '\0';
            if (memchr(buf, '\n', tot)) break;
            continue;
        }
        break;
    }
    close(fd);
    return tot >= 6 && memcmp(buf, "STATE ", 6) == 0;
}

static int wait_sock(int tenths) {
    for (int i = 0; i < tenths; ++i) {
        if (sock_alive()) return 0;
        usleep(100000);
    }
    return -1;
}

static int wait_sock_down(int tenths) {
    for (int i = 0; i < tenths; ++i) {
        if (!sock_alive()) return 0;
        usleep(100000);
    }
    return sock_alive() ? -1 : 0;
}

/* start senkod without launchd */
static int spawn_senkod_direct(void) {
    char *argv[] = {
        (char *)BIN,
        (char *)"--managed",
        (char *)"--ctl", (char *)SOCK,
        (char *)"--config", (char *)CFG,
        (char *)"--full-device",
        NULL
    };

    posix_spawn_file_actions_t fa;
    int action_rc = posix_spawn_file_actions_init(&fa);
    if (action_rc != 0) {
        char msg[128];
        snprintf(msg, sizeof msg, "direct spawn setup failed (%d: %s)",
                 action_rc, strerror(action_rc));
        klog(msg);
        return -1;
    }
    /* keep daemon logs where the ui expects them */
    action_rc = posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/var/log/senkod.log",
                                                  O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (action_rc == 0)
        action_rc = posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/var/log/senkod.log",
                                                      O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (action_rc != 0) {
        char msg[128];
        snprintf(msg, sizeof msg, "direct spawn log setup failed (%d: %s)",
                 action_rc, strerror(action_rc));
        posix_spawn_file_actions_destroy(&fa);
        klog(msg);
        return -1;
    }

    pid_t pid = 0;
    int rc = posix_spawn(&pid, BIN, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {
        char msg[128];
        snprintf(msg, sizeof msg, "direct spawn failed (%d: %s)",
                 rc, strerror(rc));
        klog(msg);
        return -1;
    }

    return 0;
}

static void kill_senkod(void) {
    static const char *kills[] = { "/usr/bin/killall", "/bin/killall", NULL };
    char killbin[64];
    if (find_bin(kills, killbin, sizeof killbin) != 0) return;
    char *argv[] = { killbin, (char *)"-9", (char *)"senkod", NULL };
    (void)run_argv(argv);
}

static int senkod_alive(void) {
    static const char *kills[] = { "/usr/bin/killall", "/bin/killall", NULL };
    char killbin[64], output[32];
    if (find_bin(kills, killbin, sizeof killbin) != 0) return 0;
    char *argv[] = { killbin, (char *)"-0", (char *)"senkod", NULL };
    return run_capture_text(argv, output, sizeof output) == 0;
}

static int wait_senkod_down(int tenths) {
    for (int i = 0; i < tenths; ++i) {
        if (!senkod_alive()) return 0;
        usleep(100000);
    }
    return senkod_alive() ? -1 : 0;
}

static int launch_job_loaded(const char *launchctl) {
    char output[256];
    char *argv[] = { (char *)launchctl, (char *)"list", (char *)LABEL, NULL };
    return run_capture_text(argv, output, sizeof output) == 0;
}

static int launch_job_stop(const char *launchctl) {
    char *unload[] = { (char *)launchctl, (char *)"unload", (char *)PLIST, NULL };
    char *remove[] = { (char *)launchctl, (char *)"remove", (char *)LABEL, NULL };
    (void)run_argv(unload);
    (void)run_argv(remove);
    if (launch_job_loaded(launchctl)) return -1;
    kill_senkod();
    (void)wait_senkod_down(30);
    (void)wait_sock_down(30);
    unlink(SOCK);
    return 0;
}

static int kill_named(const char *name, int signal_number) {
    static const char *kills[] = { "/usr/bin/killall", "/bin/killall", NULL };
    char killbin[64];
    if (!name || find_bin(kills, killbin, sizeof killbin) != 0) return -1;
    char signal_text[16];
    snprintf(signal_text, sizeof signal_text, "-%d", signal_number);
    char *argv[] = { killbin, signal_text, (char *)name, NULL };
    return run_argv(argv);
}

static int awg_path_ok(const char *path) {
    if (!path || strncmp(path, AWG_CONFIG_DIR, strlen(AWG_CONFIG_DIR)) != 0) return 0;
    size_t n = strlen(path);
    return n > strlen(AWG_CONFIG_DIR) + 5 && strcmp(path + n - 5, ".conf") == 0 &&
           strstr(path, "..") == NULL;
}

static int awg_read_pid(pid_t *out) {
    FILE *f = fopen(AWG_PID, "r");
    long value = 0;
    int ok = f && fscanf(f, "%ld", &value) == 1 && value > 1;
    if (f) fclose(f);
    if (!ok) return -1;
    *out = (pid_t)value;
    return 0;
}

static int awg_running(void) {
    pid_t pid;
    return awg_read_pid(&pid) == 0 && kill(pid, 0) == 0;
}

static void awg_write_active_config(const char *path) {
    int fd = open(AWG_ACTIVE_CONFIG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    dprintf(fd, "%s\n", path ? path : "");
    close(fd);
}

static void awg_write_status(const char *text) {
    int fd = open(AWG_STATUS, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    dprintf(fd, "%s\n", text);
    close(fd);
}

static int awg_read_status(char *out, size_t cap) {
    if (!out || cap < 2) return -1;
    FILE *f = fopen(AWG_STATUS, "r");
    if (!f) return -1;
    if (!fgets(out, (int)cap, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    out[strcspn(out, "\r\n")] = '\0';
    return out[0] ? 0 : -1;
}

static void awg_write_icon(int enabled) {
    int fd = open(VPN_ICON_STATE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    (void)write(fd, enabled ? "1\n" : "0\n", 2);
    close(fd);
}

static int awg_stop(void) {
    pid_t pid;
    char state[160];
    if (awg_read_status(state, sizeof state) == 0 && strncmp(state, "error", 5) == 0) {
        if (awg_read_pid(&pid) == 0) {
            (void)kill(pid, SIGTERM);
            usleep(200000);
            (void)kill(pid, SIGKILL);
        }
        unlink(AWG_PID);
        unlink(AWG_ACTIVE_CONFIG);
        awg_write_status("idle");
        awg_write_icon(0);
        fputs("idle\n", stdout);
        return 0;
    }
    if (awg_read_pid(&pid) != 0 || kill(pid, 0) != 0) {
        (void)kill_named("senkoawgd", SIGTERM);
        unlink(AWG_PID);
        unlink(AWG_ACTIVE_CONFIG);
        awg_write_status("idle");
        awg_write_icon(0);
        fputs("idle\n", stdout);
        return 0;
    }
    if (kill(pid, SIGTERM) != 0) {
        (void)kill_named("senkoawgd", SIGTERM);
        unlink(AWG_PID);
        unlink(AWG_ACTIVE_CONFIG);
        awg_write_status("idle");
        awg_write_icon(0);
        fputs("idle\n", stdout);
        return 0;
    }
    for (int i = 0; i < 50 && kill(pid, 0) == 0; ++i) usleep(100000);
    if (kill(pid, 0) == 0) {
        (void)kill_named("senkoawgd", SIGTERM);
        for (int i = 0; i < 20 && kill(pid, 0) == 0; ++i) usleep(100000);
    }
    if (kill(pid, SIGKILL) == 0 || errno == ESRCH) {
        unlink(AWG_PID);
        unlink(AWG_ACTIVE_CONFIG);
        awg_write_status("idle");
        awg_write_icon(0);
        fputs("idle\n", stdout);
        return 0;
    }
    return -1;
}

static int awg_start(const char *config) {
    if (!awg_path_ok(config) || access(config, R_OK) != 0 || access(AWG_BIN, X_OK) != 0) return -1;
    if (awg_running()) {
        fputs("connecting\n", stdout);
        return 0;
    }
    if (access(CTL_BIN, X_OK) == 0) {
        char *disconnect[] = { (char *)CTL_BIN, (char *)"disconnect", NULL };
        if (run_argv(disconnect) != 0) {
            awg_write_status("error vless still active");
            return -1;
        }
    }
    unlink(AWG_PID);
    awg_write_icon(0);
    awg_write_status("connecting");
    char *argv[] = { (char *)AWG_BIN, (char *)"--run", (char *)config, NULL };
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, AWG_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, AWG_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, AWG_BIN, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) { awg_write_status("error spawn failed"); return -1; }
    int fd = open(AWG_PID, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { kill(pid, SIGTERM); return -1; }
    dprintf(fd, "%ld\n", (long)pid);
    close(fd);
    awg_write_active_config(config);
    fputs("connecting\n", stdout);
    return 0;
}

static int awg_status(void) {
    if (awg_running()) {
        FILE *f = fopen(AWG_STATUS, "r");
        char line[160] = "connecting";
        if (f) { (void)fgets(line, sizeof line, f); fclose(f); }
        fputs(line, stdout);
        return 0;
    }
    unlink(AWG_PID);
    unlink(AWG_ACTIVE_CONFIG);
    awg_write_icon(0);
    awg_write_status("idle");
    fputs("idle\n", stdout);
    return 0;
}

static void save_awg_upgrade_state(void) {
    if (!awg_running()) return;
    FILE *f = fopen(AWG_ACTIVE_CONFIG, "r");
    if (!f) return;
    char path[512];
    int ok = fgets(path, sizeof path, f) != NULL;
    fclose(f);
    if (!ok) return;
    path[strcspn(path, "\r\n")] = '\0';
    if (!awg_path_ok(path)) return;
    int fd = open(UPDATE_AWG_MARKER, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        dprintf(fd, "%s\n", path);
        close(fd);
    }
}

static void restore_awg_upgrade_state(void) {
    FILE *f = fopen(UPDATE_AWG_MARKER, "r");
    if (!f) return;
    char path[512];
    int ok = fgets(path, sizeof path, f) != NULL;
    fclose(f);
    unlink(UPDATE_AWG_MARKER);
    if (!ok) return;
    path[strcspn(path, "\r\n")] = '\0';
    if (!awg_path_ok(path) || access(path, R_OK) != 0) return;
    char *argv[] = { (char *)"/usr/bin/senko-kick", (char *)"--awg", path, NULL };
    (void)run_argv(argv);
}

static void stop_senkod_for_update(void) {
    char launchctl[64];
    static const char *launchctl_paths[] = {
        "/bin/launchctl", "/usr/bin/launchctl", "/sbin/launchctl",
        "/usr/sbin/launchctl", NULL
    };
    if (find_bin(launchctl_paths, launchctl, sizeof launchctl) == 0) {
        char *unload[] = { launchctl, (char *)"unload", (char *)PLIST, NULL };
        (void)run_argv(unload);
        char *remove[] = { launchctl, (char *)"remove", (char *)LABEL, NULL };
        (void)run_argv(remove);
    }
    (void)kill_named("senkod", SIGTERM);
    usleep(700000);
    kill_senkod();
    (void)wait_senkod_down(30);
    (void)wait_sock_down(30);
    unlink(SOCK);
}

static int restart_senkod_after_update(void) {
    char *argv[] = { (char *)"/usr/bin/senko-kick", NULL };
    if (run_argv(argv) != 0) return -1;
    return wait_sock(40);
}

static void update_stage(const char *name) {
    /* flush progress lines immediately */
    printf("UPDATE STAGE %s\n", name);
    fflush(stdout);
}

static int update_package(const char *path) {
    setvbuf(stdout, NULL, _IONBF, 0);
    /* keep SpringBoard alive until dpkg exits */
    setenv("SENKO_SKIP_RESPRING", "1", 1);

    if (!update_path_ok(path)) {
        fputs("UPDATE ERR invalid package path\n", stdout);
        return 1;
    }
    if (access(path, R_OK) != 0) {
        fputs("UPDATE ERR package not readable\n", stdout);
        return 1;
    }

    update_stage("checking package");

    /* copy the package before installing it */
    char staged[96];
    staged[0] = '\0';
    if (update_stage_copy(path, staged, sizeof staged) != 0) {
        fputs("UPDATE ERR cannot stage package\n", stdout);
        return 1;
    }
    const char *pkg_path = staged;

    char dpkg_deb[64];
    static const char *dpkg_deb_paths[] = {
        "/usr/bin/dpkg-deb", "/bin/dpkg-deb", "/sbin/dpkg-deb", NULL
    };
    if (find_bin(dpkg_deb_paths, dpkg_deb, sizeof dpkg_deb) != 0) {
        unlink(staged);
        fputs("UPDATE ERR dpkg-deb missing\n", stdout);
        return 1;
    }

    char package[128];
    char *field_argv[] = { dpkg_deb, (char *)"-f", (char *)pkg_path,
                           (char *)"Package", NULL };
    if (run_capture_text(field_argv, package, sizeof package) != 0 ||
        strcmp(package, "com.senko.daemon") != 0) {
        unlink(staged);
        fputs("UPDATE ERR package is not Senko\n", stdout);
        return 1;
    }

    char version[128];
    char *version_argv[] = { dpkg_deb, (char *)"-f", (char *)pkg_path,
                             (char *)"Version", NULL };
    if (run_capture_text(version_argv, version, sizeof version) != 0 || !version[0]) {
        unlink(staged);
        fputs("UPDATE ERR package version missing\n", stdout);
        return 1;
    }

    char arch[128];
    char *arch_argv[] = { dpkg_deb, (char *)"-f", (char *)pkg_path,
                          (char *)"Architecture", NULL };
    if (run_capture_text(arch_argv, arch, sizeof arch) != 0 ||
        (strcmp(arch, "iphoneos-arm") != 0 &&
         strcmp(arch, "iphoneos-arm64") != 0 &&
         strcmp(arch, "all") != 0)) {
        unlink(staged);
        fputs("UPDATE ERR package architecture mismatch\n", stdout);
        return 1;
    }
    printf("UPDATE META version %s\n", version);
    fflush(stdout);

    save_awg_upgrade_state();
    update_stage("stopping daemon");
    stop_senkod_for_update();

    char dpkg[64];
    static const char *dpkg_paths[] = {
        "/usr/bin/dpkg", "/bin/dpkg", "/sbin/dpkg", NULL
    };
    if (find_bin(dpkg_paths, dpkg, sizeof dpkg) != 0) {
        unlink(staged);
        restore_awg_upgrade_state();
        (void)restart_senkod_after_update();
        fputs("UPDATE ERR dpkg missing\n", stdout);
        return 1;
    }

    update_stage("installing");
    char *install_argv[] = { dpkg, (char *)"--install", (char *)pkg_path, NULL };
    int rc = run_logged(install_argv);
    unlink(staged);
    update_stage("starting daemon");
    int daemon_rc = restart_senkod_after_update();
    if (access(UPDATE_AWG_MARKER, F_OK) == 0)
        restore_awg_upgrade_state();
    if (rc != 0) {
        printf("UPDATE ERR dpkg exit %d\n", rc);
        return 1;
    }
    if (daemon_rc != 0) {
        fputs("UPDATE ERR daemon did not start\n", stdout);
        return 1;
    }
    update_stage("done");
    printf("UPDATE OK %s\n", version);
    return 0;
}

static long elapsed_ms(const struct timeval *start, const struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000L + (end->tv_usec - start->tv_usec) / 1000L;
}

static int awg_probe(const char *config) {
    if (!awg_path_ok(config) || access(config, R_OK) != 0 || access(AWG_BIN, X_OK) != 0) return -1;
    struct timeval start, end;
    gettimeofday(&start, NULL);
    char *argv[] = { (char *)AWG_BIN, (char *)"--handshake", (char *)config, (char *)"5000", NULL };
    int rc = run_argv(argv);
    gettimeofday(&end, NULL);
    if (rc != 0) return -1;
    printf("PING %ld\n", elapsed_ms(&start, &end));
    return 0;
}

static int awg_validate(const char *config) {
    if (!awg_path_ok(config) || access(config, R_OK) != 0 || access(AWG_BIN, X_OK) != 0)
        return -1;
    char *argv[] = { (char *)AWG_BIN, (char *)"--validate", (char *)config, NULL };
    return run_argv(argv);
}

int main(int argc, char **argv) {
    if (geteuid() != 0) {
        if (setuid(0) != 0) {
            klog("need root (setuid bit / reinstall deb as root)");
            return 1;
        }
    }
    setuid(0);
    setgid(0);

    int kick_lock = acquire_kick_lock();
    if (kick_lock < 0) {
        klog("could not lock daemon startup");
        return 1;
    }
    (void)kick_lock;

    if (argc == 2 && strcmp(argv[1], "--awg-stop") == 0) {
        int rc = awg_stop();
        if (rc != 0) fputs("error amneziawg stop timeout\n", stdout);
        return rc == 0 ? 0 : 1;
    }
    if (argc == 2 && strcmp(argv[1], "--awg-status") == 0) return awg_status();
    if (argc == 3 && strcmp(argv[1], "--awg") == 0) return awg_start(argv[2]) == 0 ? 0 : 1;
    if (argc == 3 && strcmp(argv[1], "--awg-validate") == 0)
        return awg_validate(argv[2]) == 0 ? 0 : 1;
    if (argc == 3 && strcmp(argv[1], "--awg-probe") == 0) return awg_probe(argv[2]) == 0 ? 0 : 1;
    if (argc == 3 && strcmp(argv[1], "--update") == 0) return update_package(argv[2]);

    if (sock_alive()) {
        klog("already up");
        return 0;
    }

    if (wait_sock(60) == 0) {
        klog("already up via launchctl");
        return 0;
    }

    if (access(BIN, X_OK) != 0) {
        klog("/usr/bin/senkod missing");
        return 2;
    }

    char lc[64];
    static const char *lcs[] = {
        "/bin/launchctl", "/usr/bin/launchctl",
        "/sbin/launchctl", "/usr/sbin/launchctl", NULL
    };
    int have_lc = (find_bin(lcs, lc, sizeof lc) == 0);

    if (have_lc && access(PLIST, R_OK) == 0) {
        if (!launch_job_loaded(lc)) {
            char *load[] = { lc, (char *)"load", (char *)PLIST, NULL };
            if (run_argv(load) != 0) {
                char *loadw[] = { lc, (char *)"load", (char *)"-w", (char *)PLIST, NULL };
                (void)run_argv(loadw);
            }
        }
        char *start[] = { lc, (char *)"start", (char *)LABEL, NULL };
        (void)run_argv(start);

        if (wait_sock(180) == 0) {
            klog("up via launchctl");
            return 0;
        }
        if (launch_job_stop(lc) != 0) {
            klog("launchd job still loaded; refusing direct spawn");
            return 5;
        }
        klog("launchd handoff complete, trying direct spawn");
    } else {
        klog("no launchctl/plist, direct spawn");
    }

    kill_senkod();
    (void)wait_senkod_down(60);
    (void)wait_sock_down(60);
    unlink(SOCK);
    if (spawn_senkod_direct() != 0)
        return 4;

    if (wait_sock(120) == 0) {
        klog("up via direct spawn");
        return 0;
    }

    klog("senkod did not open sock");
    return 5;
}
