#include "senko_core_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "core/geo.h"

typedef struct {
    char *out;
    size_t cap;
    size_t len;
    int failed;
} json_out_t;

static void append_raw(json_out_t *j, const char *value) {
    size_t n;
    if (!j || j->failed || !value) return;
    n = strlen(value);
    if (n >= j->cap - j->len) {
        j->failed = 1;
        return;
    }
    memcpy(j->out + j->len, value, n);
    j->len += n;
    j->out[j->len] = '\0';
}

static void append_format(json_out_t *j, const char *format, ...) {
    int n;
    va_list ap;
    if (!j || j->failed || !format) return;
    va_start(ap, format);
    n = vsnprintf(j->out + j->len, j->cap - j->len, format, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= j->cap - j->len) {
        j->failed = 1;
        return;
    }
    j->len += (size_t)n;
}

static void append_string(json_out_t *j, const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    append_raw(j, "\"");
    while (!j->failed && *p) {
        char escaped[7];
        switch (*p) {
            case '\"': append_raw(j, "\\\""); break;
            case '\\': append_raw(j, "\\\\"); break;
            case '\b': append_raw(j, "\\b"); break;
            case '\f': append_raw(j, "\\f"); break;
            case '\n': append_raw(j, "\\n"); break;
            case '\r': append_raw(j, "\\r"); break;
            case '\t': append_raw(j, "\\t"); break;
            default:
                if (*p < 0x20) {
                    snprintf(escaped, sizeof escaped, "\\u%04x", *p);
                    append_raw(j, escaped);
                } else {
                    escaped[0] = (char)*p;
                    escaped[1] = '\0';
                    append_raw(j, escaped);
                }
                break;
        }
        ++p;
    }
    append_raw(j, "\"");
}

static const char *network_name(vl_net_t net) {
    switch (net) {
        case VL_NET_TCP: return "tcp";
        case VL_NET_WS: return "ws";
        case VL_NET_GRPC: return "grpc";
        case VL_NET_XHTTP: return "xhttp";
        default: return NULL;
    }
}

static void append_grpc_service(json_out_t *j, const char *path) {
    char service[256];
    const char *start = path ? path : "";
    if (*start == '/') ++start;
    size_t len = strlen(start);
    if (strcmp(start, "Tun") == 0) len = 0;
    else if (len >= 4 && memcmp(start + len - 4, "/Tun", 4) == 0) len -= 4;
    if (len >= sizeof service) {
        j->failed = 1;
        return;
    }
    memcpy(service, start, len);
    service[len] = '\0';
    append_string(j, service);
}

/* go on darwin verifies through Security unless the config names its own roots */
static void append_roots(json_out_t *j, const char *ca_file) {
    if (!ca_file) return;
    append_raw(j, ",\"disableSystemRoot\":true,\"certificates\":[{\"usage\":\"verify\",\"certificateFile\":");
    append_string(j, ca_file);
    append_raw(j, "}]");
}

static int append_stream(json_out_t *j, const vl_server_t *s, const char *ca_file) {
    const char *network = network_name(s->net);
    if (!network) return -1;
    append_raw(j, ",\"streamSettings\":{\"network\":");
    append_string(j, network);
    append_raw(j, ",\"security\":");
    append_string(j, vl_sec_name(s->security));

    if (s->security == VL_SEC_REALITY) {
        append_raw(j, ",\"realitySettings\":{\"serverName\":");
        append_string(j, s->sni[0] ? s->sni : s->host);
        append_raw(j, ",\"fingerprint\":");
        append_string(j, s->fp[0] ? s->fp : "chrome");
        append_raw(j, ",\"publicKey\":");
        append_string(j, s->pbk);
        append_raw(j, ",\"shortId\":");
        append_string(j, s->sid);
        append_raw(j, "}");
    } else if (s->security == VL_SEC_TLS) {
        append_raw(j, ",\"tlsSettings\":{\"serverName\":");
        append_string(j, s->sni[0] ? s->sni : s->host);
        append_raw(j, ",\"fingerprint\":");
        append_string(j, s->fp[0] ? s->fp : "chrome");
        append_roots(j, ca_file);
        append_raw(j, "}");
    }

    if (s->net == VL_NET_WS) {
        append_raw(j, ",\"wsSettings\":{\"path\":");
        append_string(j, s->path[0] ? s->path : "/");
        append_raw(j, ",\"host\":");
        append_string(j, s->ws_host[0] ? s->ws_host : s->sni);
        append_raw(j, "}");
    } else if (s->net == VL_NET_GRPC) {
        append_raw(j, ",\"grpcSettings\":{\"serviceName\":");
        append_grpc_service(j, s->path);
        append_raw(j, "}");
    } else if (s->net == VL_NET_XHTTP) {
        append_raw(j, ",\"xhttpSettings\":{\"path\":");
        append_string(j, s->path[0] ? s->path : "/");
        append_raw(j, ",\"host\":");
        append_string(j, s->ws_host[0] ? s->ws_host : s->sni);
        append_raw(j, ",\"mode\":");
        append_string(j, s->mode[0] ? s->mode : "auto");
        append_raw(j, "}");
    }
    append_raw(j, "}");
    return j->failed ? -1 : 0;
}

static int append_outbound(json_out_t *j, const vl_server_t *s,
                           const char *endpoint_ip, const char *ca_file) {
    const char *protocol;
    if (s->proto == VL_PROTO_VLESS) {
        append_raw(j, "{\"tag\":\"senko-out\",\"protocol\":\"vless\",\"settings\":{\"address\":");
        append_string(j, endpoint_ip);
        append_format(j, ",\"port\":%u,\"id\":", (unsigned)s->port);
        append_string(j, s->uuid);
        append_raw(j, ",\"encryption\":");
        append_string(j, s->encryption[0] ? s->encryption : "none");
        append_raw(j, ",\"flow\":");
        append_string(j, s->flow);
        append_raw(j, "}");
        if (append_stream(j, s, ca_file) != 0) return -1;
        append_raw(j, "}");
        return j->failed ? -1 : 0;
    }

    if (s->proto == VL_PROTO_HYSTERIA2) {
/* the outbound settings only carry the destination: auth and the quic
   transport itself live in streamSettings.hysteriaSettings below */
        append_raw(j, "{\"tag\":\"senko-out\",\"protocol\":\"hysteria\",\"settings\":{\"version\":2,\"address\":");
        append_string(j, endpoint_ip);
        append_format(j, ",\"port\":%u}", (unsigned)s->port);
        append_raw(j, ",\"streamSettings\":{\"network\":\"hysteria\",\"security\":\"tls\",\"tlsSettings\":{\"serverName\":");
        append_string(j, s->sni[0] ? s->sni : s->host);
        append_raw(j, ",\"fingerprint\":");
        append_string(j, s->fp[0] ? s->fp : "chrome");
/* this fork removed allowInsecure outright, so a self-signed node has no way
   through except a pinned hash */
        if (s->pin_sha256[0]) {
            append_raw(j, ",\"pinnedPeerCertSha256\":");
            append_string(j, s->pin_sha256);
        }
        append_roots(j, ca_file);
        append_raw(j, "},\"hysteriaSettings\":{\"version\":2,\"auth\":");
        append_string(j, s->pass);
        append_raw(j, "}");
        if ((s->obfs[0] && strcmp(s->obfs, "none") != 0) || s->port_hop[0]) {
            int need_comma = 0;
            append_raw(j, ",\"finalmask\":{");
            if (s->obfs[0] && strcmp(s->obfs, "none") != 0) {
                append_raw(j, "\"udp\":[{\"type\":");
                append_string(j, s->obfs);
                append_raw(j, ",\"settings\":{\"password\":");
                append_string(j, s->obfs_password);
                append_raw(j, "}}]");
                need_comma = 1;
            }
            if (s->port_hop[0]) {
                if (need_comma) append_raw(j, ",");
                append_raw(j, "\"quicParams\":{\"udpHop\":{\"ports\":");
                append_string(j, s->port_hop);
                append_raw(j, "}}");
            }
            append_raw(j, "}");
        }
        append_raw(j, "}}");
        return j->failed ? -1 : 0;
    }

    if (s->proto == VL_PROTO_VMESS) {
/* the flat form infra/conf/vmess.go reads when settings carries an address;
   a cipher name it does not know runs as auto there */
        append_raw(j, "{\"tag\":\"senko-out\",\"protocol\":\"vmess\",\"settings\":{\"address\":");
        append_string(j, endpoint_ip);
        append_format(j, ",\"port\":%u,\"id\":", (unsigned)s->port);
        append_string(j, s->uuid);
        append_raw(j, ",\"security\":");
        append_string(j, s->encryption[0] ? s->encryption : "auto");
        append_raw(j, "}");
        if (append_stream(j, s, ca_file) != 0) return -1;
        append_raw(j, "}");
        return j->failed ? -1 : 0;
    }

    if (s->proto == VL_PROTO_TROJAN) {
        append_raw(j, "{\"tag\":\"senko-out\",\"protocol\":\"trojan\",\"settings\":{\"servers\":[{\"address\":");
        append_string(j, endpoint_ip);
        append_format(j, ",\"port\":%u,\"password\":", (unsigned)s->port);
        append_string(j, s->pass);
        append_raw(j, "}]}");
        if (append_stream(j, s, ca_file) != 0) return -1;
        append_raw(j, "}");
        return j->failed ? -1 : 0;
    }

    if (s->proto == VL_PROTO_SHADOWSOCKS) {
        append_raw(j, "{\"tag\":\"senko-out\",\"protocol\":\"shadowsocks\",\"settings\":{\"servers\":[{\"address\":");
        append_string(j, endpoint_ip);
        append_format(j, ",\"port\":%u,\"method\":", (unsigned)s->port);
        append_string(j, s->encryption);
        append_raw(j, ",\"password\":");
        append_string(j, s->pass);
        append_raw(j, "}]}");
        append_raw(j, "}");
        return j->failed ? -1 : 0;
    }

    protocol = (s->proto == VL_PROTO_SOCKS5) ? "socks" : "http";
    append_raw(j, "{\"tag\":\"senko-out\",\"protocol\":");
    append_string(j, protocol);
    append_raw(j, ",\"settings\":{\"servers\":[{\"address\":");
    append_string(j, endpoint_ip);
    append_format(j, ",\"port\":%u", (unsigned)s->port);
    if (s->user[0] || s->pass[0]) {
        append_raw(j, ",\"users\":[{\"user\":");
        append_string(j, s->user);
        append_raw(j, ",\"pass\":");
        append_string(j, s->pass);
        append_raw(j, "}]");
    }
    append_raw(j, "}]}");
    if (s->proto == VL_PROTO_HTTPS) {
        append_raw(j, ",\"streamSettings\":{\"security\":\"tls\",\"tlsSettings\":{\"serverName\":");
        append_string(j, s->host);
        append_roots(j, ca_file);
        append_raw(j, "}}");
    }
    append_raw(j, "}");
    return j->failed ? -1 : 0;
}

/* xray can name a domain or address set from a dat in its asset directory, but
   not an app. a geo rule is written when its dat is there, is inert when its
   set is empty or was never downloaded (the utun backend matches nothing for
   it too), and is lost when senko-core has no directory to read it from */
typedef enum { CORE_RULE_WRITE, CORE_RULE_INERT, CORE_RULE_LOST } core_rule_state_t;

static int geo_dat_present(const char *geo_dir, rule_type_t type) {
    char path[320];
    int n = snprintf(path, sizeof path, "%s/%s", geo_dir,
                     type == RULE_TYPE_GEOSITE ? "geosite.dat" : "geoip.dat");
    return n > 0 && (size_t)n < sizeof path && access(path, R_OK) == 0;
}

static core_rule_state_t core_rule_state(const rule_t *rule, const char *geo_dir) {
    if (rule->type == RULE_TYPE_APP) return CORE_RULE_LOST;
    if (rule->type != RULE_TYPE_GEOSITE && rule->type != RULE_TYPE_GEOIP)
        return CORE_RULE_WRITE;
    if (rule->type == RULE_TYPE_GEOIP && strcmp(rule->value, "private") == 0)
        return CORE_RULE_WRITE;
    if (!geo_dir || !geo_dir[0]) return CORE_RULE_LOST;
    size_t count = !rule->geo ? 0 : rule->type == RULE_TYPE_GEOSITE
        ? geo_site_count((const geo_site_t *)rule->geo)
        : geo_ip_count((const geo_ip_t *)rule->geo);
    if (count == 0) return CORE_RULE_INERT;
    return geo_dat_present(geo_dir, rule->type) ? CORE_RULE_WRITE : CORE_RULE_LOST;
}

size_t senko_core_config_unsupported(const ruleset_t *rules, const char *geo_dir) {
    size_t n = 0;
    for (size_t i = 0; rules && i < rules->count; ++i)
        if (core_rule_state(&rules->entries[i], geo_dir) == CORE_RULE_LOST) ++n;
    return n;
}

/* the ranges geoip:private names, spelled out because the dat is not trusted to
   carry that code */
static const char *const PRIVATE_RANGES[] = {
    "0.0.0.0/8", "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8", "169.254.0.0/16",
    "172.16.0.0/12", "192.0.0.0/24", "192.0.2.0/24", "192.88.99.0/24",
    "192.168.0.0/16", "198.18.0.0/15", "198.51.100.0/24", "203.0.113.0/24",
    "224.0.0.0/4", "240.0.0.0/4", "::1/128", "fc00::/7", "fe80::/10"
};

static void append_routing_rule(json_out_t *j, const rule_t *rule, int *first) {
    if (!*first) append_raw(j, ",");
    *first = 0;
    append_raw(j, "{\"type\":\"field\",\"outboundTag\":");
    if (rule->action == RULE_ACTION_DIRECT) append_string(j, "direct");
    else if (rule->action == RULE_ACTION_BLOCK) append_string(j, "block");
    else append_string(j, "senko-out");
    if (rule->type == RULE_TYPE_IP_CIDR) {
        append_raw(j, ",\"ip\":[");
        append_string(j, rule->value);
    } else if (rule->type == RULE_TYPE_GEOIP && strcmp(rule->value, "private") == 0) {
        append_raw(j, ",\"ip\":[");
        for (size_t i = 0; i < sizeof PRIVATE_RANGES / sizeof PRIVATE_RANGES[0]; ++i) {
            if (i) append_raw(j, ",");
            append_string(j, PRIVATE_RANGES[i]);
        }
    } else if (rule->type == RULE_TYPE_GEOSITE || rule->type == RULE_TYPE_GEOIP) {
        char tag[RULE_VALUE_MAX + 16];
        int n = snprintf(tag, sizeof tag, "%s:%s",
                         rule->type == RULE_TYPE_GEOSITE ? "geosite" : "geoip", rule->value);
        if (n < 0 || (size_t)n >= sizeof tag) {
            j->failed = 1;
            return;
        }
        append_raw(j, rule->type == RULE_TYPE_GEOSITE ? ",\"domain\":[" : ",\"ip\":[");
        append_string(j, tag);
    } else {
        char domain[RULE_VALUE_MAX + 16];
        int n = snprintf(domain, sizeof domain, "%s%s",
                         rule->type == RULE_TYPE_DOMAIN_SUFFIX ? "domain:"
                         : rule->type == RULE_TYPE_DOMAIN_FULL ? "full:" : "keyword:",
                         rule->value);
        if (n < 0 || (size_t)n >= sizeof domain) {
            j->failed = 1;
            return;
        }
        append_raw(j, ",\"domain\":[");
        append_string(j, domain);
    }
    append_raw(j, "]}");
}

static void append_routing(json_out_t *j, const ruleset_t *rules, const char *geo_dir) {
    append_raw(j, ",\"routing\":{\"domainStrategy\":\"IPIfNonMatch\",\"rules\":[");
    int first = 1;
    if (rules) {
        const rule_action_t order[] = {
            RULE_ACTION_BLOCK, RULE_ACTION_DIRECT, RULE_ACTION_PROXY
        };
        for (size_t rank = 0; rank < sizeof order / sizeof order[0]; ++rank)
            for (size_t i = 0; i < rules->count; ++i)
                if (rules->entries[i].action == order[rank] &&
                    core_rule_state(&rules->entries[i], geo_dir) == CORE_RULE_WRITE)
                    append_routing_rule(j, &rules->entries[i], &first);
        /* a profile that sends unnamed traffic straight out is only safe when every
           rule that names the exceptions could be written; a skipped geo rule would
           leave its sites direct, so then the default stays the tunnel */
        if (rules->fallback == RULE_ACTION_DIRECT && senko_core_config_unsupported(rules, geo_dir) == 0) {
            if (!first) append_raw(j, ",");
            first = 0;
            append_raw(j, "{\"type\":\"field\",\"network\":\"tcp,udp\",\"outboundTag\":\"direct\"}");
        }
    }
    append_raw(j, "]}");
}

int senko_core_config_render_rules(const vl_server_t *server, const char *endpoint_ip,
                           const char *ifname, const ruleset_t *rules, const char *geo_dir,
                           const char *ca_file, char *out, size_t out_cap) {
    json_out_t j;
    char reason[128];
    if (!server || !endpoint_ip || !endpoint_ip[0] ||
        !ifname || !ifname[0] || !out || out_cap < 2 ||
        !cfg_validate_server(server, reason, sizeof reason))
        return -1;
    j.out = out;
    j.cap = out_cap;
    j.len = 0;
    j.failed = 0;
    out[0] = '\0';
    append_raw(&j, "{\"log\":{\"loglevel\":\"warning\"},\"inbounds\":[{\"tag\":\"senko-tun\",\"protocol\":\"tun\",\"settings\":{\"name\":");
    append_string(&j, ifname);
    append_raw(&j, ",\"mtu\":1500},\"sniffing\":{\"enabled\":true,\"destOverride\":[\"http\",\"tls\",\"quic\"],\"routeOnly\":true}}],\"outbounds\":[");
    if (append_outbound(&j, server, endpoint_ip, ca_file) != 0) return -1;
    append_raw(&j, ",{\"tag\":\"direct\",\"protocol\":\"freedom\"},"
                   "{\"tag\":\"block\",\"protocol\":\"blackhole\"}]");
    append_routing(&j, rules, geo_dir);
    append_raw(&j, "}");
    return j.failed ? -1 : 0;
}

int senko_core_config_render(const vl_server_t *server, const char *endpoint_ip,
                     const char *ifname,
                     char *out, size_t out_cap) {
    return senko_core_config_render_rules(server, endpoint_ip, ifname, NULL, NULL, NULL, out, out_cap);
}
