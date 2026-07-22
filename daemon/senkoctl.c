#define _DEFAULT_SOURCE

#include "core/b64.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_SOCK "/var/tmp/senkod.sock"

/* keep reading streamed catalog and fetch records until a terminal line */
static int reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        if (llen > 0) {
            const char *ln = buf + start;
            int stream =
                (llen >= 4 && memcmp(ln, "SRV ", 4) == 0) ||
                (llen >= 4 && memcmp(ln, "SUB ", 4) == 0) ||
                (llen >= 8 && memcmp(ln, "SUBMETA ", 8) == 0) ||
                (llen >= 8 && memcmp(ln, "SECTION ", 8) == 0) ||
                (llen >= 6 && memcmp(ln, "FDATA ", 6) == 0);
            if (!stream) return 1;
        }
        start = i + 1;
    }
    return 0;
}

/* wait for terminal state so connect does not report success before verify */
static int tunnel_reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    int terminal = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        if (llen >= 4 && memcmp(buf + start, "ERR ", 4) == 0)
            return 1;
        if (llen >= 6 && memcmp(buf + start, "STATE ", 6) == 0) {
            const char *st = buf + start + 6;
            size_t slen = llen - 6;
            if (slen >= 10 && memcmp(st, "connecting", 10) == 0) {
/* keep reading */
            } else if ((slen >= 9 && memcmp(st, "connected", 9) == 0) ||
                       (slen >= 5 && memcmp(st, "error", 5) == 0) ||
                       (slen >= 4 && memcmp(st, "idle", 4) == 0)) {
                terminal = 1;
            }
        }
        start = i + 1;
    }
    return terminal;
}

static int connect_sock(const char *sock, int *out_fd) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, sock, sizeof a.sun_path - 1);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    *out_fd = fd;
    return 0;
}

static ssize_t write_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return (ssize_t)len;
}

static void token_path_from_sock(const char *sock, char *out, size_t cap) {
    size_t n = strlen(sock);
    if (n >= 5 && strcmp(sock + n - 5, ".sock") == 0 && n - 5 + 6 < cap) {
        memcpy(out, sock, n - 5);
        memcpy(out + n - 5, ".token", 7);
        return;
    }
    snprintf(out, cap, "%s.token", sock);
}

static int load_token(const char *sock, char *token, size_t cap) {
    char path[160];
    token_path_from_sock(sock, path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(token, (int)cap, f)) { fclose(f); return -1; }
    fclose(f);
    size_t n = strlen(token);
    while (n > 0 && (token[n - 1] == '\n' || token[n - 1] == '\r'))
        token[--n] = '\0';
    return n > 0 ? 0 : -1;
}

/* without AUTH any local process can mutate the tunnel on a jailbreak */
static int ctl_auth(int fd, const char *sock) {
    char token[48];
    if (load_token(sock, token, sizeof token) != 0) return 0;
    char line[80];
    int ln = snprintf(line, sizeof line, "AUTH %s\n", token);
    if (ln <= 0 || (size_t)ln >= sizeof line) return -1;
    if (write_all(fd, line, (size_t)ln) < 0) return -1;
    struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[128];
    size_t tot = 0;
    while (tot + 1 < sizeof buf) {
        ssize_t r = read(fd, buf + tot, sizeof buf - 1 - tot);
        if (r > 0) {
            tot += (size_t)r;
            buf[tot] = '\0';
            if (memchr(buf, '\n', tot)) break;
            continue;
        }
        break;
    }
    return (tot >= 3 && memcmp(buf, "OK ", 3) == 0) ? 0 : -1;
}

static ssize_t read_line(int fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        ssize_t r = read(fd, buf + n, 1);
        if (r == 1) {
            if (buf[n] == '\n') {
                buf[n + 1] = '\0';
                return (ssize_t)(n + 1);
            }
            n++;
            continue;
        }
        if (r == 0) break;
        if (errno == EINTR) continue;
        return -1;
    }
    return -1;
}

/* bound control reads so dead daemons and failover cannot hang the cli */
static ssize_t talk_ex(const char *sock, const char *line, size_t line_len,
                       char *buf, size_t cap, int timeout_sec,
                       int (*done)(const char *, size_t)) {
    int fd;
    if (connect_sock(sock, &fd) != 0) return -1;

/* status stays open for boot probes; mutators still need the token */
    int is_status = (line_len >= 6 && memcmp(line, "STATUS", 6) == 0);
    if (!is_status && ctl_auth(fd, sock) != 0) {
        close(fd);
        return -1;
    }

    if (write_all(fd, line, line_len) < 0) { close(fd); return -1; }

    if (!done) done = reply_complete;
    if (timeout_sec <= 0) timeout_sec = 2;
    struct timeval tv; tv.tv_sec = timeout_sec; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    size_t tot = 0;
    for (;;) {
        if (tot >= cap - 1) break;
        ssize_t r = read(fd, buf + tot, cap - 1 - tot);
        if (r > 0) {
            tot += (size_t)r;
            if (done(buf, tot)) break;
            continue;
        }
        if (r == 0) break;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        break;
    }
    close(fd);
    buf[tot] = '\0';
    return (ssize_t)tot;
}

static int run_fetch(const char *sock, const char *url) {
    int fd;
    if (connect_sock(sock, &fd) != 0) return 2;
    if (ctl_auth(fd, sock) != 0) { close(fd); return 2; }

    char line[1200];
    int ln = snprintf(line, sizeof line, "FETCH %s\n", url);
    if (ln <= 0 || (size_t)ln >= sizeof line) { close(fd); return 2; }
    if (write_all(fd, line, (size_t)ln) < 0) { close(fd); return 2; }

    struct timeval tv; tv.tv_sec = 30; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    char reply[4096];
    for (;;) {
        ssize_t n = read_line(fd, reply, sizeof reply);
        if (n <= 0) { close(fd); return 2; }

        if (strncmp(reply, "ERR ", 4) == 0) {
            fputs(reply, stderr);
            close(fd);
            return 1;
        }
        if (strncmp(reply, "FDATA ", 6) == 0) {
            const char *b64 = reply + 6;
            size_t blen = strlen(b64);
            while (blen > 0 && (b64[blen - 1] == '\n' || b64[blen - 1] == '\r'))
                blen--;
            unsigned char raw[512];
            size_t rawlen = 0;
            if (b64_decode(b64, blen, raw, sizeof raw, &rawlen) != 0) {
                close(fd);
                return 2;
            }
            if (rawlen > 0 && fwrite(raw, 1, rawlen, stdout) != rawlen) {
                close(fd);
                return 2;
            }
            continue;
        }
        if (strncmp(reply, "FDEND ", 6) == 0) {
            close(fd);
            return 0;
        }

        fprintf(stderr, "senkoctl: unexpected fetch reply: %s", reply);
        close(fd);
        return 2;
    }
}

static int join_args(char **argv, int start, int argc, char *dst, size_t cap) {
    size_t o = 0;
    for (int j = start; j < argc; ++j) {
        size_t need = strlen(argv[j]) + (j > start ? 1 : 0);
        if (o + need >= cap) return -1;
        if (j > start) dst[o++] = ' ';
        memcpy(dst + o, argv[j], strlen(argv[j]));
        o += strlen(argv[j]);
    }
    dst[o] = '\0';
    return 0;
}

static void usage(const char *a0) {
    fprintf(stderr,
        "usage: %s [-s sock] <command>\n"
        "  status | list | disconnect\n"
        "  connect <idx> | ping <idx> | refresh <sub-idx> | del <idx>\n"
        "  fetch <url> | addsrv <link> | addsub <url> <name...> | raw <verb...>\n"
        "sock defaults to $SENKOD_SOCK or " DEFAULT_SOCK "\n", a0);
}

int main(int argc, char **argv) {
    const char *sock = getenv("SENKOD_SOCK");
    if (!sock || !sock[0]) sock = DEFAULT_SOCK;

    int i = 1;
    if (i < argc && strcmp(argv[i], "-s") == 0) {
        if (i + 1 >= argc) { usage(argv[0]); return 2; }
        sock = argv[i + 1];
        i += 2;
    }
    if (i >= argc) { usage(argv[0]); return 2; }

    const char *cmd = argv[i++];
    char line[1200];

    if (strcmp(cmd, "fetch") == 0) {
        if (i >= argc) { usage(argv[0]); return 2; }
        char url[1100];
        if (join_args(argv, i, argc, url, sizeof url) != 0) {
            fprintf(stderr, "senkoctl: arguments too long\n");
            return 2;
        }
        return run_fetch(sock, url);
    }

    if (strcmp(cmd, "status") == 0) {
        snprintf(line, sizeof line, "STATUS\n");
    } else if (strcmp(cmd, "list") == 0) {
        snprintf(line, sizeof line, "LIST\n");
    } else if (strcmp(cmd, "disconnect") == 0) {
        snprintf(line, sizeof line, "DISCONNECT\n");
    } else if (strcmp(cmd, "connect") == 0) {
        if (i >= argc) { usage(argv[0]); return 2; }
        snprintf(line, sizeof line, "CONNECT %d\n", atoi(argv[i]));
    } else if (strcmp(cmd, "ping") == 0) {
        if (i >= argc) { usage(argv[0]); return 2; }
        snprintf(line, sizeof line, "PING %d\n", atoi(argv[i]));
    } else if (strcmp(cmd, "refresh") == 0) {
        if (i >= argc) { usage(argv[0]); return 2; }
        snprintf(line, sizeof line, "REFRESH %d\n", atoi(argv[i]));
    } else if (strcmp(cmd, "del") == 0) {
        if (i >= argc) { usage(argv[0]); return 2; }
        snprintf(line, sizeof line, "DELSRV %d\n", atoi(argv[i]));
    } else if (strcmp(cmd, "addsrv") == 0) {
        if (i >= argc) { usage(argv[0]); return 2; }
        snprintf(line, sizeof line, "ADDSRV %s\n", argv[i]);
    } else if (strcmp(cmd, "addsub") == 0) {
        if (i + 1 >= argc) { usage(argv[0]); return 2; }
        char rest[1100];
        if (join_args(argv, i, argc, rest, sizeof rest) != 0) {
            fprintf(stderr, "senkoctl: arguments too long\n"); return 2;
        }
        snprintf(line, sizeof line, "ADDSUB %s\n", rest);
    } else if (strcmp(cmd, "raw") == 0) {
        if (i >= argc) { usage(argv[0]); return 2; }
        char rest[1100];
        if (join_args(argv, i, argc, rest, sizeof rest) != 0) {
            fprintf(stderr, "senkoctl: arguments too long\n"); return 2;
        }
        snprintf(line, sizeof line, "%s\n", rest);
    } else {
        usage(argv[0]);
        return 2;
    }

    char buf[65536];
    int is_tunnel = (strcmp(cmd, "connect") == 0 || strcmp(cmd, "disconnect") == 0);
    int is_ping = (strcmp(cmd, "ping") == 0);
/* leave timeout headroom for failover verification and two ping samples */
    int timeout_sec = is_tunnel ? 60 : (is_ping ? 8 : 5);
    int (*done)(const char *, size_t) =
        is_tunnel ? tunnel_reply_complete : reply_complete;
    ssize_t n = talk_ex(sock, line, strlen(line), buf, sizeof buf,
                        timeout_sec, done);
    if (n < 0) {
        fprintf(stderr, "senkoctl: cannot reach daemon at %s (%s)\n",
                sock, strerror(errno));
        return 2;
    }
    fputs(buf, stdout);
    if (n > 0 && buf[n - 1] != '\n') fputc('\n', stdout);

    for (size_t p = 0; p < (size_t)n; ) {
        if (strncmp(buf + p, "ERR ", 4) == 0 || strncmp(buf + p, "ERR\n", 4) == 0)
            return 1;
        if (strncmp(buf + p, "STATE error", 11) == 0)
            return 1;
        char *nl = strchr(buf + p, '\n');
        if (!nl) break;
        p = (size_t)(nl - buf) + 1;
    }
    return 0;
}
