#define _POSIX_C_SOURCE 200809L

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[128];
    size_t n;
} bucket_t;

static void bucket_add(bucket_t *b, size_t cap, const char *name) {
    for (size_t i = 0; i < cap; ++i) {
        if (b[i].n == 0) {
            snprintf(b[i].name, sizeof b[i].name, "%s", name);
            b[i].n = 1;
            return;
        }
        if (strcmp(b[i].name, name) == 0) {
            b[i].n++;
            return;
        }
    }
}

static const char *net_name(vl_net_t n) {
    switch (n) {
        case VL_NET_TCP: return "tcp";
        case VL_NET_WS: return "ws";
        case VL_NET_GRPC: return "grpc";
        case VL_NET_HTTP: return "http";
        default: return "unknown";
    }
}

static const char *proto_name(vl_proto_t p) {
    switch (p) {
        case VL_PROTO_VLESS: return "vless";
        case VL_PROTO_SOCKS5: return "socks5";
        case VL_PROTO_HTTP: return "http";
        case VL_PROTO_HTTPS: return "https";
        default: return "unknown";
    }
}

static const char *parse_status_name(cfg_status_t s) {
    switch (s) {
        case CFG_OK: return "ok";
        case CFG_ERR_BAD_ARG: return "bad arg";
        case CFG_ERR_SCHEME: return "bad scheme";
        case CFG_ERR_NO_AT: return "missing user host split";
        case CFG_ERR_NO_HOST: return "missing host";
        case CFG_ERR_BAD_PORT: return "bad port";
        case CFG_ERR_TOO_LONG: return "field too long";
        default: return "parse error";
    }
}

static size_t trim_line(char *line, size_t len) {
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t'))
        line[--len] = '\0';
    return len;
}

static void print_buckets(const char *title, const bucket_t *b, size_t cap) {
    printf("%s\n", title);
    for (size_t i = 0; i < cap; ++i) {
        if (b[i].n == 0) continue;
        printf("  %s: %zu\n", b[i].name, b[i].n);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/subscription.txt\n", argv[0]);
        return 2;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "open %s failed: %s\n", argv[1], strerror(errno));
        return 2;
    }

    bucket_t syntax[16];
    bucket_t unsupported[32];
    bucket_t supported[32];
    char *line = NULL;
    size_t cap = 0;
    size_t lines = 0;
    size_t nonempty = 0;
    size_t parsed = 0;
    size_t valid = 0;

    memset(syntax, 0, sizeof syntax);
    memset(unsupported, 0, sizeof unsupported);
    memset(supported, 0, sizeof supported);

    while (getline(&line, &cap, f) >= 0) {
        size_t len = trim_line(line, strlen(line));
        lines++;
        if (len == 0) continue;
        nonempty++;

        vl_server_t s;
        cfg_status_t ps = cfg_parse_link(line, &s);
        if (ps != CFG_OK) {
            bucket_add(syntax, sizeof syntax / sizeof syntax[0],
                       parse_status_name(ps));
            continue;
        }
        parsed++;

        char reason[128];
        if (cfg_validate_server(&s, reason, sizeof reason)) {
            char key[128];
            snprintf(key, sizeof key, "%s %s %s", proto_name(s.proto),
                     vl_sec_name(s.security), net_name(s.net));
            bucket_add(supported, sizeof supported / sizeof supported[0], key);
            valid++;
        } else {
            bucket_add(unsupported, sizeof unsupported / sizeof unsupported[0],
                       reason);
        }
    }

    free(line);
    fclose(f);

    printf("lines: %zu\n", lines);
    printf("nonempty: %zu\n", nonempty);
    printf("parsed: %zu\n", parsed);
    printf("supported: %zu\n", valid);
    printf("unsupported: %zu\n", parsed - valid);
    printf("syntax_failed: %zu\n", nonempty - parsed);
    print_buckets("supported buckets:", supported,
                  sizeof supported / sizeof supported[0]);
    print_buckets("unsupported reasons:", unsupported,
                  sizeof unsupported / sizeof unsupported[0]);
    print_buckets("syntax reasons:", syntax,
                  sizeof syntax / sizeof syntax[0]);
    return 0;
}
