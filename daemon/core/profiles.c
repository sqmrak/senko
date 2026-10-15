#include "profiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/* one entry is a flat key/value table: nested yaml maps are flattened into
   parent.child so the field lookup below stays a single string compare */
#define PROF_MAX_PAIRS 40
#define PROF_KEY_MAX   64
#define PROF_VAL_MAX   256

typedef struct {
    char key[PROF_KEY_MAX];
    char value[PROF_VAL_MAX];
} prof_pair_t;

typedef struct {
    prof_pair_t pairs[PROF_MAX_PAIRS];
    size_t n;
} prof_entry_t;

static int ci_equal(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

static void trim_span(const char **start, const char **end) {
    while (*start < *end && (**start == ' ' || **start == '\t')) ++(*start);
    while (*end > *start) {
        char c = (*end)[-1];
        if (c != ' ' && c != '\t' && c != '\r') break;
        --(*end);
    }
}

/* yaml scalars are commonly quoted, and panels quote every name that carries a
   flag emoji or a colon */
static void unquote(const char **start, const char **end) {
    if (*end - *start >= 2) {
        char q = **start;
        if ((q == '"' || q == '\'') && (*end)[-1] == q) {
            ++(*start);
            --(*end);
        }
    }
}

static void copy_span(char *dst, size_t cap, const char *start, const char *end) {
    size_t n = (size_t)(end - start);
    if (n >= cap) n = cap - 1;
    memcpy(dst, start, n);
    dst[n] = '\0';
}

static void entry_reset(prof_entry_t *e) { e->n = 0; }

static void entry_put(prof_entry_t *e, const char *prefix,
                      const char *kstart, const char *kend,
                      const char *vstart, const char *vend) {
    if (e->n >= PROF_MAX_PAIRS) return;
    trim_span(&kstart, &kend);
    trim_span(&vstart, &vend);
    unquote(&kstart, &kend);
    unquote(&vstart, &vend);
    if (kend <= kstart) return;
    prof_pair_t *p = &e->pairs[e->n];
    if (prefix && prefix[0]) {
        size_t pl = strlen(prefix);
        if (pl + 1 >= sizeof p->key) return;
        memcpy(p->key, prefix, pl);
        p->key[pl] = '.';
        copy_span(p->key + pl + 1, sizeof p->key - pl - 1, kstart, kend);
    } else {
        copy_span(p->key, sizeof p->key, kstart, kend);
    }
    copy_span(p->value, sizeof p->value, vstart, vend);
    e->n++;
}

static void entry_put_span(prof_entry_t *e, const char *key,
                           const char *vstart, const char *vend) {
    if (!key) return;
    entry_put(e, NULL, key, key + strlen(key), vstart, vend);
}

static void entry_put_str(prof_entry_t *e, const char *key, const char *value) {
    if (!key || !value) return;
    entry_put(e, NULL, key, key + strlen(key), value, value + strlen(value));
}

static const char *entry_get(const prof_entry_t *e, const char *key) {
    for (size_t i = 0; i < e->n; ++i)
        if (ci_equal(e->pairs[i].key, key)) return e->pairs[i].value;
    return NULL;
}

static const char *entry_get_any(const prof_entry_t *e, const char *a, const char *b) {
    const char *v = entry_get(e, a);
    return v ? v : entry_get(e, b);
}

static int truthy(const char *v) {
    return v && (ci_equal(v, "true") || ci_equal(v, "1") || ci_equal(v, "yes") ||
                 ci_equal(v, "on"));
}

static int parse_port(const char *v, uint16_t *out) {
    if (!v || !*v) return -1;
    char *end = NULL;
    long p = strtol(v, &end, 10);
    if (!end || *end != '\0' || p < 1 || p > 65535) return -1;
    *out = (uint16_t)p;
    return 0;
}

static void set_why(char *why, size_t cap, const char *text) {
    if (why && cap) snprintf(why, cap, "%s", text);
}

/* clash's "network": "http" there is tcp behind the http header disguise
   (http-opts), http/2 is "h2" */
static int clash_transport(const prof_entry_t *e, vl_server_t *s, char *why, size_t why_cap) {
    const char *net = entry_get(e, "network");
    if (!net || !net[0] || ci_equal(net, "none")) net = "tcp";
    if (ci_equal(net, "http")) return cfg_node_transport(s, "tcp", "http", why, why_cap);
    if (cfg_node_transport(s, net, NULL, why, why_cap) != 0) return -1;
    if (s->net == VL_NET_WS) {
        const char *path = entry_get(e, "ws-opts.path");
        const char *whost = entry_get_any(e, "ws-opts.headers.Host", "ws-opts.headers.host");
        if (path) snprintf(s->path, sizeof s->path, "%s", path);
        if (whost && whost[0]) snprintf(s->ws_host, sizeof s->ws_host, "%s", whost);
    } else if (s->net == VL_NET_GRPC) {
        const char *svc = entry_get_any(e, "grpc-opts.grpc-service-name", "grpc-opts.serviceName");
        if (svc) snprintf(s->path, sizeof s->path, "%s", svc);
    }
    return 0;
}

/* -1 with why naming what senko cannot carry, or that the entry is broken */
static int entry_to_server(const prof_entry_t *e, vl_server_t *s, char *why, size_t why_cap) {
    const char *type = entry_get(e, "type");
    const char *host = entry_get_any(e, "server", "host");
    const char *port = entry_get(e, "port");
    const char *name = entry_get(e, "name");

    set_why(why, why_cap, "malformed profile entry");
    if (!type || !host || !host[0] || !port) return -1;
    memset(s, 0, sizeof *s);
    if (parse_port(port, &s->port) != 0) return -1;
    if (truthy(entry_get(e, "skip-cert-verify"))) s->insecure = 1;
    snprintf(s->host, sizeof s->host, "%s", host);
    if (name) snprintf(s->remark, sizeof s->remark, "%s", name);

    if (ci_equal(type, "vless") || ci_equal(type, "vmess")) {
        const char *uuid = entry_get_any(e, "uuid", "id");
        if (!uuid || !uuid[0]) return -1;
        cfg_node_vless_id(s, uuid);
        if (ci_equal(type, "vmess")) {
            const char *cipher = entry_get(e, "cipher");
            s->proto = VL_PROTO_VMESS;
            snprintf(s->encryption, sizeof s->encryption, "%s",
                     cipher && cipher[0] ? cipher : "auto");
        } else {
            const char *flow = entry_get(e, "flow");
            s->proto = VL_PROTO_VLESS;
            snprintf(s->encryption, sizeof s->encryption, "none");
            if (flow) snprintf(s->flow, sizeof s->flow, "%s", flow);
        }
        if (clash_transport(e, s, why, why_cap) != 0) return -1;

        {
            const char *pbk = entry_get_any(e, "reality-opts.public-key",
                                            "reality-opts.publicKey");
            const char *sid = entry_get_any(e, "reality-opts.short-id",
                                            "reality-opts.shortId");
            if (pbk && pbk[0]) {
                s->security = VL_SEC_REALITY;
                snprintf(s->pbk, sizeof s->pbk, "%s", pbk);
                if (sid) snprintf(s->sid, sizeof s->sid, "%s", sid);
            } else if (truthy(entry_get(e, "tls"))) {
                s->security = VL_SEC_TLS;
            } else {
                s->security = VL_SEC_NONE;
            }
        }
        {
            const char *sni = entry_get_any(e, "servername", "sni");
            if (sni && sni[0]) snprintf(s->sni, sizeof s->sni, "%s", sni);
            const char *fp = entry_get_any(e, "client-fingerprint", "fingerprint");
            if (fp && fp[0]) snprintf(s->fp, sizeof s->fp, "%s", fp);
        }
    } else if (ci_equal(type, "socks5") || ci_equal(type, "socks")) {
        s->proto = VL_PROTO_SOCKS5;
        s->net = VL_NET_TCP;
        if (truthy(entry_get(e, "tls"))) {
            set_why(why, why_cap, "socks over tls");
            return -1;
        }
        {
            const char *u = entry_get_any(e, "username", "user");
            const char *p = entry_get(e, "password");
            if (u) snprintf(s->user, sizeof s->user, "%s", u);
            if (p) snprintf(s->pass, sizeof s->pass, "%s", p);
        }
    } else if (ci_equal(type, "http") || ci_equal(type, "https")) {
        int secure = ci_equal(type, "https") || truthy(entry_get(e, "tls"));
        s->proto = secure ? VL_PROTO_HTTPS : VL_PROTO_HTTP;
        s->net = VL_NET_TCP;
        s->security = secure ? VL_SEC_TLS : VL_SEC_NONE;
        {
            const char *u = entry_get_any(e, "username", "user");
            const char *p = entry_get(e, "password");
            if (u) snprintf(s->user, sizeof s->user, "%s", u);
            if (p) snprintf(s->pass, sizeof s->pass, "%s", p);
        }
    } else if (ci_equal(type, "trojan")) {
        s->proto = VL_PROTO_TROJAN;
        s->security = VL_SEC_TLS;
        s->net = VL_NET_TCP;
        const char *p = entry_get(e, "password");
        if (p) snprintf(s->pass, sizeof s->pass, "%s", p);
        const char *sni = entry_get_any(e, "sni", "server-name");
        if (sni) snprintf(s->sni, sizeof s->sni, "%s", sni);
        const char *fp = entry_get_any(e, "client-fingerprint", "fingerprint");
        if (fp && fp[0]) snprintf(s->fp, sizeof s->fp, "%s", fp);
        if (entry_get(e, "reality-opts.public-key")) {
            set_why(why, why_cap, "trojan over reality");
            return -1;
        }
        if (clash_transport(e, s, why, why_cap) != 0) return -1;
    } else if (ci_equal(type, "ss") || ci_equal(type, "shadowsocks")) {
        s->proto = VL_PROTO_SHADOWSOCKS;
        s->security = VL_SEC_NONE;
        s->net = VL_NET_TCP;
        const char *p = entry_get(e, "password");
        const char *c = entry_get(e, "cipher");
        const char *plugin = entry_get(e, "plugin");
        if (p) snprintf(s->pass, sizeof s->pass, "%s", p);
        if (c) cfg_node_ss_method(s, c);
        if (plugin && plugin[0]) {
            char text[64];
            snprintf(text, sizeof text, "shadowsocks plugin %.32s", plugin);
            set_why(why, why_cap, text);
            return -1;
        }
    } else if (ci_equal(type, "hysteria2")) {
/* hysteria1 ("hysteria") is a different, incompatible handshake: only the
   type spelled exactly "hysteria2" reaches senko-core's hysteria client */
        const char *pass = entry_get_any(e, "password", "auth");
        if (!pass || !pass[0]) {
            set_why(why, why_cap, "hysteria2 requires an auth password");
            return -1;
        }
        s->proto = VL_PROTO_HYSTERIA2;
        s->security = VL_SEC_TLS;
        s->net = VL_NET_TCP;
        snprintf(s->pass, sizeof s->pass, "%s", pass);
        {
            const char *sni = entry_get_any(e, "sni", "servername");
            if (sni && sni[0]) snprintf(s->sni, sizeof s->sni, "%s", sni);
        }
        {
            const char *obfs = entry_get(e, "obfs");
            if (obfs && obfs[0] && !ci_equal(obfs, "none")) {
                const char *op = entry_get_any(e, "obfs-password", "obfs_password");
                snprintf(s->obfs, sizeof s->obfs, "%s", obfs);
                if (op) snprintf(s->obfs_password, sizeof s->obfs_password, "%s", op);
            }
        }
        {
/* clash-meta and surge both spell the port hop range "ports"; surge also
   accepts "mport" */
            const char *ports = entry_get_any(e, "ports", "mport");
            if (ports && ports[0])
                (void)cfg_parse_port_hop(ports, strlen(ports), s->port_hop,
                                         sizeof s->port_hop, NULL);
        }
    } else {
        /* tuic, hysteria v1 and the rest have no senko client */
        char text[32];
        size_t i = 0;
        for (; type[i] && i + 1 < sizeof text; ++i) text[i] = (char)tolower((unsigned char)type[i]);
        text[i] = '\0';
        set_why(why, why_cap, text);
        return -1;
    }

    cfg_node_finish(s);
    if (s->remark[0] == '\0')
        snprintf(s->remark, sizeof s->remark, "%.120s:%u", s->host, (unsigned)s->port);
    return cfg_validate_server(s, why, why_cap) ? 0 : -1;
}

/* one entry into out[*count], or one skip counted by its reason */
static void take_entry(const prof_entry_t *e, vl_server_t *out, size_t max, size_t *count,
                       cfg_import_stats_t *st) {
    char why[96];
    if (*count >= max) return;
    /* a refused entry's placeholder reads its port from this slot */
    memset(&out[*count], 0, sizeof out[*count]);
    if (entry_to_server(e, &out[*count], why, sizeof why) == 0) (*count)++;
    else cfg_import_skip(st, out, max, count, entry_get(e, "name"),
                         entry_get_any(e, "server", "host"), out[*count].port, why);
}

/* ---- clash yaml -------------------------------------------------------- */

static const char *line_end_of(const char *p, const char *end) {
    const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
    return nl ? nl : end;
}

static size_t indent_of(const char *p, const char *le) {
    size_t n = 0;
    while (p + n < le && p[n] == ' ') ++n;
    return n;
}

static int line_is_blank(const char *p, const char *le) {
    for (const char *q = p; q < le; ++q)
        if (*q != ' ' && *q != '\t' && *q != '\r') return *q == '#';
    return 1;
}

int profiles_looks_like_clash(const char *blob, size_t len) {
    if (!blob || len == 0) return 0;
    const char *end = blob + len;
    for (const char *p = blob; p < end; ) {
        const char *le = line_end_of(p, end);
        if (indent_of(p, le) == 0 &&
            (size_t)(le - p) >= 8 && memcmp(p, "proxies:", 8) == 0)
            return 1;
        p = (le < end) ? le + 1 : end;
    }
    return 0;
}

/* split one flow mapping body (already inside the braces) into pairs */
static void clash_flow_pairs(prof_entry_t *e, const char *p, const char *end) {
    int depth = 0;
    const char *seg = p;
    for (const char *q = p; q <= end; ++q) {
        if (q < end && (*q == '{' || *q == '[')) { ++depth; continue; }
        if (q < end && (*q == '}' || *q == ']')) { if (depth > 0) --depth; continue; }
        if (q < end && (*q != ',' || depth > 0)) continue;
        const char *colon = NULL;
        for (const char *r = seg; r < q; ++r) {
            if (*r == ':') { colon = r; break; }
        }
        if (colon) entry_put(e, NULL, seg, colon, colon + 1, q);
        seg = q + 1;
    }
}

size_t profiles_parse_clash(const char *blob, size_t len,
                            vl_server_t *out, size_t max) {
    return profiles_parse_clash_ex(blob, len, out, max, NULL);
}

size_t profiles_parse_clash_ex(const char *blob, size_t len, vl_server_t *out,
                               size_t max, cfg_import_stats_t *st) {
    if (!blob || !out || max == 0) return 0;
    const char *end = blob + len;
    const char *p = blob;
    size_t count = 0;

    while (p < end) {
        const char *le = line_end_of(p, end);
        if (indent_of(p, le) == 0 &&
            (size_t)(le - p) >= 8 && memcmp(p, "proxies:", 8) == 0) {
            p = (le < end) ? le + 1 : end;
            break;
        }
        p = (le < end) ? le + 1 : end;
    }
    if (p >= end) return 0;

    prof_entry_t entry;
    entry_reset(&entry);
    int have_entry = 0;
    size_t item_indent = 0;
    char nest[PROF_KEY_MAX];
    nest[0] = '\0';
    size_t nest_indent = 0;

    while (p < end) {
        const char *le = line_end_of(p, end);
        const char *next = (le < end) ? le + 1 : end;
        if (line_is_blank(p, le)) { p = next; continue; }
        size_t ind = indent_of(p, le);
        const char *body = p + ind;

/* a key at column zero ends the proxies block */
        if (ind == 0 && *body != '-') break;

/* a dash deeper than the items is an element of a nested list such as
   "alpn:\n  - h3"; taking it for a new item split the proxy in two */
        if (*body == '-' && have_entry && ind > item_indent) {
            p = next;
            continue;
        }

        if (*body == '-') {
            if (have_entry) take_entry(&entry, out, max, &count, st);
            if (count >= max) return count;
            entry_reset(&entry);
            have_entry = 1;
            item_indent = ind;
            nest[0] = '\0';
            nest_indent = 0;

            const char *v = body + 1;
            while (v < le && (*v == ' ' || *v == '\t')) ++v;
            if (v < le && *v == '{') {
                const char *close = le;
                while (close > v && close[-1] != '}') --close;
                if (close > v) clash_flow_pairs(&entry, v + 1, close - 1);
            } else if (v < le) {
                const char *colon = (const char *)memchr(v, ':', (size_t)(le - v));
                const char *vs = colon ? colon + 1 : le, *ve = le;
                trim_span(&vs, &ve);
                if (colon && ve == vs) {
                    const char *ks = v, *ke = colon;
                    trim_span(&ks, &ke);
                    unquote(&ks, &ke);
                    copy_span(nest, sizeof nest, ks, ke);
                    nest_indent = (size_t)(v - p);
                } else if (colon) {
                    entry_put(&entry, NULL, v, colon, colon + 1, le);
                }
            }
            p = next;
            continue;
        }

        if (!have_entry || ind <= item_indent) break;

        {
            const char *colon = (const char *)memchr(body, ':', (size_t)(le - body));
            if (!colon) { p = next; continue; }
            const char *vs = colon + 1;
            const char *ve = le;
            trim_span(&vs, &ve);
            if (nest[0] && ind <= nest_indent) nest[0] = '\0';
            if (ve == vs) {
/* a key with no scalar opens a nested map such as ws-opts: */
                const char *ks = body, *ke = colon;
                trim_span(&ks, &ke);
                unquote(&ks, &ke);
                if (nest[0]) {
                    char joined[PROF_KEY_MAX];
                    size_t nl = strlen(nest);
                    if (nl + 1 < sizeof joined) {
                        memcpy(joined, nest, nl);
                        joined[nl] = '.';
                        copy_span(joined + nl + 1, sizeof joined - nl - 1, ks, ke);
                        memcpy(nest, joined, sizeof nest);
                    }
                } else {
                    copy_span(nest, sizeof nest, ks, ke);
                    nest_indent = ind;
                }
            } else {
                entry_put(&entry, nest, body, colon, vs, ve);
            }
        }
        p = next;
    }

    if (have_entry) take_entry(&entry, out, max, &count, st);
    return count;
}

/* ---- shadowrocket / surge ini ------------------------------------------ */

int profiles_looks_like_surge(const char *blob, size_t len) {
    if (!blob || len == 0) return 0;
    const char *end = blob + len;
    for (const char *p = blob; p < end; ) {
        const char *le = line_end_of(p, end);
        const char *b = p;
        while (b < le && (*b == ' ' || *b == '\t')) ++b;
        if ((size_t)(le - b) >= 7 && memcmp(b, "[Proxy]", 7) == 0) return 1;
        p = (le < end) ? le + 1 : end;
    }
    return 0;
}

/* surge writes: Name = type, host, port, [user], [pass], key=value, ... */
static int surge_line_to_server(const char *name_s, const char *name_e,
                                const char *body_s, const char *body_e,
                                vl_server_t *s, char *why, size_t why_cap) {
    prof_entry_t e;
    entry_reset(&e);

    const char *fields[16];
    size_t field_n = 0;
    const char *seg = body_s;
    for (const char *q = body_s; q <= body_e && field_n < 16; ++q) {
        if (q < body_e && *q != ',') continue;
        fields[field_n++] = seg;
        if (q >= body_e) break;
        seg = q + 1;
    }
    if (field_n < 3) return -1;

    const char *ends[16];
    for (size_t i = 0; i < field_n; ++i)
        ends[i] = (i + 1 < field_n) ? fields[i + 1] - 1 : body_e;

    entry_put_span(&e, "name", name_s, name_e);
    entry_put_span(&e, "type", fields[0], ends[0]);
    entry_put_span(&e, "server", fields[1], ends[1]);
    entry_put_span(&e, "port", fields[2], ends[2]);


/* positional user and password come before the first key=value field */
    size_t positional = 3;
    for (size_t i = 3; i < field_n; ++i) {
        const char *fs = fields[i], *fe = ends[i];
        trim_span(&fs, &fe);
        const char *eq = NULL;
        for (const char *r = fs; r < fe; ++r) {
            if (*r == '=') { eq = r; break; }
        }
        if (eq) {
            entry_put(&e, NULL, fs, eq, eq + 1, fe);
            continue;
        }
        if (positional == 3) entry_put_span(&e, "username", fs, fe);
        else if (positional == 4) entry_put_span(&e, "password", fs, fe);
        ++positional;
    }

/* surge names the vmess id username= and its cipher encrypt-method=, while
   shadowrocket writes the cipher and the id as the two positional fields; ws
   is a flag with its own path and "Host:name" header fields in surge */
    const char *kind = entry_get(&e, "type");
    if (kind && ci_equal(kind, "vmess")) {
        const char *user = entry_get(&e, "username");
        const char *pw = entry_get(&e, "password");
        const char *method = entry_get(&e, "encrypt-method");
        if (!entry_get(&e, "uuid") && !(pw && pw[0]) && user && user[0])
            entry_put_str(&e, "uuid", user);
        if (!entry_get(&e, "cipher")) {
            if (method && method[0]) entry_put_str(&e, "cipher", method);
            else if (pw && pw[0] && user && user[0]) entry_put_str(&e, "cipher", user);
        }
        if (truthy(entry_get(&e, "ws"))) {
            const char *path = entry_get(&e, "ws-path");
            const char *headers = entry_get(&e, "ws-headers");
            entry_put_str(&e, "network", "ws");
            if (path) entry_put_str(&e, "ws-opts.path", path);
            if (headers && (strncmp(headers, "Host:", 5) == 0 || strncmp(headers, "host:", 5) == 0)) {
                const char *h = headers + 5;
                const char *he = h + strcspn(h, "|");
                while (h < he && (*h == ' ' || *h == '"')) ++h;
                while (he > h && (he[-1] == ' ' || he[-1] == '"')) --he;
                entry_put_span(&e, "ws-opts.headers.Host", h, he);
            }
        }
    }

/* shadowrocket spells the vless credential and sni differently from clash */
    if (!entry_get(&e, "uuid")) {
        const char *pw = entry_get(&e, "password");
        if (pw && pw[0]) entry_put_str(&e, "uuid", pw);
    }
    if (!entry_get(&e, "servername")) {
        const char *peer = entry_get_any(&e, "peer", "sni");
        if (peer && peer[0])
            entry_put_str(&e, "servername", peer);
    }
    {
        const char *obfs = entry_get(&e, "obfs");
        if (obfs && (ci_equal(obfs, "websocket") || ci_equal(obfs, "ws"))) {
            entry_put_str(&e, "network", "ws");
            const char *uri = entry_get_any(&e, "obfs-uri", "path");
            const char *oh = entry_get_any(&e, "obfs-host", "host");
            if (uri) entry_put_str(&e, "ws-opts.path", uri);
            if (oh) entry_put_str(&e, "ws-opts.headers.Host", oh);
        } else if (obfs && ci_equal(obfs, "grpc")) {
            entry_put_str(&e, "network", "grpc");
            const char *uri = entry_get_any(&e, "obfs-uri", "path");
            if (uri) entry_put_str(&e, "grpc-opts.grpc-service-name", uri);
        }
    }
    return entry_to_server(&e, s, why, why_cap);
}

size_t profiles_parse_surge(const char *blob, size_t len,
                            vl_server_t *out, size_t max) {
    return profiles_parse_surge_ex(blob, len, out, max, NULL);
}

size_t profiles_parse_surge_ex(const char *blob, size_t len, vl_server_t *out,
                               size_t max, cfg_import_stats_t *st) {
    if (!blob || !out || max == 0) return 0;
    const char *end = blob + len;
    const char *p = blob;
    size_t count = 0;
    int in_proxy = 0;

    while (p < end && count < max) {
        const char *le = line_end_of(p, end);
        const char *next = (le < end) ? le + 1 : end;
        const char *b = p, *e2 = le;
        trim_span(&b, &e2);
        if (b >= e2 || *b == '#' || *b == ';') { p = next; continue; }
        if (*b == '[') {
            in_proxy = ((size_t)(e2 - b) >= 7 && memcmp(b, "[Proxy]", 7) == 0);
            p = next;
            continue;
        }
        if (!in_proxy) { p = next; continue; }
        {
            const char *eq = (const char *)memchr(b, '=', (size_t)(e2 - b));
            char why[96] = "";
            memset(&out[count], 0, sizeof out[count]);
            if (eq && surge_line_to_server(b, eq, eq + 1, e2, &out[count], why, sizeof why) == 0)
                ++count;
            else if (eq && why[0])
                cfg_import_skip(st, out, max, &count, out[count].remark,
                                out[count].host, out[count].port, why);
        }
        p = next;
    }
    return count;
}
