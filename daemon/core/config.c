#include "config.h"
#include "b64.h"
#include "happ.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* small helpers */

static int is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hexnib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int copy_span(const char *s, const char *e, char *dst, size_t cap) {
    size_t n = (size_t)(e - s);
    if (n + 1 > cap) return -1;
    memcpy(dst, s, n);
    dst[n] = '\0';
    return 0;
}

static int copy_query_value(const char *v, size_t vlen, char *dst, size_t cap) {
    return url_percent_decode(v, vlen, dst, cap) >= 0 ? 0 : -1;
}

const char *vl_sec_name(vl_sec_t s) {
    switch (s) {
        case VL_SEC_NONE:    return "none";
        case VL_SEC_TLS:     return "tls";
        case VL_SEC_REALITY: return "reality";
        default:             return "unknown";
    }
}

static void cfg_reason(char *reason, size_t cap, const char *msg) {
    if (!reason || cap == 0) return;
    snprintf(reason, cap, "%s", msg ? msg : "unsupported server");
}

static int uuid_text_ok(const char *u) {
    if (!u) return 0;
    for (int i = 0; i < 36; ++i) {
        char c = u[i];
        if (c == '\0') return 0;
        int hy = (i == 8 || i == 13 || i == 18 || i == 23);
        if (hy) {
            if (c != '-') return 0;
            continue;
        }
        if (!is_hex(c)) return 0;
    }
    return u[36] == '\0';
}

static int sid_text_ok(const char *sid) {
    if (!sid || !sid[0]) return 1;
    size_t n = strlen(sid);
    if (n > 16 || (n % 2) != 0) return 0;
    for (size_t i = 0; i < n; ++i)
        if (!is_hex(sid[i])) return 0;
    return 1;
}

static int pbk_text_ok(const char *pbk) {
    unsigned char raw[32];
    size_t n = 0;
    return pbk && pbk[0] &&
        b64_decode(pbk, strlen(pbk), raw, sizeof raw, &n) == 0 &&
        n == sizeof raw;
}

int cfg_validate_server(const vl_server_t *s, char *reason, size_t reason_cap) {
    if (!s) {
        cfg_reason(reason, reason_cap, "empty server");
        return 0;
    }
    if (!s->host[0] || s->port == 0) {
        cfg_reason(reason, reason_cap, "missing host or port");
        return 0;
    }

    if (s->proto == VL_PROTO_SOCKS5 ||
        s->proto == VL_PROTO_HTTP ||
        s->proto == VL_PROTO_HTTPS)
        return 1;

    if (s->proto != VL_PROTO_VLESS) {
        cfg_reason(reason, reason_cap, "unsupported protocol");
        return 0;
    }
    if (!uuid_text_ok(s->uuid)) {
        cfg_reason(reason, reason_cap, "invalid uuid");
        return 0;
    }
    if (s->encryption[0] && strcmp(s->encryption, "none") != 0) {
        cfg_reason(reason, reason_cap, "unsupported encryption");
        return 0;
    }
    if (s->net != VL_NET_TCP && s->net != VL_NET_WS &&
        s->net != VL_NET_GRPC && s->net != VL_NET_XHTTP) {
        cfg_reason(reason, reason_cap, "unsupported transport type");
        return 0;
    }
    if (s->net == VL_NET_WS) {
        if (s->flow[0]) {
            cfg_reason(reason, reason_cap, "ws flow is unsupported");
            return 0;
        }
        if (s->security == VL_SEC_REALITY) {
            if (!pbk_text_ok(s->pbk)) {
                cfg_reason(reason, reason_cap, "reality requires valid pbk");
                return 0;
            }
            if (!sid_text_ok(s->sid)) {
                cfg_reason(reason, reason_cap, "reality sid must be hex <= 8 bytes");
                return 0;
            }
            return 1;
        }
        if (s->security != VL_SEC_NONE && s->security != VL_SEC_TLS) {
            cfg_reason(reason, reason_cap, "unsupported ws security");
            return 0;
        }
        return 1;
    }
    if (s->net == VL_NET_XHTTP) {
        if (s->flow[0]) {
            cfg_reason(reason, reason_cap, "xhttp does not use flow");
            return 0;
        }
        if (s->mode[0] &&
            strcmp(s->mode, "auto") &&
            strcmp(s->mode, "stream-one") &&
            strcmp(s->mode, "stream-up") &&
            strcmp(s->mode, "packet-up")) {
            cfg_reason(reason, reason_cap, "unsupported xhttp mode");
            return 0;
        }
        if (s->security == VL_SEC_NONE || s->security == VL_SEC_TLS)
            return 1;
        if (s->security == VL_SEC_REALITY) {
            if (!pbk_text_ok(s->pbk)) {
                cfg_reason(reason, reason_cap, "reality requires valid pbk");
                return 0;
            }
            if (!sid_text_ok(s->sid)) {
                cfg_reason(reason, reason_cap, "reality sid must be hex <= 8 bytes");
                return 0;
            }
            return 1;
        }
        cfg_reason(reason, reason_cap, "unsupported xhttp security");
        return 0;
    }

    if (s->net == VL_NET_GRPC) {
        if (s->flow[0]) {
            cfg_reason(reason, reason_cap, "grpc does not use flow");
            return 0;
        }
        if (s->security == VL_SEC_NONE || s->security == VL_SEC_TLS)
            return 1;
        if (s->security == VL_SEC_REALITY) {
            if (!pbk_text_ok(s->pbk)) {
                cfg_reason(reason, reason_cap, "reality requires valid pbk");
                return 0;
            }
            if (!sid_text_ok(s->sid)) {
                cfg_reason(reason, reason_cap, "reality sid must be hex <= 8 bytes");
                return 0;
            }
            return 1;
        }
        cfg_reason(reason, reason_cap, "unsupported grpc security");
        return 0;
    }

    if (s->security == VL_SEC_NONE)
        return 1;
    if (s->security == VL_SEC_TLS) {
        if (s->flow[0] && strcmp(s->flow, "xtls-rprx-vision") != 0) {
            cfg_reason(reason, reason_cap, "unsupported tls flow");
            return 0;
        }
        return 1;
    }
    if (s->security == VL_SEC_REALITY) {
/* empty flow = plain vless (durev and some panels); vision only when set */
        if (s->flow[0] && strcmp(s->flow, "xtls-rprx-vision") != 0) {
            cfg_reason(reason, reason_cap, "unsupported reality flow");
            return 0;
        }
        if (!pbk_text_ok(s->pbk)) {
            cfg_reason(reason, reason_cap, "reality requires valid pbk");
            return 0;
        }
        if (!sid_text_ok(s->sid)) {
            cfg_reason(reason, reason_cap, "reality sid must be hex <= 8 bytes");
            return 0;
        }
        return 1;
    }

    cfg_reason(reason, reason_cap, "unsupported security");
    return 0;
}

int cfg_validate_link(const char *uri, char *reason, size_t reason_cap) {
    vl_server_t s;
    cfg_status_t r = cfg_parse_link(uri, &s);
    if (r != CFG_OK) {
        cfg_reason(reason, reason_cap, "bad link syntax");
        return 0;
    }
    return cfg_validate_server(&s, reason, reason_cap);
}

int url_percent_decode(const char *src, size_t src_len, char *dst, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < src_len; ++i) {
        char c = src[i];
        if (c == '%' && i + 2 < src_len &&
            is_hex(src[i+1]) && is_hex(src[i+2])) {
            int hi = hexnib(src[i+1]);
            int lo = hexnib(src[i+2]);
            if (o + 1 >= cap) return -1;
            dst[o++] = (char)((hi << 4) | lo);
            i += 2;
        } else if (c == '+') {
/* '+' only means space in queries, so decoding it here is safe */
            if (o + 1 >= cap) return -1;
            dst[o++] = ' ';
        } else {
            if (o + 1 >= cap) return -1;
            dst[o++] = c;
        }
    }
    if (o >= cap) return -1;
    dst[o] = '\0';
    return (int)o;
}

static void assign_kv(vl_server_t *s,
                      const char *k, size_t klen,
                      const char *v, size_t vlen) {
    #define KEY_IS(lit) (klen == sizeof(lit) - 1 && memcmp(k, lit, klen) == 0)

    if (KEY_IS("security")) {
        char tmp[32];
        if (copy_span(v, v + vlen, tmp, sizeof tmp) == 0) {
            if      (strcmp(tmp, "reality") == 0) s->security = VL_SEC_REALITY;
            else if (strcmp(tmp, "tls")     == 0) s->security = VL_SEC_TLS;
            else if (strcmp(tmp, "none")    == 0) s->security = VL_SEC_NONE;
            else if (tmp[0] == '\0')              s->security = VL_SEC_NONE;
            else                                  s->security = VL_SEC_UNKNOWN;
        }
    } else if (KEY_IS("type")) {
        char tmp[32];
        if (copy_span(v, v + vlen, tmp, sizeof tmp) == 0) {
            if      (strcmp(tmp, "tcp")  == 0 ||
                     strcmp(tmp, "raw")  == 0) s->net = VL_NET_TCP;
            else if (strcmp(tmp, "ws")   == 0) s->net = VL_NET_WS;
            else if (strcmp(tmp, "grpc") == 0) s->net = VL_NET_GRPC;
            else if (strcmp(tmp, "http") == 0 || strcmp(tmp, "h2") == 0)
                                               s->net = VL_NET_HTTP;
            else if (strcmp(tmp, "xhttp") == 0 ||
                     strcmp(tmp, "splithttp") == 0)
                                               s->net = VL_NET_XHTTP;
            else                               s->net = VL_NET_UNKNOWN;
        }
    } else if (KEY_IS("sni") || KEY_IS("serverName")) {
        copy_query_value(v, vlen, s->sni, sizeof s->sni);
    } else if (KEY_IS("host")) {
/* ws links use this for the http host header, sni stays separate */
        copy_query_value(v, vlen, s->ws_host, sizeof s->ws_host);
    } else if (KEY_IS("flow")) {
        copy_query_value(v, vlen, s->flow, sizeof s->flow);
    } else if (KEY_IS("encryption")) {
        if (copy_query_value(v, vlen, s->encryption, sizeof s->encryption) != 0)
            snprintf(s->encryption, sizeof s->encryption, "%s", "unsupported");
    } else if (KEY_IS("fp")) {
        copy_query_value(v, vlen, s->fp, sizeof s->fp);
    } else if (KEY_IS("pbk")) {
        copy_query_value(v, vlen, s->pbk, sizeof s->pbk);
    } else if (KEY_IS("sid")) {
        copy_query_value(v, vlen, s->sid, sizeof s->sid);
    } else if (KEY_IS("path")) {
        url_percent_decode(v, vlen, s->path, sizeof s->path);
    } else if (KEY_IS("serviceName")) {
        url_percent_decode(v, vlen, s->path, sizeof s->path);
    } else if (KEY_IS("mode")) {
        copy_query_value(v, vlen, s->mode, sizeof s->mode);
    }

    #undef KEY_IS
}

static void parse_query(vl_server_t *s, const char *q, const char *end) {
    while (q < end) {
        const char *amp = memchr(q, '&', (size_t)(end - q));
        const char *seg_end = amp ? amp : end;
        const char *eq = memchr(q, '=', (size_t)(seg_end - q));
        if (eq) {
            assign_kv(s, q, (size_t)(eq - q), eq + 1, (size_t)(seg_end - eq - 1));
        }
        if (!amp) break;
        q = amp + 1;
    }
}

/* xray's old grpc links carry only serviceName; the wire path also contains Tun */
static void normalize_grpc_path(vl_server_t *s) {
    char base[sizeof s->path];
    size_t n;

    if (!s || s->net != VL_NET_GRPC) return;
    snprintf(base, sizeof base, "%s", s->path[0] ? s->path : "/");
    if (base[0] != '/') {
        size_t base_len = strlen(base);
        if (base_len + 1 >= sizeof base) return;
        memmove(base + 1, base, base_len + 1);
        base[0] = '/';
    }
    n = strlen(base);
    while (n > 1 && base[n - 1] == '/') base[--n] = '\0';
    if (n == 1 && base[0] == '/') {
        snprintf(s->path, sizeof s->path, "/Tun");
    } else if (n >= 4 && strcmp(base + n - 4, "/Tun") == 0) {
        snprintf(s->path, sizeof s->path, "%s", base);
    } else {
        snprintf(s->path, sizeof s->path, "%s/Tun", base);
    }
    snprintf(s->mode, sizeof s->mode, "grpc");
}

cfg_status_t cfg_parse_link(const char *uri, vl_server_t *out) {
    if (!uri || !out) return CFG_ERR_BAD_ARG;
    memset(out, 0, sizeof *out);

/* happ://crypt... unwraps to vless/socks/http before scheme match */
    if (strncmp(uri, "happ://", 7) == 0 || strncmp(uri, "HAPP://", 7) == 0) {
        char plain[8192];
        char line[1024];
        const char *p, *end, *nl;
        if (happ_unwrap(uri, plain, sizeof plain) != 0)
            return CFG_ERR_SCHEME;
/* single link */
        if (cfg_parse_link(plain, out) == CFG_OK)
            return CFG_OK;
/* multi-line body: first valid link */
        p = plain;
        end = plain + strlen(plain);
        while (p < end) {
            size_t llen;
            nl = memchr(p, '\n', (size_t)(end - p));
            llen = nl ? (size_t)(nl - p) : (size_t)(end - p);
            while (llen > 0 && (p[llen - 1] == '\r' || p[llen - 1] == ' '))
                llen--;
            if (llen > 0 && llen < sizeof line) {
                memcpy(line, p, llen);
                line[llen] = '\0';
                if (cfg_parse_link(line, out) == CFG_OK)
                    return CFG_OK;
            }
            if (!nl) break;
            p = nl + 1;
        }
        return CFG_ERR_SCHEME;
    }

    const char *p = uri;
    vl_proto_t proto = VL_PROTO_VLESS;
    size_t slen = 0;

    if (strncmp(p, "vless://", 8) == 0) {
        proto = VL_PROTO_VLESS;
        slen = 8;
    } else if (strncmp(p, "socks5://", 9) == 0) {
        proto = VL_PROTO_SOCKS5;
        slen = 9;
    } else if (strncmp(p, "http://", 7) == 0) {
        proto = VL_PROTO_HTTP;
        slen = 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        proto = VL_PROTO_HTTPS;
        slen = 8;
    } else {
        return CFG_ERR_SCHEME;
    }
    p += slen;
    out->proto = proto;

    const char *frag = strchr(p, '#');
    const char *body_end = frag ? frag : (p + strlen(p));

    const char *at = memchr(p, '@', (size_t)(body_end - p));
    if (at) {
        if (proto == VL_PROTO_VLESS) {
            if (copy_span(p, at, out->uuid, sizeof out->uuid) != 0) return CFG_ERR_TOO_LONG;
        } else {
            const char *colon = memchr(p, ':', (size_t)(at - p));
            if (colon) {
                if (copy_span(p, colon, out->user, sizeof out->user) != 0) return CFG_ERR_TOO_LONG;
                if (copy_span(colon + 1, at, out->pass, sizeof out->pass) != 0) return CFG_ERR_TOO_LONG;
            } else {
                if (copy_span(p, at, out->user, sizeof out->user) != 0) return CFG_ERR_TOO_LONG;
            }
        }
        p = at + 1;
    } else {
        if (proto == VL_PROTO_VLESS) return CFG_ERR_NO_AT;
    }

    const char *hostport = p;
    const char *qmark = memchr(hostport, '?', (size_t)(body_end - hostport));
    const char *hp_end = qmark ? qmark : body_end;

/* stop before '/' too so grpc and ws links do not spill into the port */
    const char *slash = memchr(hostport, '/', (size_t)(hp_end - hostport));
    if (slash) hp_end = slash;

    const char *colon = NULL;
    if (*hostport == '[') {
        const char *rb = memchr(hostport, ']', (size_t)(hp_end - hostport));
        if (!rb) return CFG_ERR_NO_HOST;
        if (copy_span(hostport + 1, rb, out->host, sizeof out->host) != 0)
            return CFG_ERR_TOO_LONG;
        if (rb + 1 < hp_end && rb[1] == ':') colon = rb + 1;
    } else {
        for (const char *c = hp_end - 1; c >= hostport; --c) {
            if (*c == ':') { colon = c; break; }
        }
        if (!colon) return CFG_ERR_BAD_PORT;
        if (copy_span(hostport, colon, out->host, sizeof out->host) != 0)
            return CFG_ERR_TOO_LONG;
    }
    if (out->host[0] == '\0') return CFG_ERR_NO_HOST;
    if (!colon) return CFG_ERR_BAD_PORT;

    unsigned long port = 0;
    const char *pp = colon + 1;
    if (pp >= hp_end) return CFG_ERR_BAD_PORT;
    for (; pp < hp_end; ++pp) {
        if (*pp < '0' || *pp > '9') return CFG_ERR_BAD_PORT;
        port = port * 10 + (unsigned long)(*pp - '0');
        if (port > 65535) return CFG_ERR_BAD_PORT;
    }
    if (port == 0) return CFG_ERR_BAD_PORT;
    out->port = (uint16_t)port;

    if (qmark) parse_query(out, qmark + 1, body_end);

    normalize_grpc_path(out);

    if (out->sni[0] == '\0' &&
        (out->security == VL_SEC_TLS || out->security == VL_SEC_REALITY))
        snprintf(out->sni, sizeof out->sni, "%s", out->host);

    if (out->net == VL_NET_WS && out->ws_host[0] == '\0') {
        if (out->sni[0])
            snprintf(out->ws_host, sizeof out->ws_host, "%s", out->sni);
        else
            snprintf(out->ws_host, sizeof out->ws_host, "%s", out->host);
    }

/* do not invent flow=vision: durev-style reality links omit flow and expect plain vless */
    if (out->security == VL_SEC_REALITY && out->net == VL_NET_UNKNOWN)
        out->net = VL_NET_TCP;

    if (frag) {
        url_percent_decode(frag + 1, strlen(frag + 1), out->remark, sizeof out->remark);
    }

    return CFG_OK;
}

static int looks_like_links(const char *b, size_t n) {
    for (size_t i = 0; i + 2 < n; ++i) {
        if (b[i] == ':' && b[i+1] == '/' && b[i+2] == '/') return 1;
    }
    return 0;
}

/* xray / v2rayn export bodies are json objects or arrays of full configs */
static int looks_like_json(const char *b, size_t n) {
    size_t i = 0;
    while (i < n && (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n'))
        ++i;
    if (i >= n) return 0;
    return b[i] == '{' || b[i] == '[';
}

static void parse_link_lines(const char *text, size_t len,
                             vl_server_t *out, size_t max, size_t *count) {
    const char *p = text;
    const char *end = text + len;
    char line[8192]; /* feeds often append long xhttp metadata to each link */

    while (p < end && *count < max) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl ? nl : end;
        const char *le = line_end;
        if (le > p && le[-1] == '\r') --le;

        size_t llen = (size_t)(le - p);
        if (llen > 0 && llen < sizeof line) {
            memcpy(line, p, llen);
            line[llen] = '\0';
/* expand happ lines into one or many nodes */
            if ((strncmp(line, "happ://", 7) == 0 ||
                 strncmp(line, "HAPP://", 7) == 0)) {
                char plain[8192];
                if (happ_unwrap(line, plain, sizeof plain) == 0)
                    parse_link_lines(plain, strlen(plain), out, max, count);
            } else if (cfg_parse_link(line, &out[*count]) == CFG_OK &&
                       cfg_validate_server(&out[*count], NULL, 0)) {
                (*count)++;
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
}

#include "third_party/cJSON.h"

static int jstr_copy(const cJSON *obj, const char *key, char *dst, size_t cap) {
    const cJSON *v;
    if (!obj || !key || !dst || cap == 0) return -1;
    v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring) return -1;
    snprintf(dst, cap, "%s", v->valuestring);
    return 0;
}

static int jstr_copy_any(const cJSON *obj, char *dst, size_t cap,
                         const char *k0, const char *k1) {
    if (jstr_copy(obj, k0, dst, cap) == 0) return 0;
    if (k1 && jstr_copy(obj, k1, dst, cap) == 0) return 0;
    return -1;
}

/* shortid may be a hex string or an array of candidates; keep the first */
static void j_copy_short_id(const cJSON *reality, char *dst, size_t cap) {
    const cJSON *sid;
    if (!reality || !dst || cap == 0) return;
    sid = cJSON_GetObjectItemCaseSensitive(reality, "shortId");
    if (cJSON_IsString(sid) && sid->valuestring) {
        snprintf(dst, cap, "%s", sid->valuestring);
        return;
    }
    if (cJSON_IsArray(sid) && cJSON_GetArraySize(sid) > 0) {
        const cJSON *first = cJSON_GetArrayItem(sid, 0);
        if (cJSON_IsString(first) && first->valuestring)
            snprintf(dst, cap, "%s", first->valuestring);
    }
}

static int j_port(const cJSON *obj, const char *key, uint16_t *out) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    double n;
    if (!out) return -1;
    if (cJSON_IsNumber(v)) {
        n = v->valuedouble;
    } else if (cJSON_IsString(v) && v->valuestring) {
        char *end = NULL;
        n = strtod(v->valuestring, &end);
        if (!end || end == v->valuestring) return -1;
    } else {
        return -1;
    }
    if (n < 1.0 || n > 65535.0) return -1;
    *out = (uint16_t)n;
    return 0;
}

static int count_proxy_outbounds(const cJSON *outbounds) {
    int n = 0;
    const cJSON *o;
    if (!cJSON_IsArray(outbounds)) return 0;
    cJSON_ArrayForEach(o, outbounds) {
        const cJSON *proto = cJSON_GetObjectItemCaseSensitive(o, "protocol");
        const char *p;
        if (!cJSON_IsString(proto) || !proto->valuestring) continue;
        p = proto->valuestring;
        if (strcmp(p, "freedom") == 0 || strcmp(p, "blackhole") == 0 ||
            strcmp(p, "dns") == 0 || strcmp(p, "loopback") == 0 ||
            strcmp(p, "direct") == 0 || strcmp(p, "block") == 0)
            continue;
        n++;
    }
    return n;
}

/* map one xray outbound object into vl_server_t; return 0 on accept */
static int xray_outbound_to_server(const cJSON *ob, const char *remarks,
                                   int multi_proxy, vl_server_t *s) {
    const cJSON *proto, *settings, *stream, *vnext, *user, *netj, *secj;
    const char *network = "tcp";
    const char *security = "none";
    char tag[128];

    if (!ob || !s) return -1;
    memset(s, 0, sizeof *s);
    s->proto = VL_PROTO_VLESS;

    proto = cJSON_GetObjectItemCaseSensitive(ob, "protocol");
    if (!cJSON_IsString(proto) || !proto->valuestring) return -1;
    if (strcmp(proto->valuestring, "vless") != 0) return -1;

    settings = cJSON_GetObjectItemCaseSensitive(ob, "settings");
    if (!cJSON_IsObject(settings)) return -1;
    vnext = cJSON_GetObjectItemCaseSensitive(settings, "vnext");
    if (!cJSON_IsArray(vnext) || cJSON_GetArraySize(vnext) < 1) return -1;
    vnext = cJSON_GetArrayItem(vnext, 0);
    if (!cJSON_IsObject(vnext)) return -1;

    if (jstr_copy(vnext, "address", s->host, sizeof s->host) != 0) return -1;
    if (j_port(vnext, "port", &s->port) != 0) return -1;

    user = cJSON_GetObjectItemCaseSensitive(vnext, "users");
    if (!cJSON_IsArray(user) || cJSON_GetArraySize(user) < 1) return -1;
    user = cJSON_GetArrayItem(user, 0);
    if (!cJSON_IsObject(user)) return -1;
    if (jstr_copy(user, "id", s->uuid, sizeof s->uuid) != 0) return -1;
    (void)jstr_copy(user, "flow", s->flow, sizeof s->flow);
    if (jstr_copy(user, "encryption", s->encryption, sizeof s->encryption) != 0)
        snprintf(s->encryption, sizeof s->encryption, "%s", "none");

    stream = cJSON_GetObjectItemCaseSensitive(ob, "streamSettings");
    if (cJSON_IsObject(stream)) {
        netj = cJSON_GetObjectItemCaseSensitive(stream, "network");
        if (cJSON_IsString(netj) && netj->valuestring) network = netj->valuestring;
        secj = cJSON_GetObjectItemCaseSensitive(stream, "security");
        if (cJSON_IsString(secj) && secj->valuestring) security = secj->valuestring;
    }

    if (strcmp(network, "tcp") == 0 || strcmp(network, "raw") == 0)
        s->net = VL_NET_TCP;
    else if (strcmp(network, "ws") == 0 || strcmp(network, "websocket") == 0)
        s->net = VL_NET_WS;
    else if (strcmp(network, "xhttp") == 0 || strcmp(network, "splithttp") == 0)
        s->net = VL_NET_XHTTP;
    else if (strcmp(network, "grpc") == 0)
        s->net = VL_NET_GRPC;
    else if (strcmp(network, "http") == 0 || strcmp(network, "h2") == 0 ||
             strcmp(network, "httpupgrade") == 0)
        s->net = VL_NET_HTTP;
    else
        s->net = VL_NET_UNKNOWN;

    if (strcmp(security, "reality") == 0)
        s->security = VL_SEC_REALITY;
    else if (strcmp(security, "tls") == 0)
        s->security = VL_SEC_TLS;
    else if (strcmp(security, "none") == 0 || security[0] == '\0')
        s->security = VL_SEC_NONE;
    else
        s->security = VL_SEC_UNKNOWN;

    if (cJSON_IsObject(stream)) {
        const cJSON *rs = cJSON_GetObjectItemCaseSensitive(stream, "realitySettings");
        const cJSON *ts = cJSON_GetObjectItemCaseSensitive(stream, "tlsSettings");
        const cJSON *ws = cJSON_GetObjectItemCaseSensitive(stream, "wsSettings");
        const cJSON *xh = cJSON_GetObjectItemCaseSensitive(stream, "xhttpSettings");
        const cJSON *gr = cJSON_GetObjectItemCaseSensitive(stream, "grpcSettings");
        if (!cJSON_IsObject(xh))
            xh = cJSON_GetObjectItemCaseSensitive(stream, "splithttpSettings");

        if (s->security == VL_SEC_REALITY && cJSON_IsObject(rs)) {
            (void)jstr_copy(rs, "publicKey", s->pbk, sizeof s->pbk);
            j_copy_short_id(rs, s->sid, sizeof s->sid);
            (void)jstr_copy_any(rs, s->sni, sizeof s->sni, "serverName", "server_name");
            (void)jstr_copy_any(rs, s->fp, sizeof s->fp, "fingerprint", "fp");
        }
        if ((s->security == VL_SEC_TLS || s->sni[0] == '\0') && cJSON_IsObject(ts)) {
            if (s->sni[0] == '\0')
                (void)jstr_copy_any(ts, s->sni, sizeof s->sni, "serverName", "server_name");
            if (s->fp[0] == '\0')
                (void)jstr_copy_any(ts, s->fp, sizeof s->fp, "fingerprint", "fp");
        }
        if (s->net == VL_NET_WS && cJSON_IsObject(ws)) {
            (void)jstr_copy(ws, "path", s->path, sizeof s->path);
            {
                const cJSON *hdr = cJSON_GetObjectItemCaseSensitive(ws, "headers");
                if (cJSON_IsObject(hdr))
                    (void)jstr_copy_any(hdr, s->ws_host, sizeof s->ws_host, "Host", "host");
            }
            if (s->ws_host[0] == '\0')
                (void)jstr_copy(ws, "host", s->ws_host, sizeof s->ws_host);
        }
        if (s->net == VL_NET_XHTTP && cJSON_IsObject(xh)) {
            (void)jstr_copy(xh, "path", s->path, sizeof s->path);
            (void)jstr_copy(xh, "mode", s->mode, sizeof s->mode);
            (void)jstr_copy(xh, "host", s->ws_host, sizeof s->ws_host);
        }
        if (s->net == VL_NET_GRPC && cJSON_IsObject(gr))
            (void)jstr_copy(gr, "serviceName", s->path, sizeof s->path);
    }

    normalize_grpc_path(s);

    if (s->sni[0] == '\0' &&
        (s->security == VL_SEC_TLS || s->security == VL_SEC_REALITY))
        snprintf(s->sni, sizeof s->sni, "%s", s->host);

    if (s->net == VL_NET_WS && s->ws_host[0] == '\0') {
        if (s->sni[0])
            snprintf(s->ws_host, sizeof s->ws_host, "%s", s->sni);
        else
            snprintf(s->ws_host, sizeof s->ws_host, "%s", s->host);
    }

    tag[0] = '\0';
    (void)jstr_copy(ob, "tag", tag, sizeof tag);
    if (remarks && remarks[0] && multi_proxy) {
/* keep host suffix so multi-outbound groups stay distinguishable */
        size_t rl = strlen(remarks);
        if (rl > 80) rl = 80;
        snprintf(s->remark, sizeof s->remark, "%.*s · %.40s",
                 (int)rl, remarks, s->host);
    } else if (remarks && remarks[0]) {
        snprintf(s->remark, sizeof s->remark, "%s", remarks);
    } else if (tag[0]) {
        snprintf(s->remark, sizeof s->remark, "%s", tag);
    } else {
        snprintf(s->remark, sizeof s->remark, "%.120s:%u",
                 s->host, (unsigned)s->port);
    }

    if (!cfg_validate_server(s, NULL, 0)) return -1;
    return 0;
}

static void xray_collect_outbounds(const cJSON *outbounds, const char *remarks,
                                   vl_server_t *out, size_t max, size_t *count) {
    const cJSON *ob;
    int multi;
    if (!cJSON_IsArray(outbounds) || !out || !count) return;
    multi = count_proxy_outbounds(outbounds) > 1;
    cJSON_ArrayForEach(ob, outbounds) {
        if (*count >= max) return;
        if (xray_outbound_to_server(ob, remarks, multi, &out[*count]) == 0)
            (*count)++;
    }
}

/* accept: [ { remarks, outbounds: [...] } */
static int parse_xray_json(const char *blob, size_t blob_len,
                           vl_server_t *out, size_t max, size_t *count) {
    cJSON *root;
    if (!blob || !out || !count || max == 0) return -1;

    root = cJSON_ParseWithLength(blob, blob_len);
    if (!root) return -1;

    if (cJSON_IsArray(root)) {
        const cJSON *item;
        cJSON_ArrayForEach(item, root) {
            const cJSON *outbounds;
            char remarks[128];
            if (*count >= max) break;
            if (!cJSON_IsObject(item)) continue;
            remarks[0] = '\0';
            (void)jstr_copy(item, "remarks", remarks, sizeof remarks);
            outbounds = cJSON_GetObjectItemCaseSensitive(item, "outbounds");
            if (cJSON_IsArray(outbounds))
                xray_collect_outbounds(outbounds, remarks, out, max, count);
        }
    } else if (cJSON_IsObject(root)) {
        const cJSON *outbounds = cJSON_GetObjectItemCaseSensitive(root, "outbounds");
        char remarks[128];
        remarks[0] = '\0';
        (void)jstr_copy(root, "remarks", remarks, sizeof remarks);
        if (cJSON_IsArray(outbounds))
            xray_collect_outbounds(outbounds, remarks, out, max, count);
    }

    cJSON_Delete(root);
    return 0;
}

cfg_status_t cfg_parse_subscription(const char *blob, size_t blob_len,
                                    vl_server_t *out, size_t max_servers,
                                    size_t *out_count) {
    if (!blob || !out || !out_count || max_servers == 0) return CFG_ERR_BAD_ARG;
    *out_count = 0;

/* whole-blob happ deep link (export as single line) */
    if (blob_len >= 7 &&
        (strncmp(blob, "happ://", 7) == 0 || strncmp(blob, "HAPP://", 7) == 0)) {
        char plain[16384];
        char tmp[8192];
        size_t n = blob_len < sizeof tmp - 1 ? blob_len : sizeof tmp - 1;
        memcpy(tmp, blob, n);
        tmp[n] = '\0';
        if (happ_unwrap(tmp, plain, sizeof plain) == 0) {
            return cfg_parse_subscription(plain, strlen(plain),
                                          out, max_servers, out_count);
        }
        return CFG_ERR_SCHEME;
    }

/* json first: bodies contain https:// dns urls that would trip link detection */
    if (looks_like_json(blob, blob_len)) {
        if (parse_xray_json(blob, blob_len, out, max_servers, out_count) == 0)
            return CFG_OK;
/* fall through if the payload only looked like json */
    }

    if (looks_like_links(blob, blob_len)) {
        parse_link_lines(blob, blob_len, out, max_servers, out_count);
        return CFG_OK;
    }

    size_t cap = b64_decoded_maxlen(blob_len);
    if (cap > (size_t)(2 * 1024 * 1024)) return CFG_ERR_TOO_LONG;

    unsigned char *scratch = (unsigned char *)malloc(cap ? cap : 1);
    if (!scratch) return CFG_ERR_NO_MEMORY;
    size_t dec_len = 0;
    if (b64_decode(blob, blob_len, scratch, cap, &dec_len) != 0) {
        free(scratch);
        return CFG_OK;
    }
    if (looks_like_json((const char *)scratch, dec_len)) {
        (void)parse_xray_json((const char *)scratch, dec_len, out, max_servers, out_count);
        free(scratch);
        return CFG_OK;
    }
    parse_link_lines((const char *)scratch, dec_len, out, max_servers, out_count);
    free(scratch);
    return CFG_OK;
}
