#include "awg_config.h"

#include "b64.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AWG_CONFIG_FILE_MAX (64 * 1024)

typedef enum {
    AWG_SECTION_NONE = 0,
    AWG_SECTION_INTERFACE,
    AWG_SECTION_PEER
} awg_section_t;

static void set_reason(char *reason, size_t cap, const char *text) {
    if (!reason || cap == 0) return;
    snprintf(reason, cap, "%s", text ? text : "invalid config");
}

void awg_config_init(awg_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof *cfg);
    cfg->mtu = 1280;
    for (size_t i = 0; i < 4; ++i)
        cfg->header_min[i] = cfg->header_max[i] = (uint32_t)i + 1;
}

static void trim_span(const char **start, const char **end) {
    while (*start < *end && isspace((unsigned char)**start)) ++*start;
    while (*end > *start && isspace((unsigned char)(*end)[-1])) --*end;
}

static int span_equals(const char *start, const char *end, const char *word) {
    size_t n = (size_t)(end - start);
    return strlen(word) == n && strncmp(start, word, n) == 0;
}

static int copy_span(char *dst, size_t cap, const char *start, const char *end) {
    size_t n = (size_t)(end - start);
    if (n >= cap) return -1;
    if (n) memcpy(dst, start, n);
    dst[n] = '\0';
    return 0;
}

static int parse_u32(const char *start, const char *end, uint32_t *out) {
    char buf[32];
    if (!out || copy_span(buf, sizeof buf, start, end) != 0 || !buf[0]) return -1;
    errno = 0;
    char *tail = NULL;
    unsigned long n = strtoul(buf, &tail, 10);
    if (errno || tail == buf || *tail || n > UINT32_MAX) return -1;
    *out = (uint32_t)n;
    return 0;
}

static int parse_u16(const char *start, const char *end, uint16_t *out) {
    uint32_t n = 0;
    if (parse_u32(start, end, &n) != 0 || n > UINT16_MAX) return -1;
    *out = (uint16_t)n;
    return 0;
}

static int parse_key(const char *start, const char *end, uint8_t key[AWG_KEY_LEN]) {
    size_t out_len = 0;
    size_t len = (size_t)(end - start);
    return len > 0 && b64_decode(start, len, key, AWG_KEY_LEN, &out_len) == 0 &&
           out_len == AWG_KEY_LEN ? 0 : -1;
}

static int add_csv(char out[][64], size_t cap, size_t *count,
                   const char *start, const char *end) {
    const char *p = start;
    while (p < end) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *item_end = comma ? comma : end;
        const char *item_start = p;
        trim_span(&item_start, &item_end);
        if (item_start == item_end || *count >= cap ||
            copy_span(out[*count], sizeof out[0], item_start, item_end) != 0)
            return -1;
        ++*count;
        if (!comma) break;
        p = comma + 1;
    }
    return 0;
}

static int parse_endpoint(const char *start, const char *end, awg_config_t *cfg) {
    const char *host_start = start;
    const char *host_end = NULL;
    const char *port_start = NULL;
    if (start < end && *start == '[') {
        const char *close = memchr(start + 1, ']', (size_t)(end - start - 1));
        if (!close || close + 1 >= end || close[1] != ':') return -1;
        host_start = start + 1;
        host_end = close;
        port_start = close + 2;
    } else {
        const char *colon = end;
        while (colon > start && colon[-1] != ':') --colon;
        if (colon == start || colon == end) return -1;
        host_end = colon - 1;
        port_start = colon;
    }
    if (copy_span(cfg->endpoint_host, sizeof cfg->endpoint_host, host_start, host_end) != 0 ||
        !cfg->endpoint_host[0] || parse_u16(port_start, end, &cfg->endpoint_port) != 0 ||
        cfg->endpoint_port == 0)
        return -1;
    return 0;
}

static int parse_header_range(const char *start, const char *end,
                              uint32_t *min, uint32_t *max) {
    const char *dash = memchr(start, '-', (size_t)(end - start));
    if (!dash) {
        if (parse_u32(start, end, min) != 0) return -1;
        *max = *min;
        return 0;
    }
    if (dash == start || dash + 1 == end || memchr(dash + 1, '-', (size_t)(end - dash - 1)))
        return -1;
    if (parse_u32(start, dash, min) != 0 || parse_u32(dash + 1, end, max) != 0 || *min > *max)
        return -1;
    return 0;
}

static int signature_syntax_ok(const char *text) {
    const char *p = text;
    while (*p) {
        if (*p != '<') return 0;
        const char *end = strchr(p + 1, '>');
        if (!end || end == p + 1) return 0;
        p = end + 1;
    }
    return 1;
}

static int key_accepts_empty_value(awg_section_t section,
                                   const char *key_start, const char *key_end) {
    return section == AWG_SECTION_INTERFACE && key_end - key_start == 2 &&
           key_start[0] == 'I' && key_start[1] >= '1' && key_start[1] <= '5';
}

static awg_cfg_status_t assign_interface(awg_config_t *cfg,
                                          const char *key_start, const char *key_end,
                                          const char *value_start, const char *value_end,
                                          char *reason, size_t reason_cap) {
    if (span_equals(key_start, key_end, "PrivateKey")) {
        if (parse_key(value_start, value_end, cfg->private_key) != 0) goto bad_key;
    } else if (span_equals(key_start, key_end, "Address")) {
        if (add_csv(cfg->addresses, AWG_MAX_ADDRESSES, &cfg->address_count,
                    value_start, value_end) != 0) goto bad_value;
    } else if (span_equals(key_start, key_end, "DNS")) {
        if (add_csv(cfg->dns, AWG_MAX_DNS, &cfg->dns_count, value_start, value_end) != 0)
            goto bad_value;
    } else if (span_equals(key_start, key_end, "MTU")) {
        if (parse_u16(value_start, value_end, &cfg->mtu) != 0 || cfg->mtu < 576)
            goto bad_value;
    } else if (span_equals(key_start, key_end, "Jc")) {
        if (parse_u32(value_start, value_end, &cfg->jc) != 0 || cfg->jc > 128) goto bad_value;
    } else if (span_equals(key_start, key_end, "Jmin")) {
        if (parse_u32(value_start, value_end, &cfg->jmin) != 0) goto bad_value;
    } else if (span_equals(key_start, key_end, "Jmax")) {
        if (parse_u32(value_start, value_end, &cfg->jmax) != 0) goto bad_value;
    } else if (key_end - key_start == 2 && key_start[0] == 'S' &&
               key_start[1] >= '1' && key_start[1] <= '4') {
        size_t i = (size_t)(key_start[1] - '1');
        if (parse_u32(value_start, value_end, &cfg->padding[i]) != 0) goto bad_value;
    } else if (key_end - key_start == 2 && key_start[0] == 'H' &&
               key_start[1] >= '1' && key_start[1] <= '4') {
        size_t i = (size_t)(key_start[1] - '1');
        if (parse_header_range(value_start, value_end, &cfg->header_min[i],
                               &cfg->header_max[i]) != 0)
            goto bad_value;
    } else if (key_end - key_start == 2 && key_start[0] == 'I' &&
               key_start[1] >= '1' && key_start[1] <= '5') {
        size_t i = (size_t)(key_start[1] - '1');
        if (copy_span(cfg->signature[i], sizeof cfg->signature[i], value_start, value_end) != 0 ||
            !signature_syntax_ok(cfg->signature[i]))
            goto bad_value;
    }
    return AWG_CFG_OK;

bad_key:
    set_reason(reason, reason_cap, "invalid interface private key");
    return AWG_CFG_ERR_KEY;
bad_value:
    set_reason(reason, reason_cap, "invalid interface value");
    return AWG_CFG_ERR_RANGE;
}

static awg_cfg_status_t assign_peer(awg_config_t *cfg,
                                     const char *key_start, const char *key_end,
                                     const char *value_start, const char *value_end,
                                     char *reason, size_t reason_cap) {
    if (span_equals(key_start, key_end, "PublicKey")) {
        if (parse_key(value_start, value_end, cfg->peer_public_key) != 0) goto bad_key;
    } else if (span_equals(key_start, key_end, "PresharedKey")) {
        if (parse_key(value_start, value_end, cfg->preshared_key) != 0) goto bad_key;
        cfg->has_preshared_key = 1;
    } else if (span_equals(key_start, key_end, "Endpoint")) {
        if (parse_endpoint(value_start, value_end, cfg) != 0) goto bad_value;
    } else if (span_equals(key_start, key_end, "AllowedIPs")) {
        if (copy_span(cfg->allowed_ips, sizeof cfg->allowed_ips, value_start, value_end) != 0)
            goto bad_value;
    } else if (span_equals(key_start, key_end, "PersistentKeepalive")) {
        if (parse_u16(value_start, value_end, &cfg->persistent_keepalive) != 0)
            goto bad_value;
    }
    return AWG_CFG_OK;

bad_key:
    set_reason(reason, reason_cap, "invalid peer key");
    return AWG_CFG_ERR_KEY;
bad_value:
    set_reason(reason, reason_cap, "invalid peer value");
    return AWG_CFG_ERR_RANGE;
}

awg_cfg_status_t awg_config_parse(const char *text, size_t len, awg_config_t *cfg,
                                  char *reason, size_t reason_cap) {
    if (!text || !cfg || len == 0 || len > AWG_CONFIG_FILE_MAX) {
        set_reason(reason, reason_cap, "invalid config input");
        return AWG_CFG_ERR_ARG;
    }
    awg_config_init(cfg);
    awg_section_t section = AWG_SECTION_NONE;
    int have_interface = 0;
    int have_peer = 0;
    const char *p = text;
    const char *end = text + len;

    while (p < end) {
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        if (!line_end) line_end = end;
        const char *start = p;
        const char *finish = line_end;
        trim_span(&start, &finish);
        if (start < finish && *start != '#' && *start != ';') {
            if (span_equals(start, finish, "[Interface]")) {
                if (have_interface) {
                    set_reason(reason, reason_cap, "duplicate interface section");
                    return AWG_CFG_ERR_FORMAT;
                }
                section = AWG_SECTION_INTERFACE;
                have_interface = 1;
            } else if (span_equals(start, finish, "[Peer]")) {
                if (have_peer) {
                    set_reason(reason, reason_cap, "multiple peers are not supported");
                    return AWG_CFG_ERR_FORMAT;
                }
                section = AWG_SECTION_PEER;
                have_peer = 1;
            } else {
                const char *equals = memchr(start, '=', (size_t)(finish - start));
                if (!equals || section == AWG_SECTION_NONE) {
                    set_reason(reason, reason_cap, "invalid config line");
                    return AWG_CFG_ERR_FORMAT;
                }
                const char *key_start = start;
                const char *key_end = equals;
                const char *value_start = equals + 1;
                const char *value_end = finish;
                trim_span(&key_start, &key_end);
                trim_span(&value_start, &value_end);
                if (key_start == key_end ||
                    (value_start == value_end &&
                     !key_accepts_empty_value(section, key_start, key_end))) {
                    set_reason(reason, reason_cap, "empty config value");
                    return AWG_CFG_ERR_FORMAT;
                }
                awg_cfg_status_t r = section == AWG_SECTION_INTERFACE
                    ? assign_interface(cfg, key_start, key_end, value_start, value_end, reason, reason_cap)
                    : assign_peer(cfg, key_start, key_end, value_start, value_end, reason, reason_cap);
                if (r != AWG_CFG_OK) return r;
            }
        }
        p = line_end < end ? line_end + 1 : end;
    }

    if (!have_interface || !have_peer || !cfg->endpoint_host[0] || !cfg->endpoint_port) {
        set_reason(reason, reason_cap, "missing interface, peer, or endpoint");
        return AWG_CFG_ERR_MISSING;
    }
    if (cfg->jmin > cfg->jmax || cfg->jmax > 65507) {
        set_reason(reason, reason_cap, "invalid junk packet range");
        return AWG_CFG_ERR_RANGE;
    }
    for (size_t i = 0; i < 4; ++i) {
        uint32_t base = i == 0 ? 148 : (i == 1 ? 92 : (i == 2 ? 64 : 32));
        if (cfg->padding[i] > cfg->mtu || base + cfg->padding[i] > cfg->mtu) {
            set_reason(reason, reason_cap, "packet padding exceeds mtu");
            return AWG_CFG_ERR_RANGE;
        }
        for (size_t j = i + 1; j < 4; ++j) {
            if (cfg->header_min[i] <= cfg->header_max[j] &&
                cfg->header_min[j] <= cfg->header_max[i]) {
                set_reason(reason, reason_cap, "overlapping packet headers");
                return AWG_CFG_ERR_RANGE;
            }
        }
    }
    set_reason(reason, reason_cap, "ok");
    return AWG_CFG_OK;
}

awg_cfg_status_t awg_config_load_file(const char *path, awg_config_t *cfg,
                                      char *reason, size_t reason_cap) {
    if (!path || !cfg) {
        set_reason(reason, reason_cap, "invalid config path");
        return AWG_CFG_ERR_ARG;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_reason(reason, reason_cap, "cannot read config file");
        return AWG_CFG_ERR_FORMAT;
    }
    static char buf[AWG_CONFIG_FILE_MAX + 1];
    size_t len = fread(buf, 1, AWG_CONFIG_FILE_MAX + 1, f);
    int read_error = ferror(f);
    fclose(f);
    if (read_error || len == 0 || len > AWG_CONFIG_FILE_MAX) {
        set_reason(reason, reason_cap, "invalid config file size");
        return AWG_CFG_ERR_SPACE;
    }
    return awg_config_parse(buf, len, cfg, reason, reason_cap);
}
