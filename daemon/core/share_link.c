/* share links (vless://, trojan://, ss://, ...) as panels and exporters write
   them, into one vl_server_t. a link senko cannot run is refused with a short
   reason naming what it is, so an import can say what it skipped */
#include "config.h"

#include "b64.h"
#include "happ.h"

#include <stdio.h>
#include <string.h>

/* happ:// and the legacy ss:// base64 form re-enter the parser with bytes an
   untrusted panel supplied; a chain that keeps decoding into itself stops here */
#define LINK_MAX_DEPTH 8
#define LINK_MAX 8192

static void set_why(char *why, size_t cap, const char *text) {
    if (why && cap) snprintf(why, cap, "%s", text);
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int span_is(const char *s, size_t n, const char *lit) {
    size_t l = strlen(lit);
    if (n != l) return 0;
    for (size_t i = 0; i < n; ++i)
        if (lower(s[i]) != lit[i]) return 0;
    return 1;
}

typedef struct {
    char scheme[16];         /* lowercased */
    const char *user;        /* userinfo, NULL when absent */
    const char *user_end;
    const char *host;        /* without ipv6 brackets */
    const char *host_end;
    const char *port;        /* NULL when absent */
    const char *port_end;
    const char *query;       /* NULL when absent */
    const char *query_end;
    const char *frag;        /* nul terminated remark, NULL when absent */
} link_parts_t;

static int host_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '%';
}

/* host[:port] at p, ending at end, '/' or '?'. the port may be a hysteria2 hop
   list (digits, ',' and '-'). returns 1 and fills parts when it is one */
static int host_port_at(const char *p, const char *end, link_parts_t *parts,
                        const char **next) {
    const char *h = p, *he;
    if (p < end && *p == '[') {
        const char *rb = memchr(p, ']', (size_t)(end - p));
        if (!rb || rb == p + 1) return 0;
        h = p + 1;
        he = rb;
        p = rb + 1;
    } else {
        while (p < end && host_char(*p)) ++p;
        he = p;
        if (he == h) return 0;
    }
    const char *port = NULL, *port_end = NULL;
    if (p < end && *p == ':') {
        port = ++p;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == ',' || *p == '-')) ++p;
        port_end = p;
        if (port_end == port) return 0;
    }
    if (p < end && *p != '/' && *p != '?') return 0;
    parts->host = h;
    parts->host_end = he;
    parts->port = port;
    parts->port_end = port_end;
    *next = p;
    return 1;
}

/* the userinfo ends at the last '@' that is followed by a real host: trojan
   passwords carry '@', and base64 userinfo carries '/' and '+' */
static int split_at(const char *link, const char *hash, link_parts_t *parts);

/* a password with a raw '#' ("pa#ss@host") splits a remark off too early; the
   first '#' after which the link still names a host and a port is the real one */
static int split_link(const char *link, link_parts_t *parts) {
    const char *hash = strchr(link, '#');
    while (hash) {
        if (split_at(link, hash, parts) && (parts->port || !strchr(hash + 1, '#')))
            return 1;
        hash = strchr(hash + 1, '#');
    }
    return split_at(link, strchr(link, '#'), parts) || split_at(link, NULL, parts);
}

static int split_at(const char *link, const char *hash, link_parts_t *parts) {
    memset(parts, 0, sizeof *parts);
    const char *sep = strstr(link, "://");
    if (!sep || sep == link || (size_t)(sep - link) >= sizeof parts->scheme) return 0;
    for (const char *c = link; c < sep; ++c) {
        char l = lower(*c);
        if (!((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '+' || l == '-' || l == '.'))
            return 0;
        parts->scheme[c - link] = l;
    }
    const char *body = sep + 3;
    if (hash && hash < body) return 0;
    const char *end = hash ? hash : body + strlen(body);
    if (hash) parts->frag = hash + 1;

    /* the query can hold '@' too ("note=@channel"), so an '@' only counts when
       a host and a port follow it, and one before the query wins */
    const char *next = NULL;
    const char *first_q = memchr(body, '?', (size_t)(end - body));
    int found = 0;
    for (int pass = 0; pass < 2 && !found; ++pass) {
        const char *limit = pass == 0 && first_q ? first_q : end;
        for (const char *at = limit; at > body && !found; --at) {
            if (at[-1] != '@') continue;
            if (host_port_at(at, end, parts, &next) && parts->port) {
                parts->user = body;
                parts->user_end = at - 1;
                found = 1;
            }
        }
    }
    /* no port anywhere: the first '@' still separates the user, and the caller
       reports the missing port */
    if (!found) {
        const char *at = memchr(body, '@', (size_t)((first_q ? first_q : end) - body));
        if (at && host_port_at(at + 1, end, parts, &next)) {
            parts->user = body;
            parts->user_end = at;
        } else if (!host_port_at(body, end, parts, &next)) {
            return 0;
        }
    }
    const char *q = memchr(next, '?', (size_t)(end - next));
    if (q) {
        parts->query = q + 1;
        parts->query_end = end;
    }
    return 1;
}

static int span_decode(const char *s, const char *e, char *dst, size_t cap, int plus_is_space) {
    return url_percent_decode_ex(s, (size_t)(e - s), dst, cap, plus_is_space) >= 0 ? 0 : -1;
}

/* panel templates prepend a long shared banner to every node name, so the name
   is clipped on a codepoint boundary and stays valid utf-8 for the control line */
static void copy_remark(const char *src, char *dst, size_t cap) {
    char wide[1024];
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (url_percent_decode(src, strlen(src), wide, sizeof wide) < 0)
        snprintf(wide, sizeof wide, "%s", src);
    size_t n = strlen(wide);
    while (n > 0 && (wide[n - 1] == ' ' || wide[n - 1] == '\t')) --n;
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)wide[n] & 0xC0) == 0x80) --n;
    }
    memcpy(dst, wide, n);
    dst[n] = '\0';
}

/* every spelling a parameter has in the wild, mapped to one name */
typedef enum {
    Q_NONE = 0, Q_SECURITY, Q_TYPE, Q_SNI, Q_HOST, Q_PATH, Q_SERVICE, Q_MODE,
    Q_FLOW, Q_ENCRYPTION, Q_FP, Q_PBK, Q_SID, Q_INSECURE, Q_PIN, Q_OBFS,
    Q_OBFS_PASSWORD, Q_HEADER, Q_PLUGIN, Q_MPORT
} qkey_t;

static const struct { const char *name; qkey_t key; } k_aliases[] = {
    { "security", Q_SECURITY }, { "tls", Q_SECURITY },
    { "type", Q_TYPE }, { "network", Q_TYPE }, { "net", Q_TYPE },
    { "sni", Q_SNI }, { "peer", Q_SNI }, { "servername", Q_SNI },
    { "host", Q_HOST }, { "obfsparam", Q_HOST },
    { "path", Q_PATH },
    { "servicename", Q_SERVICE }, { "service_name", Q_SERVICE },
    { "mode", Q_MODE },
    { "flow", Q_FLOW },
    { "encryption", Q_ENCRYPTION },
    { "fp", Q_FP }, { "fingerprint", Q_FP },
    { "pbk", Q_PBK }, { "publickey", Q_PBK }, { "public-key", Q_PBK },
    { "sid", Q_SID }, { "shortid", Q_SID }, { "short-id", Q_SID },
    { "allowinsecure", Q_INSECURE }, { "insecure", Q_INSECURE },
    { "allow_insecure", Q_INSECURE }, { "skip-cert-verify", Q_INSECURE },
    { "pinsha256", Q_PIN },
    { "obfs", Q_OBFS },
    { "obfs-password", Q_OBFS_PASSWORD }, { "obfs_password", Q_OBFS_PASSWORD },
    { "obfspassword", Q_OBFS_PASSWORD },
    { "headertype", Q_HEADER },
    { "plugin", Q_PLUGIN },
    { "mport", Q_MPORT },
};

static qkey_t query_key(const char *k, size_t n) {
    for (size_t i = 0; i < sizeof k_aliases / sizeof k_aliases[0]; ++i)
        if (span_is(k, n, k_aliases[i].name)) return k_aliases[i].key;
    return Q_NONE;
}

typedef struct {
    char security[32];
    char type[32];
    char header[32];
    char plugin[128];
    char mport[128];
    char tls_flag[8]; /* "tls=1" in old socks/http exports */
} link_extra_t;

static void apply_query(const link_parts_t *parts, vl_server_t *s, link_extra_t *x) {
    const char *q = parts->query, *end = parts->query_end;
    while (q && q < end) {
        const char *amp = memchr(q, '&', (size_t)(end - q));
        const char *seg_end = amp ? amp : end;
        const char *eq = memchr(q, '=', (size_t)(seg_end - q));
        if (eq) {
            const char *v = eq + 1;
            qkey_t key = query_key(q, (size_t)(eq - q));
            if (key == Q_SECURITY && span_is(q, (size_t)(eq - q), "tls"))
                (void)span_decode(v, seg_end, x->tls_flag, sizeof x->tls_flag, 1);
            switch (key) {
            case Q_SECURITY:
                if (!span_is(q, (size_t)(eq - q), "tls"))
                    (void)span_decode(v, seg_end, x->security, sizeof x->security, 1);
                break;
            case Q_TYPE: (void)span_decode(v, seg_end, x->type, sizeof x->type, 1); break;
            case Q_HEADER: (void)span_decode(v, seg_end, x->header, sizeof x->header, 1); break;
            case Q_PLUGIN: (void)span_decode(v, seg_end, x->plugin, sizeof x->plugin, 1); break;
            case Q_MPORT: (void)span_decode(v, seg_end, x->mport, sizeof x->mport, 1); break;
            case Q_SNI: (void)span_decode(v, seg_end, s->sni, sizeof s->sni, 1); break;
            case Q_HOST: (void)span_decode(v, seg_end, s->ws_host, sizeof s->ws_host, 1); break;
            /* a path is a path: '+' in it is a plus, not a space */
            case Q_PATH:
            case Q_SERVICE:
                if (key == Q_PATH || !s->path[0])
                    (void)span_decode(v, seg_end, s->path, sizeof s->path, 0);
                break;
            case Q_MODE: (void)span_decode(v, seg_end, s->mode, sizeof s->mode, 1); break;
            case Q_FLOW: (void)span_decode(v, seg_end, s->flow, sizeof s->flow, 1); break;
            case Q_ENCRYPTION:
                if (span_decode(v, seg_end, s->encryption, sizeof s->encryption, 1) != 0)
                    snprintf(s->encryption, sizeof s->encryption, "unsupported");
                break;
            case Q_FP: (void)span_decode(v, seg_end, s->fp, sizeof s->fp, 1); break;
            case Q_PBK: (void)span_decode(v, seg_end, s->pbk, sizeof s->pbk, 1); break;
            case Q_SID: (void)span_decode(v, seg_end, s->sid, sizeof s->sid, 1); break;
            case Q_INSECURE: {
                char b[8] = "";
                (void)span_decode(v, seg_end, b, sizeof b, 1);
                if (strcmp(b, "1") == 0 || strcmp(b, "true") == 0) s->insecure = 1;
                break;
            }
            case Q_PIN: (void)span_decode(v, seg_end, s->pin_sha256, sizeof s->pin_sha256, 1); break;
            case Q_OBFS: (void)span_decode(v, seg_end, s->obfs, sizeof s->obfs, 1); break;
            case Q_OBFS_PASSWORD:
                (void)span_decode(v, seg_end, s->obfs_password, sizeof s->obfs_password, 0);
                break;
            case Q_NONE: break;
            }
        }
        q = amp ? amp + 1 : end;
    }
}

static int value_is(const char *v, const char *a) {
    return span_is(v, strlen(v), a);
}

/* 0, or -1 with why naming the part senko cannot carry */
int cfg_node_security(vl_server_t *s, const char *v, char *why, size_t cap) {
    if (!v[0] || value_is(v, "none") || value_is(v, "false") || value_is(v, "0"))
        s->security = VL_SEC_NONE;
    else if (value_is(v, "tls") || value_is(v, "xtls") || value_is(v, "true") ||
             value_is(v, "1"))
        s->security = VL_SEC_TLS;
    else if (value_is(v, "reality"))
        s->security = VL_SEC_REALITY;
    else {
        char text[64];
        snprintf(text, sizeof text, "%.24s security", v);
        set_why(why, cap, text);
        return -1;
    }
    return 0;
}

int cfg_node_transport(vl_server_t *s, const char *v, const char *header,
                       char *why, size_t cap) {
    if (!v[0] || value_is(v, "tcp") || value_is(v, "raw")) s->net = VL_NET_TCP;
    else if (value_is(v, "ws") || value_is(v, "websocket")) s->net = VL_NET_WS;
    else if (value_is(v, "grpc") || value_is(v, "gun")) s->net = VL_NET_GRPC;
    else if (value_is(v, "http") || value_is(v, "h2")) s->net = VL_NET_HTTP;
    else if (value_is(v, "xhttp") || value_is(v, "splithttp")) s->net = VL_NET_XHTTP;
    else {
        char text[64];
        snprintf(text, sizeof text, "%.24s transport", value_is(v, "mkcp") ? "kcp" : v);
        set_why(why, cap, text);
        return -1;
    }
    /* the xray tcp "http" header disguises every packet as an http exchange;
       a node that expects it drops a plain stream */
    if (s->net == VL_NET_TCP && header && value_is(header, "http")) {
        char text[64];
        snprintf(text, sizeof text, "tcp with an http header");
        set_why(why, cap, text);
        return -1;
    }
    return 0;
}

/* a 32 digit id without dashes is the same uuid; xray also takes any string of
   1 to 30 bytes and hashes it (vless_uuid_parse does the same) */
void cfg_node_vless_id(vl_server_t *s, const char *id) {
    size_t n = strlen(id);
    int hex = n == 32;
    for (size_t i = 0; hex && i < n; ++i)
        hex = (id[i] >= '0' && id[i] <= '9') || (lower(id[i]) >= 'a' && lower(id[i]) <= 'f');
    if (hex)
        snprintf(s->uuid, sizeof s->uuid, "%.8s-%.4s-%.4s-%.4s-%.12s",
                 id, id + 8, id + 12, id + 16, id + 20);
    else
        snprintf(s->uuid, sizeof s->uuid, "%.63s", id);
}

/* the method names exporters use for the three ciphers senko carries */
void cfg_node_ss_method(vl_server_t *s, const char *method) {
    char m[64];
    size_t i = 0;
    for (; method[i] && i + 1 < sizeof m; ++i) m[i] = lower(method[i]);
    m[i] = '\0';
    if (strcmp(m, "chacha20-poly1305") == 0)
        snprintf(m, sizeof m, "chacha20-ietf-poly1305");
    snprintf(s->user, sizeof s->user, "%.63s", m);
    snprintf(s->encryption, sizeof s->encryption, "%.31s", m);
}

/* "method:password", plain or base64 */
static int ss_userinfo(const char *u, const char *ue, vl_server_t *s) {
    char plain[512];
    const char *colon = memchr(u, ':', (size_t)(ue - u));
    if (colon) {
        char method[64];
        if (span_decode(u, colon, method, sizeof method, 0) != 0 ||
            span_decode(colon + 1, ue, s->pass, sizeof s->pass, 0) != 0)
            return -1;
        cfg_node_ss_method(s, method);
        return 0;
    }
    char enc[512];
    size_t dl = 0;
    if (span_decode(u, ue, enc, sizeof enc, 0) != 0 ||
        b64_decode(enc, strlen(enc), (unsigned char *)plain, sizeof plain - 1, &dl) != 0)
        return -1;
    plain[dl] = '\0';
    char *c = strchr(plain, ':');
    if (!c) return -1;
    *c = '\0';
    cfg_node_ss_method(s, plain);
    snprintf(s->pass, sizeof s->pass, "%s", c + 1);
    return 0;
}

static const struct { const char *scheme; const char *name; } k_unsupported[] = {
    { "tuic", "tuic" }, { "ssr", "ssr" },
    { "wireguard", "wireguard" }, { "wg", "wireguard" }, { "hysteria", "hysteria v1" },
    { "hy", "hysteria v1" }, { "anytls", "anytls" }, { "juicity", "juicity" },
    { "naive+https", "naive" }, { "naive+quic", "naive" }, { "mieru", "mieru" },
    { "snell", "snell" }, { "socks4", "socks4" }, { "socks4a", "socks4" },
    { "shadowtls", "shadowtls" }, { "ssh", "ssh" },
};

static cfg_status_t parse_link(const char *text, vl_server_t *out, char *why,
                               size_t why_cap, int depth);

/* happ:// carries one link or a list; the first usable one is the node */
static cfg_status_t parse_happ(const char *link, vl_server_t *out, char *why,
                               size_t why_cap, int depth) {
    char plain[LINK_MAX];
    if (happ_unwrap(link, plain, sizeof plain) != 0) {
        set_why(why, why_cap, "happ link that does not open");
        return CFG_ERR_UNSUPPORTED;
    }
    cfg_status_t last = CFG_ERR_SCHEME;
    char *p = plain;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        if (*p) {
            last = parse_link(p, out, why, why_cap, depth + 1);
            if (last == CFG_OK) return CFG_OK;
        }
        p = nl ? nl + 1 : NULL;
    }
    return last;
}

static cfg_status_t parse_link(const char *text, vl_server_t *out, char *why,
                               size_t why_cap, int depth) {
    memset(out, 0, sizeof *out);
    if (depth > LINK_MAX_DEPTH) return CFG_ERR_SCHEME;

    /* a bom, spaces around the line and html-escaped separators are what a
       panel page or a pasted list leaves on a link */
    char link[LINK_MAX];
    const char *b = text;
    if ((unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF)
        b += 3;
    while (*b == ' ' || *b == '\t' || *b == '\r' || *b == '\n') ++b;
    size_t o = 0;
    for (; *b && o + 1 < sizeof link; ++b) {
        if (b[0] == '&' && strncmp(b, "&amp;", 5) == 0) {
            link[o++] = '&';
            b += 4;
            continue;
        }
        link[o++] = *b;
    }
    if (*b) return CFG_ERR_TOO_LONG;
    while (o > 0 && (link[o - 1] == ' ' || link[o - 1] == '\t' ||
                     link[o - 1] == '\r' || link[o - 1] == '\n'))
        --o;
    link[o] = '\0';

    if (span_is(link, o < 7 ? o : 7, "happ://"))
        return parse_happ(link, out, why, why_cap, depth);

    link_parts_t parts;
    const char *sep = strstr(link, "://");
    char scheme[16] = "";
    if (sep && (size_t)(sep - link) < sizeof scheme)
        for (const char *c = link; c < sep; ++c) scheme[c - link] = lower(*c);
    for (size_t i = 0; i < sizeof k_unsupported / sizeof k_unsupported[0]; ++i)
        if (strcmp(scheme, k_unsupported[i].scheme) == 0) {
            set_why(why, why_cap, k_unsupported[i].name);
            return CFG_ERR_UNSUPPORTED;
        }

    /* v2rayn writes vmess as base64 json; the xtls share link form has an '@'
       in the authority and takes the vless path below */
    if (strcmp(scheme, "vmess") == 0) {
        const char *body = sep + 3;
        size_t end = strcspn(body, "#?");
        if (!memchr(body, '@', end)) {
            cfg_status_t st = cfg_parse_vmess_json(body, end, out, why, why_cap);
            const char *hash = strchr(body, '#');
            if (st == CFG_OK && !out->remark[0] && hash && hash[1])
                copy_remark(hash + 1, out->remark, sizeof out->remark);
            return st;
        }
    }

    /* ss://BASE64(method:password@host:port)#name, the pre-sip002 form */
    if (strcmp(scheme, "ss") == 0) {
        const char *body = sep + 3;
        const char *hash = strchr(body, '#');
        const char *end = hash ? hash : body + strlen(body);
        const char *q = memchr(body, '?', (size_t)(end - body));
        const char *b64_end = q ? q : end;
        if (b64_end > body && b64_end[-1] == '/') --b64_end;
        if (!memchr(body, '@', (size_t)(end - body))) {
            unsigned char dec[1024];
            size_t dl = 0;
            if (b64_decode(body, (size_t)(b64_end - body), dec, sizeof dec - 1, &dl) != 0 || !dl)
                return CFG_ERR_NO_AT;
            dec[dl] = '\0';
            char again[LINK_MAX];
            int n = snprintf(again, sizeof again, "ss://%s%.*s%s", (char *)dec,
                             (int)(end - b64_end), b64_end, hash ? hash : "");
            if (n < 0 || (size_t)n >= sizeof again) return CFG_ERR_TOO_LONG;
            return parse_link(again, out, why, why_cap, depth + 1);
        }
    }

    if (!split_link(link, &parts)) return sep ? CFG_ERR_NO_HOST : CFG_ERR_SCHEME;

    const char *sc = parts.scheme;
    if (strcmp(sc, "vless") == 0) out->proto = VL_PROTO_VLESS;
    else if (strcmp(sc, "vmess") == 0) out->proto = VL_PROTO_VMESS;
    else if (strcmp(sc, "trojan") == 0) out->proto = VL_PROTO_TROJAN;
    else if (strcmp(sc, "ss") == 0) out->proto = VL_PROTO_SHADOWSOCKS;
    else if (strcmp(sc, "hysteria2") == 0 || strcmp(sc, "hy2") == 0) out->proto = VL_PROTO_HYSTERIA2;
    else if (strcmp(sc, "socks") == 0 || strcmp(sc, "socks5") == 0 || strcmp(sc, "socks5h") == 0)
        out->proto = VL_PROTO_SOCKS5;
    else if (strcmp(sc, "http") == 0) out->proto = VL_PROTO_HTTP;
    else if (strcmp(sc, "https") == 0) out->proto = VL_PROTO_HTTPS;
    else return CFG_ERR_SCHEME;

    if (span_decode(parts.host, parts.host_end, out->host, sizeof out->host, 0) != 0)
        return CFG_ERR_TOO_LONG;
    if (!out->host[0]) return CFG_ERR_NO_HOST;

    link_extra_t x;
    memset(&x, 0, sizeof x);
    apply_query(&parts, out, &x);

    if (!parts.port) return CFG_ERR_BAD_PORT;
    size_t plen = (size_t)(parts.port_end - parts.port);
    int hop = memchr(parts.port, ',', plen) || memchr(parts.port, '-', plen);
    if (out->proto == VL_PROTO_HYSTERIA2 && (hop || x.mport[0])) {
        uint16_t first = 0;
        const char *list = hop ? parts.port : x.mport;
        size_t list_len = hop ? plen : strlen(x.mport);
        if (cfg_parse_port_hop(list, list_len, out->port_hop, sizeof out->port_hop, &first) != 0)
            return CFG_ERR_BAD_PORT;
        out->port = first;
        if (!hop) {
            unsigned long p = 0;
            for (const char *c = parts.port; c < parts.port_end; ++c) p = p * 10 + (unsigned long)(*c - '0');
            if (p > 0 && p <= 65535) out->port = (uint16_t)p;
        }
    } else {
        unsigned long p = 0;
        for (const char *c = parts.port; c < parts.port_end; ++c) {
            if (*c < '0' || *c > '9') return CFG_ERR_BAD_PORT;
            p = p * 10 + (unsigned long)(*c - '0');
            if (p > 65535) return CFG_ERR_BAD_PORT;
        }
        if (p == 0) return CFG_ERR_BAD_PORT;
        out->port = (uint16_t)p;
    }

    char user[512] = "";
    if (parts.user) {
        if ((size_t)(parts.user_end - parts.user) >= sizeof user) return CFG_ERR_TOO_LONG;
        memcpy(user, parts.user, (size_t)(parts.user_end - parts.user));
        user[parts.user_end - parts.user] = '\0';
    }

    switch (out->proto) {
    case VL_PROTO_VLESS:
    case VL_PROTO_VMESS: {
        char id[128];
        if (!user[0]) return CFG_ERR_NO_AT;
        if (url_percent_decode(user, strlen(user), id, sizeof id) < 0) return CFG_ERR_TOO_LONG;
        cfg_node_vless_id(out, id);
        /* the xtls share link names the vmess body cipher in encryption= */
        if (out->proto == VL_PROTO_VMESS && !out->encryption[0])
            snprintf(out->encryption, sizeof out->encryption, "auto");
        break;
    }
    /* the whole userinfo is one token: hysteria2 auth commonly holds a colon */
    case VL_PROTO_TROJAN:
    case VL_PROTO_HYSTERIA2:
        if (!user[0]) return CFG_ERR_NO_AT;
        if (url_percent_decode(user, strlen(user), out->pass, sizeof out->pass) < 0)
            return CFG_ERR_TOO_LONG;
        break;
    case VL_PROTO_SHADOWSOCKS:
        if (!user[0]) return CFG_ERR_NO_AT;
        if (ss_userinfo(user, user + strlen(user), out) != 0) {
            set_why(why, why_cap, "shadowsocks userinfo that does not decode");
            return CFG_ERR_UNSUPPORTED;
        }
        if (x.plugin[0] && !value_is(x.plugin, "none")) {
            char text[64];
            size_t n = strcspn(x.plugin, ";");
            snprintf(text, sizeof text, "shadowsocks plugin %.*s", (int)(n < 32 ? n : 32), x.plugin);
            set_why(why, why_cap, text);
            return CFG_ERR_UNSUPPORTED;
        }
        break;
    default: {
        /* socks and http: "user:pass", or base64 of it as v2rayn writes socks */
        if (!user[0]) break;
        char dec[256];
        size_t dl = 0;
        const char *pair = user;
        if (!strchr(user, ':') &&
            b64_decode(user, strlen(user), (unsigned char *)dec, sizeof dec - 1, &dl) == 0 &&
            dl && memchr(dec, ':', dl)) {
            dec[dl] = '\0';
            pair = dec;
            char *c = strchr(dec, ':');
            *c = '\0';
            snprintf(out->user, sizeof out->user, "%.63s", dec);
            snprintf(out->pass, sizeof out->pass, "%s", c + 1);
            break;
        }
        const char *c = strchr(pair, ':');
        if (url_percent_decode(pair, c ? (size_t)(c - pair) : strlen(pair), out->user,
                               sizeof out->user) < 0 ||
            (c && url_percent_decode(c + 1, strlen(c + 1), out->pass, sizeof out->pass) < 0))
            return CFG_ERR_TOO_LONG;
        break;
    }
    }

    if (out->proto == VL_PROTO_VLESS || out->proto == VL_PROTO_VMESS ||
        out->proto == VL_PROTO_TROJAN || out->proto == VL_PROTO_SHADOWSOCKS) {
        if (cfg_node_transport(out, x.type, x.header, why, why_cap) != 0) return CFG_ERR_UNSUPPORTED;
        if (cfg_node_security(out, x.security, why, why_cap) != 0) return CFG_ERR_UNSUPPORTED;
        if (out->proto == VL_PROTO_TROJAN && !x.security[0]) out->security = VL_SEC_TLS;
    }
    if ((out->proto == VL_PROTO_SOCKS5 || out->proto == VL_PROTO_HTTP) &&
        (strcmp(x.tls_flag, "1") == 0 || strcmp(x.tls_flag, "true") == 0))
        out->proto = out->proto == VL_PROTO_HTTP ? VL_PROTO_HTTPS : out->proto;

    cfg_node_finish(out);
    if (parts.frag) copy_remark(parts.frag, out->remark, sizeof out->remark);
    return CFG_OK;
}

void cfg_node_finish(vl_server_t *out) {
    /* vision-udp443 is vision that also lets quic to port 443 through */
    if (strcmp(out->flow, "xtls-rprx-vision-udp443") == 0)
        snprintf(out->flow, sizeof out->flow, "xtls-rprx-vision");
    if (strcmp(out->flow, "none") == 0) out->flow[0] = '\0';

    /* the mode= query param is the only way a share link spells multiMode;
       read it before cfg_normalize_grpc_path overwrites out->mode with the
       fixed "grpc" transport marker. only set the flag, never clear it: the
       json parsers that also flow through here already resolved it from
       grpcSettings.multiMode and left out->mode untouched at "grpc" */
    if (out->net == VL_NET_GRPC && strcmp(out->mode, "multi") == 0)
        out->grpc_multi = 1;
    cfg_normalize_grpc_path(out);

    /* public lists put a telegram handle in sni=. a name that cannot be a dns
       name loses the handshake before it starts, so the host stands in */
    if (out->sni[0] && !cfg_sni_text_ok(out->sni)) out->sni[0] = '\0';
    if (!out->sni[0] && (out->security == VL_SEC_TLS || out->security == VL_SEC_REALITY ||
                         out->proto == VL_PROTO_HYSTERIA2))
        snprintf(out->sni, sizeof out->sni, "%s", out->host);
    if (out->net == VL_NET_WS && !out->ws_host[0])
        snprintf(out->ws_host, sizeof out->ws_host, "%s", out->sni[0] ? out->sni : out->host);

    /* quic brings its own tls; there is no plain or reality hysteria2 */
    if (out->proto == VL_PROTO_HYSTERIA2) {
        out->security = VL_SEC_TLS;
        out->net = VL_NET_TCP;
    }
    if (out->proto == VL_PROTO_SHADOWSOCKS) {
        out->security = VL_SEC_NONE;
        if (!out->user[0]) cfg_node_ss_method(out, "chacha20-ietf-poly1305");
    }
    /* sessions take the vmess body cipher from user, as they take the
       shadowsocks method */
    if (out->proto == VL_PROTO_VMESS) {
        if (!out->encryption[0]) snprintf(out->encryption, sizeof out->encryption, "auto");
        snprintf(out->user, sizeof out->user, "%s", out->encryption);
    }
}

cfg_status_t cfg_parse_link_ex(const char *uri, vl_server_t *out, char *why, size_t why_cap) {
    if (why && why_cap) why[0] = '\0';
    if (!uri || !out) return CFG_ERR_BAD_ARG;
    return parse_link(uri, out, why, why_cap, 0);
}

cfg_status_t cfg_parse_link(const char *uri, vl_server_t *out) {
    return cfg_parse_link_ex(uri, out, NULL, 0);
}
