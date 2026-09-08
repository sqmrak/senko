#include "go_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static int append_stream(json_out_t *j, const vl_server_t *s) {
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
                           const char *endpoint_ip) {
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
        if (append_stream(j, s) != 0) return -1;
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
        append_raw(j, "}}");
    }
    append_raw(j, "}");
    return j->failed ? -1 : 0;
}

int go_config_render(const vl_server_t *server, const char *endpoint_ip,
                     const char *ifname,
                     char *out, size_t out_cap) {
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
    if (append_outbound(&j, server, endpoint_ip) != 0) return -1;
    append_raw(&j, "]}");
    return j.failed ? -1 : 0;
}
