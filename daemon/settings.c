#include "settings.h"

#include "routing.h"

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
    s->block_response = DNS_BLOCK_ZERO;
    s->auto_connect = 0;
    /* a tunnel that died with nobody watching used to stay dead until the user
       opened the app, which on a phone is the wrong default */
    s->auto_reconnect = 1;
    s->reconnect_max_attempts = SENKO_DEFAULT_RECONNECT_ATTEMPTS;
    s->sub_refresh_hours = 0;
    s->failover = 0;
    s->force_backend = SENKO_BACKEND_AUTO;
    s->force_pf_mode = SENKO_PF_MODE_AUTO;
    s->sub_ignore_gating = 0;
    s->trace = 0;
}

const char *daemon_settings_backend_name(senko_backend_force_t forced) {
    switch (forced) {
        case SENKO_BACKEND_GO:        return "go";
        case SENKO_BACKEND_C:         return "c";
        case SENKO_BACKEND_APP_PROXY: return "app_proxy";
        case SENKO_BACKEND_AUTO:      break;
    }
    return "auto";
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

static int parse_bounded_int(const char *s, const char *end, int lo, int hi, int *out) {
    long v = 0;
    int any = 0;
    for (; s < end; ++s) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        any = 1;
        if (v > (long)hi) return -1;
    }
    if (!any || v < (long)lo) return -1;
    *out = (int)v;
    return 0;
}

static int ipv4_ok(const char *s, size_t len) {
    char tmp[SETTINGS_DNS_UPSTREAM_MAX];
    if (len == 0 || len >= sizeof tmp) return 0;
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    struct in_addr a;
    return inet_pton(AF_INET, tmp, &a) == 1;
}

static int key_is(const char *key, size_t key_len, const char *name) {
    size_t n = strlen(name);
    return key_len == n && memcmp(key, name, n) == 0;
}

settings_status_t daemon_settings_set(daemon_settings_t *s,
                                      const char *key, size_t key_len,
                                      const char *val, size_t val_len) {
    if (!s || !key || !val || key_len == 0) return SETTINGS_ERR_KEY;
    const char *ve = val + val_len;

    if (key_is(key, key_len, "socks_port")) {
        uint16_t p;
        if (parse_uint16(val, ve, &p) != 0 || p == 0) return SETTINGS_ERR_VALUE;
        s->socks_port = p;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "socks_public")) {
        int b;
        if (parse_bool01(val, ve, &b) != 0) return SETTINGS_ERR_VALUE;
        s->socks_public = b;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "dns_local_port")) {
        uint16_t p;
        if (parse_uint16(val, ve, &p) != 0 || p == 0) return SETTINGS_ERR_VALUE;
        s->dns_local_port = p;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "dns_upstream")) {
        if (!ipv4_ok(val, val_len)) return SETTINGS_ERR_VALUE;
        memcpy(s->dns_upstream, val, val_len);
        s->dns_upstream[val_len] = '\0';
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "block_response")) {
        if (val_len == 4 && memcmp(val, "zero", 4) == 0)
            s->block_response = DNS_BLOCK_ZERO;
        else if (val_len == 8 && memcmp(val, "nxdomain", 8) == 0)
            s->block_response = DNS_BLOCK_NXDOMAIN;
        else if (val_len == 7 && memcmp(val, "refused", 7) == 0)
            s->block_response = DNS_BLOCK_REFUSED;
        else
            return SETTINGS_ERR_VALUE;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "auto_connect")) {
        int b;
        if (parse_bool01(val, ve, &b) != 0) return SETTINGS_ERR_VALUE;
        s->auto_connect = b;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "auto_reconnect")) {
        int b;
        if (parse_bool01(val, ve, &b) != 0) return SETTINGS_ERR_VALUE;
        s->auto_reconnect = b;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "reconnect_max_attempts")) {
        int v;
        if (parse_bounded_int(val, ve, 0, 100, &v) != 0) return SETTINGS_ERR_VALUE;
        s->reconnect_max_attempts = v;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "sub_refresh_hours")) {
        int v;
        /* a week is the longest schedule a panel expiry makes any sense at */
        if (parse_bounded_int(val, ve, 0, 168, &v) != 0) return SETTINGS_ERR_VALUE;
        s->sub_refresh_hours = v;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "failover")) {
        int b;
        if (parse_bool01(val, ve, &b) != 0) return SETTINGS_ERR_VALUE;
        s->failover = b;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "force_backend")) {
        if (val_len == 4 && memcmp(val, "auto", 4) == 0)
            s->force_backend = SENKO_BACKEND_AUTO;
        else if (val_len == 2 && memcmp(val, "go", 2) == 0)
            s->force_backend = SENKO_BACKEND_GO;
        else if (val_len == 1 && val[0] == 'c')
            s->force_backend = SENKO_BACKEND_C;
        else if (val_len == 9 && memcmp(val, "app_proxy", 9) == 0)
            s->force_backend = SENKO_BACKEND_APP_PROXY;
        else
            return SETTINGS_ERR_VALUE;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "force_pf_mode")) {
        int v;
        if (val_len == 4 && memcmp(val, "auto", 4) == 0) {
            s->force_pf_mode = SENKO_PF_MODE_AUTO;
            return SETTINGS_OK;
        }
        if (parse_bounded_int(val, ve, 0, ROUTING_PF_MODE_COUNT - 1, &v) != 0)
            return SETTINGS_ERR_VALUE;
        s->force_pf_mode = v;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "sub_ignore_gating")) {
        int b;
        if (parse_bool01(val, ve, &b) != 0) return SETTINGS_ERR_VALUE;
        s->sub_ignore_gating = b;
        return SETTINGS_OK;
    }
    if (key_is(key, key_len, "trace")) {
        int b;
        if (parse_bool01(val, ve, &b) != 0) return SETTINGS_ERR_VALUE;
        s->trace = b;
        return SETTINGS_OK;
    }
    return SETTINGS_ERR_KEY;
}

/* a config written by a newer build carries keys this one has no field for, and
   dropping the whole file over one of them would lose every server in it */
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

    (void)daemon_settings_set(s, rest, key_len, val, val_len);
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

#define SETTINGS_EMIT(fmt, value)                                     \
    do {                                                              \
        n = snprintf(buf + off, cap - off, fmt, value);               \
        if (n < 0 || (size_t)n >= cap - off) return -1;               \
        off += (size_t)n;                                             \
    } while (0)

    SETTINGS_EMIT("SET socks_port %u\n", (unsigned)s->socks_port);
    SETTINGS_EMIT("SET socks_public %d\n", s->socks_public ? 1 : 0);
    SETTINGS_EMIT("SET dns_upstream %s\n", s->dns_upstream);
    SETTINGS_EMIT("SET dns_local_port %u\n", (unsigned)s->dns_local_port);
    SETTINGS_EMIT("SET block_response %s\n",
                  s->block_response == DNS_BLOCK_NXDOMAIN ? "nxdomain" :
                  s->block_response == DNS_BLOCK_REFUSED ? "refused" : "zero");
    SETTINGS_EMIT("SET auto_connect %d\n", s->auto_connect ? 1 : 0);
    SETTINGS_EMIT("SET auto_reconnect %d\n", s->auto_reconnect ? 1 : 0);
    SETTINGS_EMIT("SET reconnect_max_attempts %d\n", s->reconnect_max_attempts);
    SETTINGS_EMIT("SET sub_refresh_hours %d\n", s->sub_refresh_hours);
    SETTINGS_EMIT("SET failover %d\n", s->failover ? 1 : 0);
    SETTINGS_EMIT("SET force_backend %s\n",
                  daemon_settings_backend_name(s->force_backend));
    if (s->force_pf_mode == SENKO_PF_MODE_AUTO)
        SETTINGS_EMIT("SET force_pf_mode %s\n", "auto");
    else
        SETTINGS_EMIT("SET force_pf_mode %d\n", s->force_pf_mode);
    SETTINGS_EMIT("SET sub_ignore_gating %d\n", s->sub_ignore_gating ? 1 : 0);
    SETTINGS_EMIT("SET trace %d\n", s->trace ? 1 : 0);

#undef SETTINGS_EMIT

    if (out_len) *out_len = off;
    return 0;
}
