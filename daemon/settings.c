#include "settings.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

void daemon_settings_defaults(daemon_settings_t *s) {
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->socks_port = SENKO_DEFAULT_SOCKS_PORT;
    s->socks_public = 0;
    s->dns_local_port = SENKO_DEFAULT_DNS_LOCAL_PORT;
    snprintf(s->dns_upstream, sizeof s->dns_upstream, "8.8.8.8");
}

static int parse_uint16(const char *s, const char *end, uint16_t *out) {
    unsigned long v = 0;
    int any = 0;
    for (; s < end; ++s) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10ul + (unsigned long)(*s - '0');
        any = 1;
        if (v > 65535ul) return -1;
    }
    if (!any) return -1;
    *out = (uint16_t)v;
    return 0;
}

static int parse_bool01(const char *s, const char *end, int *out) {
    if ((size_t)(end - s) == 1 && (*s == '0' || *s == '1')) {
        *out = (*s == '1');
        return 0;
    }
    return -1;
}

static int ipv4_ok(const char *s, size_t len) {
    char tmp[SETTINGS_DNS_UPSTREAM_MAX];
    if (len == 0 || len >= sizeof tmp) return 0;
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    struct in_addr a;
    return inet_pton(AF_INET, tmp, &a) == 1;
}

int daemon_settings_apply_line(daemon_settings_t *s, const char *line, size_t len) {
    if (!s || !line || len < 5 || memcmp(line, "SET ", 4) != 0) return 0;

    const char *rest = line + 4;
    const char *sp = memchr(rest, ' ', (size_t)(line + len - rest));
    if (!sp) return 1;

    size_t key_len = (size_t)(sp - rest);
    const char *val = sp + 1;
    size_t val_len = (size_t)(line + len - val);
    while (val_len > 0 && (val[val_len - 1] == '\n' || val[val_len - 1] == '\r'
                           || val[val_len - 1] == ' '))
        val_len--;

    if (key_len == 10 && memcmp(rest, "socks_port", 10) == 0) {
        uint16_t p;
        if (parse_uint16(val, val + val_len, &p) == 0 && p > 0) s->socks_port = p;
    } else if (key_len == 12 && memcmp(rest, "socks_public", 12) == 0) {
        int b;
        if (parse_bool01(val, val + val_len, &b) == 0) s->socks_public = b;
    } else if (key_len == 14 && memcmp(rest, "dns_local_port", 14) == 0) {
        uint16_t p;
        if (parse_uint16(val, val + val_len, &p) == 0 && p > 0) s->dns_local_port = p;
    } else if (key_len == 12 && memcmp(rest, "dns_upstream", 12) == 0) {
        if (ipv4_ok(val, val_len)) {
            memcpy(s->dns_upstream, val, val_len);
            s->dns_upstream[val_len] = '\0';
        }
    }
    return 1;
}

void daemon_settings_apply_buf(daemon_settings_t *s, const char *buf, size_t len) {
    if (!s || !buf) return;
    const char *p = buf;
    const char *end = buf + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        daemon_settings_apply_line(s, p, (size_t)(le - p));
        if (!nl) break;
        p = nl + 1;
    }
}

int daemon_settings_serialize(const daemon_settings_t *s, char *buf, size_t cap,
                              size_t *out_len) {
    if (!s || !buf) return -1;
    size_t off = 0;
    int n;

    n = snprintf(buf + off, cap - off, "SET socks_port %u\n", (unsigned)s->socks_port);
    if (n < 0 || (size_t)n >= cap - off) return -1;
    off += (size_t)n;

    n = snprintf(buf + off, cap - off, "SET socks_public %d\n", s->socks_public ? 1 : 0);
    if (n < 0 || (size_t)n >= cap - off) return -1;
    off += (size_t)n;

    n = snprintf(buf + off, cap - off, "SET dns_upstream %s\n", s->dns_upstream);
    if (n < 0 || (size_t)n >= cap - off) return -1;
    off += (size_t)n;

    n = snprintf(buf + off, cap - off, "SET dns_local_port %u\n",
                 (unsigned)s->dns_local_port);
    if (n < 0 || (size_t)n >= cap - off) return -1;
    off += (size_t)n;

    if (out_len) *out_len = off;
    return 0;
}
