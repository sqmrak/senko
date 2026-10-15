#include "config.h"
#include "b64.h"
#include "happ.h"
#include "profiles.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>


static int is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hexnib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
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

/* rfc 1035 label shape, which an ip literal also satisfies: xray puts the host
   in sni when none is configured, and that host is sometimes an address */
int cfg_sni_text_ok(const char *h) {
    if (!h || !h[0]) return 0;
    size_t total = strlen(h);
    if (total > 253) return 0;
    size_t label = 0;
    for (size_t i = 0; i < total; ++i) {
        char c = h[i];
        if (c == '.') {
            if (label == 0) return 0; /* empty label, leading or doubled dot */
            if (h[i - 1] == '-') return 0;
            label = 0;
            continue;
        }
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-';
        if (!ok) return 0;
        if (label == 0 && c == '-') return 0;
        if (++label > 63) return 0;
    }
    return label != 0 && h[total - 1] != '-'; /* reject a trailing dot */
}

/* what xray accepts as a vless id: a uuid, or any string of 1 to 30 bytes that
   it hashes into one (vless_uuid_parse) */
static int uuid_text_ok(const char *u) {
    if (!u) return 0;
    size_t n = strlen(u);
    if (n >= 1 && n <= 30) {
        for (size_t i = 0; i < n; ++i)
            if ((unsigned char)u[i] < 0x20) return 0;
        return 1;
    }
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
    if (s->proto == VL_PROTO_UNSUPPORTED) {
        cfg_reason(reason, reason_cap, s->unsupported[0] ? s->unsupported : "unsupported");
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

    if (s->proto == VL_PROTO_TROJAN) {
        if (!s->pass[0]) {
            cfg_reason(reason, reason_cap, "trojan requires password");
            return 0;
        }
        if (s->security == VL_SEC_REALITY) {
            cfg_reason(reason, reason_cap, "trojan over reality");
            return 0;
        }
        if (s->security != VL_SEC_TLS) {
            cfg_reason(reason, reason_cap, "trojan requires tls");
            return 0;
        }
        if (s->net != VL_NET_TCP && s->net != VL_NET_WS) {
            cfg_reason(reason, reason_cap, "unsupported trojan transport");
            return 0;
        }
        return 1;
    }

    if (s->proto == VL_PROTO_SHADOWSOCKS) {
        if (!s->pass[0]) {
            cfg_reason(reason, reason_cap, "shadowsocks requires password");
            return 0;
        }
        if (strcmp(s->encryption, "aes-256-gcm") &&
            strcmp(s->encryption, "aes-128-gcm") &&
            strcmp(s->encryption, "chacha20-ietf-poly1305")) {
            char text[80];
            int named = s->encryption[0] != '\0';
            for (const char *c = s->encryption; *c && named; ++c)
                named = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
            snprintf(text, sizeof text, "shadowsocks %.40s", named ? s->encryption : "cipher that is not a name");
            cfg_reason(reason, reason_cap, text);
            return 0;
        }
        /* senko speaks shadowsocks over a bare tcp stream only */
        if (s->net != VL_NET_TCP) {
            cfg_reason(reason, reason_cap, "shadowsocks over another transport");
            return 0;
        }
        return 1;
    }

    if (s->proto == VL_PROTO_HYSTERIA2) {
        if (!s->pass[0]) {
            cfg_reason(reason, reason_cap, "hysteria2 requires an auth password");
            return 0;
        }
        if (s->obfs[0] && strcmp(s->obfs, "none") != 0) {
/* senko-core's finalmask udp mask only builds a salamander conn */
            if (strcmp(s->obfs, "salamander") != 0) {
                cfg_reason(reason, reason_cap, "unsupported hysteria2 obfuscation type");
                return 0;
            }
            if (!s->obfs_password[0]) {
                cfg_reason(reason, reason_cap,
                          "hysteria2 salamander obfuscation requires obfs-password");
                return 0;
            }
        }
        return 1;
    }

    /* vmess shares every transport and security vless has; only the id, the
       body cipher and the absence of flow differ. xray runs any cipher name it
       does not know as auto, so the name is not checked here either */
    if (s->proto == VL_PROTO_VMESS) {
        if (!uuid_text_ok(s->uuid)) {
            cfg_reason(reason, reason_cap, "invalid vmess id");
            return 0;
        }
        if (s->flow[0]) {
            cfg_reason(reason, reason_cap, "vmess does not use flow");
            return 0;
        }
    } else if (s->proto != VL_PROTO_VLESS) {
        cfg_reason(reason, reason_cap, "unsupported protocol");
        return 0;
    } else {
        if (!uuid_text_ok(s->uuid)) {
            cfg_reason(reason, reason_cap, "invalid vless id");
            return 0;
        }
        if (s->encryption[0] && strcmp(s->encryption, "none") != 0) {
            cfg_reason(reason, reason_cap, "vless encryption");
            return 0;
        }
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

int url_percent_decode_ex(const char *src, size_t src_len, char *dst,
                          size_t cap, int plus_is_space) {
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
        } else if (c == '+' && plus_is_space) {
/* '+' means space only inside a query, never in fragments or bodies */
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

int url_percent_decode(const char *src, size_t src_len, char *dst, size_t cap) {
    return url_percent_decode_ex(src, src_len, dst, cap, 0);
}

int cfg_parse_port_hop(const char *text, size_t len, char *dst, size_t dst_cap,
                       uint16_t *first_port_out) {
    const char *end;
    const char *tok;
    unsigned long first = 0;
    int have_first = 0;

    if (!text || len == 0 || (dst && len + 1 > dst_cap)) return -1;
    end = text + len;
    tok = text;

    for (const char *c = text; c <= end; ++c) {
        if (c != end && *c != ',') continue;
        {
            const char *dash = memchr(tok, '-', (size_t)(c - tok));
            const char *from_end = dash ? dash : c;
            unsigned long from = 0, to;
            if (from_end == tok) return -1;
            for (const char *d = tok; d < from_end; ++d) {
                if (*d < '0' || *d > '9') return -1;
                from = from * 10 + (unsigned long)(*d - '0');
                if (from > 65535) return -1;
            }
            if (dash) {
                if (dash + 1 >= c) return -1;
                to = 0;
                for (const char *d = dash + 1; d < c; ++d) {
                    if (*d < '0' || *d > '9') return -1;
                    to = to * 10 + (unsigned long)(*d - '0');
                    if (to > 65535) return -1;
                }
                if (to < from) return -1;
            }
            if (from == 0) return -1;
            if (!have_first) { first = from; have_first = 1; }
        }
        tok = c + 1;
    }
    if (!have_first) return -1;
    if (dst) {
        memcpy(dst, text, len);
        dst[len] = '\0';
    }
    if (first_port_out) *first_port_out = (uint16_t)first;
    return 0;
}

/* xray's old grpc links carry only serviceName; the wire path also names the
   stream. multiMode servers register "TunMulti" instead of "Tun"
   (transport/internet/grpc/encoding/customSeviceName.go), and a path built
   with the wrong one comes back "unknown service <name>/Tun" even though the
   token and tls handshake were both fine */
void cfg_normalize_grpc_path(vl_server_t *s) {
    char base[sizeof s->path];
    const char *stream = s->grpc_multi ? "/TunMulti" : "/Tun";
    size_t stream_len = strlen(stream);
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
        snprintf(s->path, sizeof s->path, "%s", stream);
    } else if (n >= stream_len && strcmp(base + n - stream_len, stream) == 0) {
        snprintf(s->path, sizeof s->path, "%s", base);
    } else {
        snprintf(s->path, sizeof s->path, "%s%s", base, stream);
    }
    snprintf(s->mode, sizeof s->mode, "grpc");
}

/* the blob is not nul terminated, so strstr cannot be used on it */
static const char *memmem_ascii(const char *hay, size_t n, const char *needle) {
    size_t nl = strlen(needle);
    size_t i;
    if (nl == 0 || n < nl) return NULL;
    for (i = 0; i + nl <= n; ++i) {
        if (memcmp(hay + i, needle, nl) == 0) return hay + i;
    }
    return NULL;
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

void cfg_import_stats_add(cfg_import_stats_t *st, const char *what) {
    if (!st || !what || !what[0]) return;
    st->skipped++;
    size_t i = 0;
    for (; i < CFG_SKIP_KINDS && st->kinds[i].count; ++i)
        if (strcmp(st->kinds[i].what, what) == 0) break;
    if (i == CFG_SKIP_KINDS) {
        st->other++;
        return;
    }
    if (!st->kinds[i].count) snprintf(st->kinds[i].what, sizeof st->kinds[i].what, "%s", what);
    st->kinds[i].count++;
    /* keep the most common first */
    while (i > 0 && st->kinds[i].count > st->kinds[i - 1].count) {
        char tmp[sizeof st->kinds[0].what];
        size_t c = st->kinds[i].count;
        snprintf(tmp, sizeof tmp, "%s", st->kinds[i].what);
        st->kinds[i] = st->kinds[i - 1];
        snprintf(st->kinds[i - 1].what, sizeof st->kinds[i - 1].what, "%s", tmp);
        st->kinds[i - 1].count = c;
        --i;
    }
}

void cfg_import_skip(cfg_import_stats_t *st, vl_server_t *out, size_t max,
                     size_t *count, const char *remark, const char *host,
                     uint16_t port, const char *what) {
    vl_server_t row;
    cfg_import_stats_add(st, what);
    if (!st || !st->keep_unsupported || !out || !count || *count >= max) return;
    if ((!remark || !remark[0]) && (!host || !host[0])) return;
    /* the caller may pass fields that live in out[*count] itself */
    memset(&row, 0, sizeof row);
    row.proto = VL_PROTO_UNSUPPORTED;
    row.port = port;
    snprintf(row.host, sizeof row.host, "%s", host ? host : "");
    snprintf(row.unsupported, sizeof row.unsupported, "%s",
             what && what[0] ? what : "unsupported");
    if (remark && remark[0])
        snprintf(row.remark, sizeof row.remark, "%s", remark);
    else if (port)
        snprintf(row.remark, sizeof row.remark, "%.120s:%u", row.host, (unsigned)port);
    else
        snprintf(row.remark, sizeof row.remark, "%s", row.host);
    out[(*count)++] = row;
}

void cfg_import_stats_text(const cfg_import_stats_t *st, char *out, size_t cap) {
    if (!out || !cap) return;
    out[0] = '\0';
    if (!st || !st->skipped) return;
    size_t off = 0;
    int n = snprintf(out, cap, "skipped %zu:", st->skipped);
    if (n < 0 || (size_t)n >= cap) return;
    off = (size_t)n;
    size_t other = st->other;
    for (size_t i = 0; i < CFG_SKIP_KINDS && st->kinds[i].count; ++i) {
        if (i >= 4) {
            other += st->kinds[i].count;
            continue;
        }
        n = snprintf(out + off, cap - off, "%s %s %zu", i ? "," : "",
                     st->kinds[i].what, st->kinds[i].count);
        if (n < 0 || (size_t)n >= cap - off) return;
        off += (size_t)n;
    }
    if (other) {
        n = snprintf(out + off, cap - off, ", other %zu", other);
        if (n > 0 && (size_t)n < cap - off) off += (size_t)n;
    }
}

static void parse_link_lines(const char *text, size_t len, vl_server_t *out,
                             size_t max, size_t *count, cfg_import_stats_t *st);

/* the schemes a line can carry a node in; anything else is prose */
static int node_scheme_at(const char *p) {
    static const char *const schemes[] = {
        "vless://", "vmess://", "trojan://", "ss://", "ssr://", "socks://",
        "socks5://", "hysteria2://", "hy2://", "hysteria://", "tuic://",
        "wireguard://", "anytls://", "happ://", NULL
    };
    for (int i = 0; schemes[i]; ++i)
        if (strncasecmp(p, schemes[i], strlen(schemes[i])) == 0) return 1;
    return 0;
}

static void link_fields(const char *link, char *remark, size_t remark_cap,
                        char *host, size_t host_cap, uint16_t *port);

/* one link into out[*count]; a link that cannot be used is counted by why */
static int take_link(const char *link, vl_server_t *out, size_t max, size_t *count,
                     cfg_import_stats_t *st) {
    char why[96];
    char remark[256], host[256];
    uint16_t port = 0;
    if (*count >= max) return 0;
    /* a routing profile is not a node: the daemon reads it from the same body
       (routing_find), so it must not be counted as a link that failed to open */
    if (strncasecmp(link, "happ://routing/", 15) == 0) return 0;
    if (strncasecmp(link, "happ://", 7) == 0) {
        char plain[8192];
        size_t before = *count;
        if (happ_unwrap(link, plain, sizeof plain) != 0) {
            cfg_import_stats_add(st, "happ link that does not open");
            return 0;
        }
        parse_link_lines(plain, strlen(plain), out, max, count, st);
        return *count > before;
    }
    cfg_status_t r = cfg_parse_link_ex(link, &out[*count], why, sizeof why);
    if (r == CFG_OK) {
        vl_server_t *s = &out[*count];
        if (cfg_validate_server(s, why, sizeof why)) {
            (*count)++;
            return 1;
        }
        cfg_import_skip(st, out, max, count, s->remark, s->host, s->port, why);
    } else if (r == CFG_ERR_UNSUPPORTED || node_scheme_at(link)) {
        link_fields(link, remark, sizeof remark, host, sizeof host, &port);
        cfg_import_skip(st, out, max, count, remark, host, port,
                        r == CFG_ERR_UNSUPPORTED ? why : "malformed link");
    }
    return 0;
}

static int link_token_char(char c) {
    return !(c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\' || c == '`' ||
             c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0');
}

/* a panel page, a telegram post or a line of several links carries the nodes
   between quotes, tags and prose; they are picked out one by one */
static void scan_embedded_links(const char *line, vl_server_t *out, size_t max,
                                size_t *count, cfg_import_stats_t *st) {
    char token[8192];
    size_t n = strlen(line);
    for (size_t i = 0; i < n && *count < max; ) {
        if (!node_scheme_at(line + i) || (i > 0 && isalnum((unsigned char)line[i - 1]))) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && link_token_char(line[j])) ++j;
        if (j - i < sizeof token) {
            memcpy(token, line + i, j - i);
            token[j - i] = '\0';
            (void)take_link(token, out, max, count, st);
        }
        i = j > i ? j : i + 1;
    }
}

/* panels write the remark unencoded ("#Hong Kong 🇭🇰"), so spaces after the
   '#' stay in the name unless another link starts there */
static int whole_line_link(const char *l, size_t len) {
    if (!node_scheme_at(l)) return 0;
    const char *hash = memchr(l, '#', len);
    size_t head = hash ? (size_t)(hash - l) : len;
    if (memchr(l, ' ', head) || memchr(l, '\t', head)) return 0;
    for (size_t i = head; i + 1 < len; ++i)
        if ((l[i] == ' ' || l[i] == '\t') && node_scheme_at(l + i + 1)) return 0;
    return 1;
}

static void parse_link_lines(const char *text, size_t len, vl_server_t *out,
                             size_t max, size_t *count, cfg_import_stats_t *st) {
    const char *p = text;
    const char *end = text + len;
    char line[8192]; /* feeds append long xhttp metadata to each link */

    while (p < end && *count < max) {
        const char *nl = p;
        while (nl < end && *nl != '\n' && *nl != '\r') ++nl;
        size_t llen = (size_t)(nl - p);
        if (llen >= sizeof line && memmem_ascii(p, llen, "://"))
            cfg_import_stats_add(st, "link too long");
        if (llen > 0 && llen < sizeof line) {
            memcpy(line, p, llen);
            line[llen] = '\0';
            const char *l = line;
            if ((unsigned char)l[0] == 0xEF && (unsigned char)l[1] == 0xBB && (unsigned char)l[2] == 0xBF)
                l += 3;
            while (*l == ' ' || *l == '\t') ++l;
            /* a whole line that is one link keeps characters the embedded scan
               would end a token at */
            size_t tail = strlen(l);
            while (tail && (l[tail - 1] == ' ' || l[tail - 1] == '\t')) --tail;
            if (whole_line_link(l, tail))
                (void)take_link(l, out, max, count, st);
            else if (*l != '#' || strstr(l, "://"))
                scan_embedded_links(l, out, max, count, st);
        }
        p = nl < end ? nl + 1 : end;
    }
}

/* the happ deep link a panel page carries, unwrapped. the page is the only
   thing some panels publish, so what that link points at decides whether there
   is a subscription behind the address at all */
static int page_happ_target(const char *blob, size_t blob_len,
                            char *out, size_t out_cap) {
    const char *at = memmem_ascii(blob, blob_len, "happ://");
    size_t i;
    char token[4096];
    if (!at) at = memmem_ascii(blob, blob_len, "HAPP://");
    if (!at) return -1;
    i = 0;
    while (at + i < blob + blob_len && i + 1 < sizeof token &&
           link_token_char(at[i]))
        ++i;
    if (i < 8) return -1;
    memcpy(token, at, i);
    token[i] = '\0';
    return happ_unwrap(token, out, out_cap);
}

const char *cfg_reject_reason(const char *blob, size_t blob_len,
                              const char *source_url) {
    char target[4096];
    size_t i = 0;
    if (!blob || blob_len == 0) return NULL;

    if (page_happ_target(blob, blob_len, target, sizeof target) == 0) {
/* a page whose only offer is a link back to itself is not a subscription, and
   following it again would just fetch the same page */
        if (source_url && strcmp(target, source_url) == 0)
            return "this address only hands back its own link instead of a "
                   "subscription feed";
    } else if (memmem_ascii(blob, blob_len, "happ://crypt5/") ||
               memmem_ascii(blob, blob_len, "HAPP://crypt5/")) {
        return "the happ crypt5 bundle on this page could not be opened: it is "
               "either damaged or sealed with a key senko does not carry";
    }

    while (i < blob_len && (blob[i] == ' ' || blob[i] == '\t' ||
                            blob[i] == '\r' || blob[i] == '\n'))
        ++i;
    if (blob_len - i >= 5 &&
        (strncasecmp(blob + i, "<html", 5) == 0 ||
         strncasecmp(blob + i, "<!doc", 5) == 0))
        return "this address opens a web page, not a subscription feed";
    return NULL;
}

#include "third_party/cJSON.h"

static const cJSON *jobj(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_IsObject(obj) ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    return cJSON_IsObject(v) ? v : NULL;
}

static const char *jstr(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_IsObject(obj) ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

static int jstr_copy(const cJSON *obj, const char *key, char *dst, size_t cap) {
    const char *v = jstr(obj, key);
    if (!v || !dst || cap == 0) return -1;
    snprintf(dst, cap, "%s", v);
    return 0;
}

static int jstr_copy_any(const cJSON *obj, char *dst, size_t cap,
                         const char *k0, const char *k1) {
    if (jstr_copy(obj, k0, dst, cap) == 0) return 0;
    if (k1 && jstr_copy(obj, k1, dst, cap) == 0) return 0;
    return -1;
}

/* a short id, a host list or an alpn list is a string or an array of them;
   the first one is what a single connection uses */
static void jstr_first(const cJSON *obj, const char *key, char *dst, size_t cap) {
    const cJSON *v = cJSON_IsObject(obj) ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    if (cJSON_IsArray(v) && cJSON_GetArraySize(v) > 0) v = cJSON_GetArrayItem(v, 0);
    if (cJSON_IsString(v) && v->valuestring && cap) snprintf(dst, cap, "%s", v->valuestring);
}

static int j_port(const cJSON *obj, const char *key, uint16_t *out) {
    const cJSON *v = cJSON_IsObject(obj) ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
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

static int host_text_ok(const char *h) {
    if (!h[0]) return 0;
    for (; *h; ++h)
        if (!isalnum((unsigned char)*h) && *h != '.' && *h != '-' && *h != ':' &&
            *h != '_')
            return 0;
    return 1;
}

/* the name, host and port of a link senko cannot read, for its placeholder row.
   vmess carries them in a base64 json body, every other scheme in the
   authority and the fragment. fields that do not look right stay empty */
static void link_fields(const char *link, char *remark, size_t remark_cap,
                        char *host, size_t host_cap, uint16_t *port) {
    const char *body = strstr(link, "://");
    remark[0] = '\0';
    host[0] = '\0';
    *port = 0;
    if (!body) return;
    body += 3;
    if (strncasecmp(link, "vmess://", 8) == 0) {
        unsigned char plain[4096];
        size_t n = 0;
        size_t len = strcspn(body, "#?");
        if (b64_decode(body, len, plain, sizeof plain - 1, &n) == 0 && n > 0) {
            cJSON *root = cJSON_ParseWithLength((const char *)plain, n);
            if (root) {
                (void)jstr_copy(root, "ps", remark, remark_cap);
                (void)jstr_copy(root, "add", host, host_cap);
                (void)j_port(root, "port", port);
                cJSON_Delete(root);
                if (!host_text_ok(host)) host[0] = '\0';
                return;
            }
        }
    }
    const char *hash = strchr(body, '#');
    if (hash && hash[1] &&
        url_percent_decode(hash + 1, strlen(hash + 1), remark, remark_cap) < 0)
        remark[0] = '\0';
    size_t auth_len = strcspn(body, "/?#");
    const char *auth = body;
    for (size_t i = 0; i < auth_len; ++i)
        if (body[i] == '@') auth = body + i + 1;
    auth_len -= (size_t)(auth - body);
    const char *colon = NULL;
    const char *hs = auth, *he = auth + auth_len;
    if (auth_len && auth[0] == '[') {
        const char *close = memchr(auth, ']', auth_len);
        if (!close) return;
        hs = auth + 1;
        he = close;
        if (close + 1 < auth + auth_len && close[1] == ':') colon = close + 1;
    } else {
        for (const char *c = auth; c < auth + auth_len; ++c)
            if (*c == ':') colon = c;
        if (colon) he = colon;
    }
    if ((size_t)(he - hs) >= host_cap) return;
    memcpy(host, hs, (size_t)(he - hs));
    host[he - hs] = '\0';
    if (!host_text_ok(host)) {
        host[0] = '\0';
        return;
    }
    if (colon) {
        char digits[8];
        size_t dn = (size_t)(auth + auth_len - colon - 1);
        char *end = NULL;
        if (dn == 0 || dn >= sizeof digits) return;
        memcpy(digits, colon + 1, dn);
        digits[dn] = '\0';
        long v = strtol(digits, &end, 10);
        if (end == digits + dn && v > 0 && v <= 65535) *port = (uint16_t)v;
    }
}

static int json_flag_on(const cJSON *obj, const char *key);

/* the v2rayn body of a vmess:// link: base64 of a flat json object whose
   "type" is the tcp header, the grpc mode or the xhttp mode depending on net */
cfg_status_t cfg_parse_vmess_json(const char *body, size_t len, vl_server_t *out,
                                  char *why, size_t why_cap) {
    unsigned char plain[4096];
    size_t n = 0;
    char id[64] = "", net[24] = "", type[24] = "", tls[24] = "";
    cJSON *root;
    if (b64_decode(body, len, plain, sizeof plain - 1, &n) != 0 || n == 0)
        return CFG_ERR_NO_AT;
    root = cJSON_ParseWithLength((const char *)plain, n);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return CFG_ERR_NO_AT;
    }
    out->proto = VL_PROTO_VMESS;
    if (jstr_copy(root, "add", out->host, sizeof out->host) != 0 || !out->host[0]) {
        cJSON_Delete(root);
        return CFG_ERR_NO_HOST;
    }
    if (j_port(root, "port", &out->port) != 0) {
        cJSON_Delete(root);
        return CFG_ERR_BAD_PORT;
    }
    if (jstr_copy(root, "id", id, sizeof id) != 0 || !id[0]) {
        cJSON_Delete(root);
        return CFG_ERR_NO_AT;
    }
    cfg_node_vless_id(out, id);
    if (jstr_copy(root, "scy", out->encryption, sizeof out->encryption) != 0 ||
        !out->encryption[0])
        snprintf(out->encryption, sizeof out->encryption, "auto");
    (void)jstr_copy(root, "ps", out->remark, sizeof out->remark);
    (void)jstr_copy(root, "net", net, sizeof net);
    (void)jstr_copy(root, "type", type, sizeof type);
    (void)jstr_copy(root, "tls", tls, sizeof tls);
    (void)jstr_copy(root, "host", out->ws_host, sizeof out->ws_host);
    (void)jstr_copy(root, "path", out->path, sizeof out->path);
    (void)jstr_copy(root, "sni", out->sni, sizeof out->sni);
    (void)jstr_copy(root, "fp", out->fp, sizeof out->fp);
    (void)jstr_copy(root, "pbk", out->pbk, sizeof out->pbk);
    (void)jstr_copy(root, "sid", out->sid, sizeof out->sid);
    (void)jstr_copy(root, "mode", out->mode, sizeof out->mode);
    out->insecure = json_flag_on(root, "allowInsecure") || json_flag_on(root, "insecure");
    cJSON_Delete(root);

    int is_tcp = !net[0] || strcasecmp(net, "tcp") == 0 || strcasecmp(net, "raw") == 0;
    if (cfg_node_transport(out, net, is_tcp ? type : NULL, why, why_cap) != 0 ||
        cfg_node_security(out, tls, why, why_cap) != 0)
        return CFG_ERR_UNSUPPORTED;
    if (out->net == VL_NET_GRPC && strcasecmp(type, "multi") == 0) out->grpc_multi = 1;
    if (out->net == VL_NET_XHTTP && !out->mode[0] && type[0] && strcasecmp(type, "none") != 0)
        snprintf(out->mode, sizeof out->mode, "%s", type);
    /* a websocket host only means something on transports that send it */
    if (out->net != VL_NET_WS && out->net != VL_NET_HTTP && out->net != VL_NET_XHTTP)
        out->ws_host[0] = '\0';
    cfg_node_finish(out);
    return CFG_OK;
}

/* sing-box gates tls and reality on their own boolean, and a block that is
   present but disabled means plain tcp, not tls */
static int json_flag_on(const cJSON *obj, const char *key) {
    const cJSON *v;
    if (!cJSON_IsObject(obj)) return 0;
    v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? 1 : 0;
    if (cJSON_IsNumber(v)) return v->valuedouble != 0.0;
    if (cJSON_IsString(v) && v->valuestring)
        return strcmp(v->valuestring, "true") == 0 || strcmp(v->valuestring, "1") == 0;
    return 0;
}

/* outbounds that route rather than carry: never a node, never a skip */
static int json_plumbing(const char *kind) {
    static const char *const names[] = {
        "freedom", "blackhole", "dns", "loopback", "direct", "block", "selector",
        "urltest", "url-test", "fallback", "load-balance", NULL
    };
    for (int i = 0; names[i]; ++i)
        if (strcmp(kind, names[i]) == 0) return 1;
    return 0;
}

/* this fork's own hysteria outbound shape (see senko_core_config.c's append_outbound):
   the destination sits in settings, auth and quic live under
   streamSettings.hysteriaSettings, and finalmask carries obfuscation and
   port hopping when the node uses either */
static int xray_hysteria(const cJSON *ob, const cJSON *settings, vl_server_t *s) {
    const cJSON *stream = jobj(ob, "streamSettings");
    const cJSON *ts, *fm;

    if (jstr_copy(settings, "address", s->host, sizeof s->host) != 0 ||
        j_port(settings, "port", &s->port) != 0 ||
        jstr_copy(jobj(stream, "hysteriaSettings"), "auth", s->pass, sizeof s->pass) != 0)
        return -1;
    s->proto = VL_PROTO_HYSTERIA2;

    ts = jobj(stream, "tlsSettings");
    (void)jstr_copy_any(ts, s->sni, sizeof s->sni, "serverName", "server_name");
    (void)jstr_copy(ts, "pinnedPeerCertSha256", s->pin_sha256, sizeof s->pin_sha256);

    fm = jobj(stream, "finalmask");
    if (fm) {
        const cJSON *udp = cJSON_GetObjectItemCaseSensitive(fm, "udp");
        if (cJSON_IsArray(udp) && cJSON_GetArraySize(udp) > 0) {
            const cJSON *mask = cJSON_GetArrayItem(udp, 0);
            if (jstr_copy(mask, "type", s->obfs, sizeof s->obfs) == 0)
                (void)jstr_copy(jobj(mask, "settings"), "password", s->obfs_password,
                                sizeof s->obfs_password);
        }
        (void)jstr_copy(jobj(jobj(fm, "quicParams"), "udpHop"), "ports",
                        s->port_hop, sizeof s->port_hop);
    }
    return 0;
}

/* xray's streamSettings, the same block for vless, trojan and shadowsocks */
static int xray_stream(const cJSON *ob, vl_server_t *s, char *why, size_t cap) {
    const cJSON *st = jobj(ob, "streamSettings");
    const cJSON *tcp = jobj(st, "tcpSettings");
    const char *net = jstr(st, "network");
    const char *sec = jstr(st, "security");
    char header[16] = "";

    if (!tcp) tcp = jobj(st, "rawSettings");
    (void)jstr_copy(jobj(tcp, "header"), "type", header, sizeof header);
    if (cfg_node_transport(s, net ? net : "tcp", header, why, cap) != 0) return -1;
    /* xray's own default is none, but trojan without tls is not a node anyone
       exports, and the share link convention is tls */
    if (!sec) sec = s->proto == VL_PROTO_TROJAN ? "tls" : "none";
    if (cfg_node_security(s, sec, why, cap) != 0) return -1;

    if (s->security == VL_SEC_TLS) {
        const cJSON *ts = jobj(st, "tlsSettings");
        if (!ts) ts = jobj(st, "xtlsSettings");
        (void)jstr_copy_any(ts, s->sni, sizeof s->sni, "serverName", "server_name");
        (void)jstr_copy_any(ts, s->fp, sizeof s->fp, "fingerprint", "fp");
        s->insecure = json_flag_on(ts, "allowInsecure");
    } else if (s->security == VL_SEC_REALITY) {
        const cJSON *rs = jobj(st, "realitySettings");
        (void)jstr_copy_any(rs, s->sni, sizeof s->sni, "serverName", "server_name");
        (void)jstr_copy_any(rs, s->fp, sizeof s->fp, "fingerprint", "fp");
        (void)jstr_copy_any(rs, s->pbk, sizeof s->pbk, "publicKey", "public_key");
        jstr_first(rs, "shortId", s->sid, sizeof s->sid);
        if (!s->sid[0]) jstr_first(rs, "shortIds", s->sid, sizeof s->sid);
    }

    if (s->net == VL_NET_WS) {
        const cJSON *ws = jobj(st, "wsSettings");
        (void)jstr_copy(ws, "path", s->path, sizeof s->path);
        if (jstr_copy_any(jobj(ws, "headers"), s->ws_host, sizeof s->ws_host, "Host", "host") != 0)
            (void)jstr_copy(ws, "host", s->ws_host, sizeof s->ws_host);
    } else if (s->net == VL_NET_GRPC) {
        const cJSON *gr = jobj(st, "grpcSettings");
        (void)jstr_copy_any(gr, s->path, sizeof s->path, "serviceName", "service_name");
        /* cfg_node_finish() normalizes the path once every parse path has
           had a chance to set grpc_multi; do not call it again here */
        s->grpc_multi = json_flag_on(gr, "multiMode");
    } else if (s->net == VL_NET_HTTP) {
        const cJSON *h2 = jobj(st, "httpSettings");
        (void)jstr_copy(h2, "path", s->path, sizeof s->path);
        jstr_first(h2, "host", s->ws_host, sizeof s->ws_host);
    } else if (s->net == VL_NET_XHTTP) {
        const cJSON *xh = jobj(st, "xhttpSettings");
        if (!xh) xh = jobj(st, "splithttpSettings");
        (void)jstr_copy(xh, "path", s->path, sizeof s->path);
        (void)jstr_copy(xh, "mode", s->mode, sizeof s->mode);
        (void)jstr_copy(xh, "host", s->ws_host, sizeof s->ws_host);
    }
    return 0;
}

/* the one endpoint an xray outbound names: settings.vnext[0] for vless,
   settings.servers[0] for the rest, or the same fields flat on settings as
   xray 25 also accepts */
static const cJSON *xray_endpoint(const cJSON *settings, const char *list) {
    const cJSON *a = cJSON_IsObject(settings)
        ? cJSON_GetObjectItemCaseSensitive(settings, list) : NULL;
    if (cJSON_IsArray(a)) {
        const cJSON *first = cJSON_GetArraySize(a) > 0 ? cJSON_GetArrayItem(a, 0) : NULL;
        return cJSON_IsObject(first) ? first : NULL;
    }
    return cJSON_IsObject(settings) ? settings : NULL;
}

/* the first user of an endpoint, or the endpoint itself in the flat form */
static const cJSON *xray_user(const cJSON *ep) {
    const cJSON *a = cJSON_IsObject(ep) ? cJSON_GetObjectItemCaseSensitive(ep, "users") : NULL;
    if (cJSON_IsArray(a)) {
        const cJSON *first = cJSON_GetArraySize(a) > 0 ? cJSON_GetArrayItem(a, 0) : NULL;
        return cJSON_IsObject(first) ? first : NULL;
    }
    return ep;
}

/* 1 for a node, 0 for an outbound that is not one, -1 with why for a node
   senko cannot read or run */
static int xray_node(const cJSON *ob, vl_server_t *s, char *why, size_t cap) {
    const char *proto = jstr(ob, "protocol");
    const cJSON *settings = jobj(ob, "settings");
    const cJSON *ep, *user;

    if (!proto || json_plumbing(proto)) return 0;
    if (strcmp(proto, "hysteria") == 0) {
        if (xray_hysteria(ob, settings, s) != 0) goto malformed;
        return 1;
    }
    if (strcmp(proto, "vless") == 0) {
        char id[64] = "";
        ep = xray_endpoint(settings, "vnext");
        user = xray_user(ep);
        s->proto = VL_PROTO_VLESS;
        if (jstr_copy(user, "id", id, sizeof id) != 0) goto malformed;
        cfg_node_vless_id(s, id);
        (void)jstr_copy(user, "flow", s->flow, sizeof s->flow);
        if (jstr_copy(user, "encryption", s->encryption, sizeof s->encryption) != 0)
            snprintf(s->encryption, sizeof s->encryption, "none");
    } else if (strcmp(proto, "vmess") == 0) {
        char id[64] = "";
        ep = xray_endpoint(settings, "vnext");
        user = xray_user(ep);
        s->proto = VL_PROTO_VMESS;
        if (jstr_copy(user, "id", id, sizeof id) != 0) goto malformed;
        cfg_node_vless_id(s, id);
        if (jstr_copy(user, "security", s->encryption, sizeof s->encryption) != 0)
            snprintf(s->encryption, sizeof s->encryption, "auto");
    } else if (strcmp(proto, "trojan") == 0 || strcmp(proto, "shadowsocks") == 0) {
        ep = xray_endpoint(settings, "servers");
        if (jstr_copy(ep, "password", s->pass, sizeof s->pass) != 0) goto malformed;
        if (proto[0] == 't') {
            s->proto = VL_PROTO_TROJAN;
        } else {
            s->proto = VL_PROTO_SHADOWSOCKS;
            cfg_node_ss_method(s, jstr(ep, "method") ? jstr(ep, "method") : "");
        }
    } else if (strcmp(proto, "socks") == 0 || strcmp(proto, "http") == 0) {
        ep = xray_endpoint(settings, "servers");
        user = xray_user(ep);
        s->proto = proto[0] == 's' ? VL_PROTO_SOCKS5 : VL_PROTO_HTTP;
        (void)jstr_copy(user, "user", s->user, sizeof s->user);
        (void)jstr_copy(user, "pass", s->pass, sizeof s->pass);
    } else {
        snprintf(why, cap, "%.40s", proto);
        return -1;
    }
    if (jstr_copy(ep, "address", s->host, sizeof s->host) != 0 ||
        j_port(ep, "port", &s->port) != 0)
        goto malformed;

    if (s->proto == VL_PROTO_SOCKS5 || s->proto == VL_PROTO_HTTP) {
        const char *sec = jstr(jobj(ob, "streamSettings"), "security");
        if (sec && strcmp(sec, "tls") == 0) {
            if (s->proto == VL_PROTO_SOCKS5) {
                snprintf(why, cap, "socks over tls");
                return -1;
            }
            s->proto = VL_PROTO_HTTPS;
        }
        return 1;
    }
    return xray_stream(ob, s, why, cap) == 0 ? 1 : -1;

malformed:
    snprintf(why, cap, "malformed profile entry");
    return -1;
}

/* sing-box's transport object: ws, grpc, http and httpupgrade carry their own
   fields, and there is no tcp header disguise */
static int singbox_transport(const cJSON *ob, vl_server_t *s, char *why, size_t cap) {
    const cJSON *tr = jobj(ob, "transport");
    const char *type = jstr(tr, "type");
    if (cfg_node_transport(s, type ? type : "tcp", NULL, why, cap) != 0) return -1;
    if (s->net == VL_NET_WS) {
        (void)jstr_copy(tr, "path", s->path, sizeof s->path);
        const cJSON *hdr = jobj(tr, "headers");
        jstr_first(hdr, "Host", s->ws_host, sizeof s->ws_host);
        if (!s->ws_host[0]) jstr_first(hdr, "host", s->ws_host, sizeof s->ws_host);
    } else if (s->net == VL_NET_GRPC) {
        (void)jstr_copy_any(tr, s->path, sizeof s->path, "service_name", "serviceName");
        s->grpc_multi = json_flag_on(tr, "multi_mode") || json_flag_on(tr, "multiMode");
    } else if (s->net == VL_NET_HTTP) {
        (void)jstr_copy(tr, "path", s->path, sizeof s->path);
        jstr_first(tr, "host", s->ws_host, sizeof s->ws_host);
    }
    return 0;
}

/* sing-box writes tls as its own block with an enabled flag, reality nested
   inside it */
static void singbox_tls(const cJSON *ob, vl_server_t *s) {
    const cJSON *tls = jobj(ob, "tls");
    const cJSON *reality = jobj(tls, "reality");
    if (!tls) {
        s->security = s->proto == VL_PROTO_TROJAN ? VL_SEC_TLS : VL_SEC_NONE;
        return;
    }
    if (!json_flag_on(tls, "enabled")) {
        s->security = VL_SEC_NONE;
        return;
    }
    s->security = json_flag_on(reality, "enabled") ? VL_SEC_REALITY : VL_SEC_TLS;
    (void)jstr_copy_any(tls, s->sni, sizeof s->sni, "server_name", "serverName");
    s->insecure = json_flag_on(tls, "insecure");
    if (json_flag_on(jobj(tls, "utls"), "enabled"))
        (void)jstr_copy(jobj(tls, "utls"), "fingerprint", s->fp, sizeof s->fp);
    if (s->security == VL_SEC_REALITY) {
        (void)jstr_copy_any(reality, s->pbk, sizeof s->pbk, "public_key", "publicKey");
        jstr_first(reality, "short_id", s->sid, sizeof s->sid);
        if (!s->sid[0]) jstr_first(reality, "shortId", s->sid, sizeof s->sid);
    }
}

/* sing-box's server_ports ("20000:30000" or a list of them) in the hop list
   form share links use ("20000-30000,443") */
static int singbox_port_hop(const cJSON *ob, vl_server_t *s) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(ob, "server_ports");
    char list[sizeof s->port_hop];
    size_t n = 0;
    uint16_t first = 0;
    int i, count;
    if (!v) return 0;
    count = cJSON_IsArray(v) ? cJSON_GetArraySize(v) : 1;
    for (i = 0; i < count; ++i) {
        const cJSON *e = cJSON_IsArray(v) ? cJSON_GetArrayItem(v, i) : v;
        if (!cJSON_IsString(e) || !e->valuestring) return -1;
        for (const char *c = e->valuestring; *c; ++c) {
            if (n + 2 >= sizeof list) return -1;
            list[n++] = *c == ':' ? '-' : *c;
        }
        if (i + 1 < count) list[n++] = ',';
    }
    list[n] = '\0';
    if (cfg_parse_port_hop(list, n, s->port_hop, sizeof s->port_hop, &first) != 0)
        return -1;
    if (!s->port) s->port = first;
    return 0;
}

static int singbox_node(const cJSON *ob, vl_server_t *s, char *why, size_t cap) {
    const char *type = jstr(ob, "type");
    if (!type || json_plumbing(type)) return 0;

    if (strcmp(type, "vless") == 0) {
        char id[64] = "";
        s->proto = VL_PROTO_VLESS;
        if (jstr_copy(ob, "uuid", id, sizeof id) != 0) goto malformed;
        cfg_node_vless_id(s, id);
        (void)jstr_copy(ob, "flow", s->flow, sizeof s->flow);
        snprintf(s->encryption, sizeof s->encryption, "none");
    } else if (strcmp(type, "vmess") == 0) {
        char id[64] = "";
        s->proto = VL_PROTO_VMESS;
        if (jstr_copy(ob, "uuid", id, sizeof id) != 0) goto malformed;
        cfg_node_vless_id(s, id);
        if (jstr_copy(ob, "security", s->encryption, sizeof s->encryption) != 0)
            snprintf(s->encryption, sizeof s->encryption, "auto");
    } else if (strcmp(type, "trojan") == 0 || strcmp(type, "hysteria2") == 0) {
        s->proto = type[0] == 't' ? VL_PROTO_TROJAN : VL_PROTO_HYSTERIA2;
        if (jstr_copy(ob, "password", s->pass, sizeof s->pass) != 0) goto malformed;
    } else if (strcmp(type, "shadowsocks") == 0) {
        const char *plugin = jstr(ob, "plugin");
        if (plugin && plugin[0]) {
            snprintf(why, cap, "shadowsocks plugin %.32s", plugin);
            return -1;
        }
        s->proto = VL_PROTO_SHADOWSOCKS;
        if (jstr_copy(ob, "password", s->pass, sizeof s->pass) != 0) goto malformed;
        cfg_node_ss_method(s, jstr(ob, "method") ? jstr(ob, "method") : "");
    } else if (strcmp(type, "socks") == 0 || strcmp(type, "http") == 0) {
        const char *version = jstr(ob, "version");
        if (version && version[0] == '4') {
            snprintf(why, cap, "socks4");
            return -1;
        }
        s->proto = type[0] == 's' ? VL_PROTO_SOCKS5 : VL_PROTO_HTTP;
        (void)jstr_copy(ob, "username", s->user, sizeof s->user);
        (void)jstr_copy(ob, "password", s->pass, sizeof s->pass);
    } else {
        snprintf(why, cap, "%.40s", type);
        return -1;
    }
    if (jstr_copy(ob, "server", s->host, sizeof s->host) != 0) goto malformed;
    (void)j_port(ob, "server_port", &s->port);

    switch (s->proto) {
    case VL_PROTO_VLESS:
    case VL_PROTO_VMESS:
    case VL_PROTO_TROJAN:
        if (singbox_transport(ob, s, why, cap) != 0) return -1;
        singbox_tls(ob, s);
        break;
    case VL_PROTO_HYSTERIA2: {
        const cJSON *obfs = jobj(ob, "obfs");
        singbox_tls(ob, s);
        if (jstr_copy(obfs, "type", s->obfs, sizeof s->obfs) == 0)
            (void)jstr_copy(obfs, "password", s->obfs_password, sizeof s->obfs_password);
        if (singbox_port_hop(ob, s) != 0) goto malformed;
        break;
    }
    case VL_PROTO_HTTP:
        if (json_flag_on(jobj(ob, "tls"), "enabled")) s->proto = VL_PROTO_HTTPS;
        break;
    default:
        break;
    }
    if (!s->port) goto malformed;
    return 1;

malformed:
    snprintf(why, cap, "malformed profile entry");
    return -1;
}

/* one xray or sing-box outbound: 1 and a usable server, 0 for plumbing, -1
   with why for a node senko skips */
static int json_node(const cJSON *ob, const char *remarks, vl_server_t *s,
                     char *why, size_t cap) {
    char tag[128] = "";
    int r;
    memset(s, 0, sizeof *s);
    why[0] = '\0';
    if (!cJSON_IsObject(ob)) return 0;
    r = jstr(ob, "protocol") ? xray_node(ob, s, why, cap) : singbox_node(ob, s, why, cap);
    if (r != 1) return r;
    cfg_node_finish(s);

    /* the outbounds of one profile share its name, so the app lists them as one
       row and tries them in turn */
    (void)jstr_copy(ob, "tag", tag, sizeof tag);
    if (remarks && remarks[0])
        snprintf(s->remark, sizeof s->remark, "%.80s", remarks);
    else if (tag[0])
        snprintf(s->remark, sizeof s->remark, "%s", tag);
    else
        snprintf(s->remark, sizeof s->remark, "%.120s:%u", s->host, (unsigned)s->port);
    return cfg_validate_server(s, why, cap) ? 1 : -1;
}

/* the endpoint of an outbound json_node refused, for its placeholder row:
   xray's settings (vnext, servers or flat) or sing-box's server fields */
static void json_fields(const cJSON *ob, char *host, size_t host_cap, uint16_t *port) {
    const cJSON *settings = jobj(ob, "settings");
    const cJSON *ep = NULL;
    host[0] = '\0';
    *port = 0;
    if (settings) {
        ep = xray_endpoint(settings, "vnext");
        if (!jstr(ep, "address")) ep = xray_endpoint(settings, "servers");
        (void)jstr_copy(ep, "address", host, host_cap);
        (void)j_port(ep, "port", port);
    } else {
        (void)jstr_copy(ob, "server", host, host_cap);
        (void)j_port(ob, "server_port", port);
    }
}

static void json_collect_outbounds(const cJSON *outbounds, const char *remarks,
                                   vl_server_t *out, size_t max, size_t *count,
                                   cfg_import_stats_t *st) {
    const cJSON *ob;
    if (!cJSON_IsArray(outbounds) || !out || !count) return;
    cJSON_ArrayForEach(ob, outbounds) {
        char why[96];
        if (*count >= max) break;
        int r = json_node(ob, remarks, &out[*count], why, sizeof why);
        if (r == 1) {
            (*count)++;
        } else if (r < 0) {
            char host[256] = "";
            uint16_t port = 0;
            json_fields(ob, host, sizeof host, &port);
            cfg_import_skip(st, out, max, count,
                            remarks && remarks[0] ? remarks : jstr(ob, "tag"),
                            host, port, why[0] ? why : "malformed profile entry");
        }
    }
}

/* a profile is an xray config ("outbounds", named by "remarks") or a sing-box
   one ("outbounds", plus wireguard under "endpoints" since 1.11) */
static void json_collect_profile(const cJSON *profile, vl_server_t *out, size_t max,
                                 size_t *count, cfg_import_stats_t *st) {
    char remarks[256] = "";
    (void)jstr_copy(profile, "remarks", remarks, sizeof remarks);
    json_collect_outbounds(cJSON_GetObjectItemCaseSensitive(profile, "outbounds"),
                           remarks, out, max, count, st);
    json_collect_outbounds(cJSON_GetObjectItemCaseSensitive(profile, "endpoints"),
                           remarks, out, max, count, st);
}

static int parse_xray_json(const char *blob, size_t blob_len,
                           vl_server_t *out, size_t max, size_t *count,
                           cfg_import_stats_t *st) {
    cJSON *root;
    if (!blob || !out || !count || max == 0) return -1;

    root = cJSON_ParseWithLength(blob, blob_len);
    if (!root) return -1;

    if (cJSON_IsArray(root)) {
        const cJSON *item;
        cJSON_ArrayForEach(item, root) {
            if (*count >= max) break;
            if (cJSON_IsObject(item)) json_collect_profile(item, out, max, count, st);
        }
    } else if (cJSON_IsObject(root)) {
        json_collect_profile(root, out, max, count, st);
    }

    cJSON_Delete(root);
    return 0;
}

static int looks_like_happ(const char *b, size_t n) {
    return n >= 7 && (strncmp(b, "happ://", 7) == 0 || strncmp(b, "HAPP://", 7) == 0);
}

cfg_content_t cfg_content_kind(const char *blob, size_t blob_len) {
    if (!blob || blob_len == 0) return CFG_CONTENT_UNKNOWN;
    if (looks_like_happ(blob, blob_len)) return CFG_CONTENT_HAPP;
    if (looks_like_json(blob, blob_len)) return CFG_CONTENT_XRAY_JSON;
    if (profiles_looks_like_clash(blob, blob_len)) return CFG_CONTENT_CLASH;
    if (profiles_looks_like_surge(blob, blob_len)) return CFG_CONTENT_SURGE;
    if (looks_like_links(blob, blob_len)) return CFG_CONTENT_LINKS;

    size_t cap = b64_decoded_maxlen(blob_len);
    if (cap == 0 || cap > (size_t)(2 * 1024 * 1024)) return CFG_CONTENT_UNKNOWN;
    unsigned char *scratch = (unsigned char *)malloc(cap);
    if (!scratch) return CFG_CONTENT_UNKNOWN;
    size_t dec_len = 0;
    cfg_content_t kind = CFG_CONTENT_UNKNOWN;
    if (b64_decode(blob, blob_len, scratch, cap, &dec_len) == 0 && dec_len > 0 &&
        (looks_like_json((const char *)scratch, dec_len) ||
         looks_like_links((const char *)scratch, dec_len)))
        kind = CFG_CONTENT_BASE64;
    free(scratch);
    return kind;
}

static void trim_ends(char *s) {
    size_t n;
    char *start = s;
    if (!s) return;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        ++start;
    if (start != s) memmove(s, start, strlen(start) + 1);
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
}

/* one line, an http(s) scheme, a host, and nothing cfg_parse_link can dial:
   that shape is a subscription endpoint, not a CONNECT proxy */
static int looks_like_subscription_url(const char *text) {
    vl_server_t probe;
    size_t i;
    if (!text) return 0;
    if (strncmp(text, "http://", 7) != 0 && strncmp(text, "https://", 8) != 0)
        return 0;
    for (i = 0; text[i]; ++i) {
        if (text[i] == '\n' || text[i] == '\r' || text[i] == ' ' || text[i] == '\t')
            return 0;
    }
    if (i < 12 || i >= 512) return 0;
    return cfg_parse_link(text, &probe) != CFG_OK;
}

int cfg_subscription_url(const char *blob, size_t blob_len,
                         char *out, size_t out_cap) {
    char plain[16384];
    char tmp[8192];
    size_t n;
    const char *text;

    if (!blob || blob_len == 0 || !out || out_cap == 0) return -1;
    out[0] = '\0';

    n = blob_len < sizeof tmp - 1 ? blob_len : sizeof tmp - 1;
    memcpy(tmp, blob, n);
    tmp[n] = '\0';
    trim_ends(tmp);

    if (looks_like_happ(tmp, strlen(tmp))) {
        if (happ_unwrap(tmp, plain, sizeof plain) != 0) return -1;
        trim_ends(plain);
        text = plain;
    } else {
        text = tmp;
    }
    if (!looks_like_subscription_url(text)) return -1;
    if (strlen(text) + 1 > out_cap) return -1;
    memcpy(out, text, strlen(text) + 1);
    return 0;
}

/* the body without a byte order mark, the whitespace around it, or the
   "#profile-title: ..." and "//profile-title: ..." lines hiddify style panels
   put above a base64 or sing-box json body, which hid both formats */
static void trim_body(const char **blob, size_t *len) {
    const char *b = *blob;
    size_t n = *len;
    if (n >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB &&
        (unsigned char)b[2] == 0xBF) {
        b += 3;
        n -= 3;
    }
    for (;;) {
        while (n && (*b == ' ' || *b == '\t' || *b == '\r' || *b == '\n')) { ++b; --n; }
        if (!(n && *b == '#') && !(n >= 2 && b[0] == '/' && b[1] == '/')) break;
        while (n && *b != '\n' && *b != '\r') { ++b; --n; }
    }
    while (n && (b[n - 1] == ' ' || b[n - 1] == '\t' || b[n - 1] == '\r' || b[n - 1] == '\n')) --n;
    *blob = b;
    *len = n;
}

/* a body that is base64 and nothing else, wrapped or not */
static int looks_like_base64(const char *b, size_t n) {
    size_t alnum = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)b[i];
        if (isalnum(c)) { ++alnum; continue; }
        if (c == '+' || c == '/' || c == '-' || c == '_' || c == '=' ||
            c == '\r' || c == '\n' || c == ' ' || c == '\t')
            continue;
        return 0;
    }
    return alnum >= 8;
}

static cfg_status_t parse_body(const char *blob, size_t blob_len, vl_server_t *out,
                               size_t max, size_t *count, cfg_import_stats_t *st,
                               int depth) {
    trim_body(&blob, &blob_len);
    if (!blob_len) return CFG_OK;

    if (depth > 3) return CFG_OK;
    /* one happ link can carry a whole feed: a list, base64 or json */
    if (blob_len >= 15 && strncasecmp(blob, "happ://routing/", 15) == 0 &&
        !memchr(blob, '\n', blob_len))
        return CFG_OK;
    if (looks_like_happ(blob, blob_len) && !memchr(blob, '\n', blob_len)) {
        char tmp[8192];
        char plain[16384];
        if (blob_len >= sizeof tmp) return CFG_ERR_TOO_LONG;
        memcpy(tmp, blob, blob_len);
        tmp[blob_len] = '\0';
        if (happ_unwrap(tmp, plain, sizeof plain) != 0) {
            cfg_import_stats_add(st, "happ link that does not open");
            return CFG_OK;
        }
        return parse_body(plain, strlen(plain), out, max, count, st, depth + 1);
    }

/* JSON must win because DNS URLs inside it look like standalone server links */
    if (looks_like_json(blob, blob_len) &&
        parse_xray_json(blob, blob_len, out, max, count, st) == 0)
        return CFG_OK;

/* the foreign client profiles are checked before the link scan because a clash
   document embeds urls in its dns and rule sections */
    if (profiles_looks_like_clash(blob, blob_len)) {
        *count += profiles_parse_clash_ex(blob, blob_len, out + *count, max - *count, st);
        return CFG_OK;
    }
    if (profiles_looks_like_surge(blob, blob_len)) {
        *count += profiles_parse_surge_ex(blob, blob_len, out + *count, max - *count, st);
        return CFG_OK;
    }

    if (looks_like_base64(blob, blob_len)) {
        size_t cap = b64_decoded_maxlen(blob_len);
        if (cap > (size_t)(4 * 1024 * 1024)) return CFG_ERR_TOO_LONG;
        unsigned char *plain = malloc(cap + 1);
        if (!plain) return CFG_ERR_NO_MEMORY;
        size_t plain_len = 0;
        cfg_status_t r = CFG_OK;
        if (b64_decode(blob, blob_len, plain, cap, &plain_len) == 0) {
            plain[plain_len] = '\0';
            r = parse_body((const char *)plain, plain_len, out, max, count, st, depth + 1);
        }
        free(plain);
        return r;
    }

    parse_link_lines(blob, blob_len, out, max, count, st);
    return CFG_OK;
}

/* the end of the last line that arrived whole, or 0 */
static size_t whole_lines_end(const char *b, size_t n) {
    while (n && b[n - 1] != '\n') --n;
    return n;
}

/* the end of the last object or array item of a top level json array that
   arrived whole, or 0. a remnawave xray json feed repeats its routing rules in
   every profile and passes 512 KB at around fifty nodes */
static size_t json_array_items_end(const char *b, size_t n) {
    size_t end = 0;
    int depth = 0, in_string = 0, escaped = 0;
    for (size_t i = 0; i < n; ++i) {
        char c = b[i];
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
            continue;
        }
        if (c == '"') {
            in_string = 1;
        } else if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            if (--depth == 1) end = i + 1;
            else if (depth <= 0) return 0;
        }
    }
    return end;
}

int cfg_subscription_prefix(char *blob, size_t *len) {
    const char *b;
    size_t n, end;
    if (!blob || !len) return -1;
    b = blob;
    n = *len;
    trim_body(&b, &n);
    if (!n || looks_like_happ(b, n)) return -1;
    if (looks_like_json(b, n)) {
        size_t off = (size_t)(b - blob);
        if (b[0] != '[') return -1;
        end = json_array_items_end(b, n);
        /* the cut item is dropped, so the closing bracket always has room */
        if (!end || end >= n) return -1;
        blob[off + end] = ']';
        *len = off + end + 1;
        return 0;
    }
    if (profiles_looks_like_clash(b, n)) {
/* an entry spans lines, so the cut goes before the last list item rather
   than after the last line */
        end = whole_lines_end(b, n);
        while (end) {
            size_t start = end - 1;
            while (start && b[start - 1] != '\n') --start;
            size_t i = start;
            while (i < end && (b[i] == ' ' || b[i] == '\t')) ++i;
            end = start;
            if (i < n && b[i] == '-') break;
        }
    } else if (!profiles_looks_like_surge(b, n) && looks_like_base64(b, n)) {
/* decode whole quartets only, then keep the lines of the text they carry */
        size_t sig = 0, cut = 0;
        for (size_t i = 0; i < n; ++i) {
            if (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n') continue;
            if (++sig % 4 == 0) cut = i + 1;
        }
        unsigned char *plain = malloc(b64_decoded_maxlen(cut) + 1);
        size_t plain_len = 0;
        if (!plain) return -1;
        if (!cut || b64_decode(b, cut, plain, b64_decoded_maxlen(cut), &plain_len) != 0 ||
            plain_len > *len) {
            free(plain);
            return -1;
        }
        memcpy(blob, plain, plain_len);
        free(plain);
        *len = plain_len;
        return cfg_subscription_prefix(blob, len);
    } else {
        end = whole_lines_end(b, n);
    }
    if (!end) return -1;
    *len = (size_t)(b - blob) + end;
    return 0;
}

cfg_status_t cfg_parse_subscription_ex(const char *blob, size_t blob_len,
                                       vl_server_t *out, size_t max_servers,
                                       size_t *out_count, cfg_import_stats_t *stats) {
    cfg_import_stats_t local;
    if (!stats) stats = &local;
    memset(stats, 0, sizeof *stats);
    if (!blob || !out || !out_count || max_servers == 0) return CFG_ERR_BAD_ARG;
    *out_count = 0;
    return parse_body(blob, blob_len, out, max_servers, out_count, stats, 0);
}

cfg_status_t cfg_parse_subscription_rows(const char *blob, size_t blob_len,
                                         vl_server_t *out, size_t max_servers,
                                         size_t *out_count, cfg_import_stats_t *stats) {
    cfg_import_stats_t local;
    if (!stats) stats = &local;
    memset(stats, 0, sizeof *stats);
    stats->keep_unsupported = 1;
    if (!blob || !out || !out_count || max_servers == 0) return CFG_ERR_BAD_ARG;
    *out_count = 0;
    return parse_body(blob, blob_len, out, max_servers, out_count, stats, 0);
}

cfg_status_t cfg_parse_subscription(const char *blob, size_t blob_len,
                                    vl_server_t *out, size_t max_servers,
                                    size_t *out_count) {
    return cfg_parse_subscription_ex(blob, blob_len, out, max_servers, out_count, NULL);
}

rules_status_t cfg_parse_rule(const char *text, size_t len, rule_t *out) {
    return rules_parse(text, len, out);
}
