#include "config.h"
#include "b64.h"
#include "happ.h"
#include "profiles.h"

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

static int copy_span(const char *s, const char *e, char *dst, size_t cap) {
    size_t n = (size_t)(e - s);
    if (n + 1 > cap) return -1;
    memcpy(dst, s, n);
    dst[n] = '\0';
    return 0;
}

static int copy_query_value(const char *v, size_t vlen, char *dst, size_t cap) {
/* query parameter values are the one place where '+' encodes a space */
    return url_percent_decode_ex(v, vlen, dst, cap, 1) >= 0 ? 0 : -1;
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

    if (s->proto == VL_PROTO_TROJAN) {
        if (!s->pass[0]) {
            cfg_reason(reason, reason_cap, "trojan requires password");
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
            cfg_reason(reason, reason_cap, "unsupported shadowsocks cipher");
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
/* the go core's finalmask udp mask only builds a salamander conn */
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

/* panel templates prepend a long shared banner to every node name, so clipping
   a name at a byte boundary can leave every server in a feed reading the same.
   decode into a wide scratch first, then step back to a codepoint boundary so a
   name that still has to be shortened stays valid utf-8 for the control line */
static void copy_remark(const char *src, size_t src_len, char *dst, size_t cap) {
    char wide[1024];
    size_t n;

    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (url_percent_decode(src, src_len, wide, sizeof wide) < 0) {
        n = src_len < sizeof wide - 1 ? src_len : sizeof wide - 1;
        memcpy(wide, src, n);
        wide[n] = '\0';
    }

    n = strlen(wide);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)wide[n] & 0xC0) == 0x80) --n;
    }
    memcpy(dst, wide, n);
    dst[n] = '\0';
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
    } else if (KEY_IS("allowInsecure") || KEY_IS("insecure")) {
        char tmp[8];
        if (copy_span(v, v + vlen, tmp, sizeof tmp) == 0)
            s->insecure = (strcmp(tmp, "1") == 0 || strcmp(tmp, "true") == 0);
    } else if (KEY_IS("pinSHA256") || KEY_IS("pinsha256")) {
        copy_query_value(v, vlen, s->pin_sha256, sizeof s->pin_sha256);
    } else if (KEY_IS("obfs")) {
        copy_query_value(v, vlen, s->obfs, sizeof s->obfs);
    } else if (KEY_IS("obfs-password") || KEY_IS("obfs_password")) {
        copy_query_value(v, vlen, s->obfs_password, sizeof s->obfs_password);
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

/* happ:// unwrapping and the ss:// legacy-base64 form both re-enter this
   function with content they themselves decoded. Both are driven by bytes an
   untrusted subscription panel supplied, so a crafted chain that keeps
   re-decoding into another instance of itself needs a hard stop rather than
   riding the C stack down. */
#define CFG_PARSE_LINK_MAX_DEPTH 8

static cfg_status_t cfg_parse_link_inner(const char *uri, vl_server_t *out, int depth) {
    if (!uri || !out) return CFG_ERR_BAD_ARG;
    memset(out, 0, sizeof *out);
    if (depth > CFG_PARSE_LINK_MAX_DEPTH) return CFG_ERR_SCHEME;

/* happ://crypt... unwraps to vless/socks/http before scheme match */
    if (strncmp(uri, "happ://", 7) == 0 || strncmp(uri, "HAPP://", 7) == 0) {
        char plain[8192];
        char line[1024];
        const char *p, *end, *nl;
        if (happ_unwrap(uri, plain, sizeof plain) != 0)
            return CFG_ERR_SCHEME;
        if (cfg_parse_link_inner(plain, out, depth + 1) == CFG_OK)
            return CFG_OK;
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
                if (cfg_parse_link_inner(line, out, depth + 1) == CFG_OK)
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
    } else if (strncmp(p, "trojan://", 9) == 0) {
        proto = VL_PROTO_TROJAN;
        slen = 9;
    } else if (strncmp(p, "ss://", 5) == 0) {
        proto = VL_PROTO_SHADOWSOCKS;
        slen = 5;
    } else if (strncmp(p, "hysteria2://", 12) == 0) {
        proto = VL_PROTO_HYSTERIA2;
        slen = 12;
    } else if (strncmp(p, "hy2://", 6) == 0) {
        proto = VL_PROTO_HYSTERIA2;
        slen = 6;
    } else {
        return CFG_ERR_SCHEME;
    }
    p += slen;
    out->proto = proto;

    const char *frag = strchr(p, '#');
    const char *body_end = frag ? frag : (p + strlen(p));

    if (proto == VL_PROTO_SHADOWSOCKS) {
        const char *at_check = memchr(p, '@', (size_t)(body_end - p));
        if (!at_check) {
            unsigned char dec[1024];
            size_t dec_len = 0;
            if (b64_decode(p, (size_t)(body_end - p), dec, sizeof dec - 1, &dec_len) == 0 && dec_len > 0) {
                dec[dec_len] = '\0';
                char reconstituted[1280];
                int n = frag
                    ? snprintf(reconstituted, sizeof reconstituted, "ss://%s%s", (char *)dec, frag)
                    : snprintf(reconstituted, sizeof reconstituted, "ss://%s", (char *)dec);
                if (n < 0 || (size_t)n >= sizeof reconstituted) return CFG_ERR_TOO_LONG;
                return cfg_parse_link_inner(reconstituted, out, depth + 1);
            }
        }
    }

    const char *at = memchr(p, '@', (size_t)(body_end - p));
    if (at) {
        if (proto == VL_PROTO_VLESS) {
            if (copy_span(p, at, out->uuid, sizeof out->uuid) != 0) return CFG_ERR_TOO_LONG;
        } else if (proto == VL_PROTO_TROJAN || proto == VL_PROTO_HYSTERIA2) {
/* the whole userinfo is one opaque auth token, not a user:pass pair: a
   hysteria2 password commonly contains a colon of its own */
            char encoded_pass[sizeof out->pass];
            if (copy_span(p, at, encoded_pass, sizeof encoded_pass) != 0 ||
                url_percent_decode(encoded_pass, strlen(encoded_pass), out->pass, sizeof out->pass) < 0)
                return CFG_ERR_TOO_LONG;
        } else if (proto == VL_PROTO_SHADOWSOCKS) {
            const char *colon = memchr(p, ':', (size_t)(at - p));
            if (!colon) {
                unsigned char udec[256];
                size_t udlen = 0;
                if (b64_decode(p, (size_t)(at - p), udec, sizeof udec - 1, &udlen) == 0 && udlen > 0) {
                    udec[udlen] = '\0';
                    char *uc = strchr((char *)udec, ':');
                    if (uc) {
                        *uc = '\0';
                        snprintf(out->user, sizeof out->user, "%s", (char *)udec);
                        snprintf(out->encryption, sizeof out->encryption, "%s", (char *)udec);
                        snprintf(out->pass, sizeof out->pass, "%s", uc + 1);
                    }
                }
            } else {
                char enc_u[sizeof out->user];
                char enc_p[sizeof out->pass];
                if (copy_span(p, colon, enc_u, sizeof enc_u) != 0 ||
                    copy_span(colon + 1, at, enc_p, sizeof enc_p) != 0)
                    return CFG_ERR_TOO_LONG;
                url_percent_decode(enc_u, strlen(enc_u), out->user, sizeof out->user);
                snprintf(out->encryption, sizeof out->encryption, "%s", out->user);
                url_percent_decode(enc_p, strlen(enc_p), out->pass, sizeof out->pass);
            }
        } else {
            const char *colon = memchr(p, ':', (size_t)(at - p));
            char encoded_user[sizeof out->user];
            char encoded_pass[sizeof out->pass];
            if (colon) {
                if (copy_span(p, colon, encoded_user, sizeof encoded_user) != 0 ||
                    copy_span(colon + 1, at, encoded_pass, sizeof encoded_pass) != 0)
                    return CFG_ERR_TOO_LONG;
                if (url_percent_decode(encoded_user, strlen(encoded_user), out->user,
                                       sizeof out->user) < 0 ||
                    url_percent_decode(encoded_pass, strlen(encoded_pass), out->pass,
                                       sizeof out->pass) < 0)
                    return CFG_ERR_TOO_LONG;
            } else {
                if (copy_span(p, at, encoded_user, sizeof encoded_user) != 0 ||
                    url_percent_decode(encoded_user, strlen(encoded_user), out->user,
                                       sizeof out->user) < 0)
                    return CFG_ERR_TOO_LONG;
            }
        }
        p = at + 1;
    } else {
        if (proto == VL_PROTO_VLESS || proto == VL_PROTO_TROJAN ||
            proto == VL_PROTO_SHADOWSOCKS || proto == VL_PROTO_HYSTERIA2)
            return CFG_ERR_NO_AT;
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
/* hysteria2 alone carries a "multi-port" hop list in place of one port
   (host:123,5000-6000): the go core dials a random port from it and hops */
    if (proto == VL_PROTO_HYSTERIA2 &&
        (memchr(pp, ',', (size_t)(hp_end - pp)) || memchr(pp, '-', (size_t)(hp_end - pp)))) {
        uint16_t first_port = 0;
        if (cfg_parse_port_hop(pp, (size_t)(hp_end - pp), out->port_hop,
                               sizeof out->port_hop, &first_port) != 0)
            return CFG_ERR_BAD_PORT;
        out->port = first_port;
    } else {
        for (; pp < hp_end; ++pp) {
            if (*pp < '0' || *pp > '9') return CFG_ERR_BAD_PORT;
            port = port * 10 + (unsigned long)(*pp - '0');
            if (port > 65535) return CFG_ERR_BAD_PORT;
        }
        if (port == 0) return CFG_ERR_BAD_PORT;
        out->port = (uint16_t)port;
    }

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

    if (out->proto == VL_PROTO_HYSTERIA2) {
/* quic carries its own tls handshake; there is no plain or reality variant */
        out->security = VL_SEC_TLS;
        out->net = VL_NET_TCP;
        if (out->sni[0] == '\0') snprintf(out->sni, sizeof out->sni, "%s", out->host);
    } else if (out->proto == VL_PROTO_TROJAN) {
        if (out->security == VL_SEC_UNKNOWN) out->security = VL_SEC_TLS;
        if (out->net == VL_NET_UNKNOWN) out->net = VL_NET_TCP;
    } else if (out->proto == VL_PROTO_SHADOWSOCKS) {
        if (out->security == VL_SEC_UNKNOWN) out->security = VL_SEC_NONE;
        if (out->net == VL_NET_UNKNOWN) out->net = VL_NET_TCP;
        if (!out->user[0]) snprintf(out->user, sizeof out->user, "chacha20-ietf-poly1305");
    }

    if (frag) copy_remark(frag + 1, strlen(frag + 1), out->remark, sizeof out->remark);

    return CFG_OK;
}

cfg_status_t cfg_parse_link(const char *uri, vl_server_t *out) {
    return cfg_parse_link_inner(uri, out, 0);
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

static void parse_link_lines(const char *text, size_t len,
                             vl_server_t *out, size_t max, size_t *count);

/* a panel that answers with its own web page still carries the nodes inside the
   markup, wrapped in quotes and tags. only the schemes that are always a node
   are picked out of a line: an http(s) url inside a page is a page link far
   more often than it is a CONNECT proxy */
static int embedded_link_start(const char *p) {
    return strncmp(p, "vless://", 8) == 0 ||
           strncmp(p, "socks5://", 9) == 0 ||
           strncmp(p, "happ://", 7) == 0 ||
           strncmp(p, "HAPP://", 7) == 0 ||
           strncmp(p, "trojan://", 9) == 0 ||
           strncmp(p, "ss://", 5) == 0;
}

static int link_token_char(char c) {
    return !(c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\' ||
             c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0');
}

static void scan_embedded_links(const char *line, vl_server_t *out,
                                size_t max, size_t *count) {
    char token[4096];
    size_t i = 0;
    size_t n = strlen(line);
    while (i < n && *count < max) {
        size_t j;
        if (!embedded_link_start(line + i)) { ++i; continue; }
        j = i;
        while (j < n && link_token_char(line[j])) ++j;
        if (j - i > 0 && j - i < sizeof token) {
            memcpy(token, line + i, j - i);
            token[j - i] = '\0';
            if (token[0] == 'h' || token[0] == 'H') {
                char plain[8192];
                if (happ_unwrap(token, plain, sizeof plain) == 0)
                    parse_link_lines(plain, strlen(plain), out, max, count);
            } else if (cfg_parse_link(token, &out[*count]) == CFG_OK &&
                       cfg_validate_server(&out[*count], NULL, 0)) {
                (*count)++;
            }
        }
        i = j > i ? j : i + 1;
    }
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
            size_t before = *count;
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
            if (*count == before)
                scan_embedded_links(line, out, max, count);
        }
        if (!nl) break;
        p = nl + 1;
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
            return "this address only hands back its own link: the provider has "
                   "not published a subscription feed behind it";
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
static void j_copy_short_id_key(const cJSON *reality, const char *key,
                                char *dst, size_t cap) {
    const cJSON *sid;
    if (!reality || !key || !dst || cap == 0) return;
    sid = cJSON_GetObjectItemCaseSensitive(reality, key);
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

static void j_copy_short_id(const cJSON *reality, char *dst, size_t cap) {
    j_copy_short_id_key(reality, "shortId", dst, cap);
}

/* sing-box spells it short_id, xray shortId, and a hand-merged config can carry
   either */
static void j_copy_short_id_any(const cJSON *reality, char *dst, size_t cap) {
    j_copy_short_id_key(reality, "short_id", dst, cap);
    if (dst[0] == '\0') j_copy_short_id_key(reality, "shortId", dst, cap);
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

/* both formats name the same wire transports, and each has spelled some of them
   more than one way across versions */
static void json_set_network(vl_server_t *s, const char *network) {
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
}

/* sing-box gates tls and reality on their own boolean, and a block that is
   present but disabled means plain tcp, not tls */
static int json_flag_on(const cJSON *obj, const char *key) {
    const cJSON *v;
    if (!cJSON_IsObject(obj)) return 0;
    v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? 1 : 0;
    if (cJSON_IsNumber(v)) return v->valuedouble != 0.0;
    return 0;
}

/* the defaults are the same in both formats: an absent sni follows the host,
   and a websocket without a Host header follows the sni */
static int json_server_finish(vl_server_t *s, const cJSON *ob,
                              const char *remarks, int multi) {
    char tag[128];

    /* the ui keeps same-name profiles as one row and tries every endpoint, so
       an endpoint suffix here would split one provider location into copies */
    (void)multi;

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
    if (remarks && remarks[0]) {
        size_t rl = strlen(remarks);
        if (rl > 80) rl = 80;
        snprintf(s->remark, sizeof s->remark, "%.*s", (int)rl, remarks);
    } else if (tag[0]) {
        snprintf(s->remark, sizeof s->remark, "%s", tag);
    } else {
        snprintf(s->remark, sizeof s->remark, "%.120s:%u",
                 s->host, (unsigned)s->port);
    }

    if (!cfg_validate_server(s, NULL, 0)) return -1;
    return 0;
}

/* this fork's own hysteria outbound shape (see go_config.c's append_outbound):
   the destination sits in settings, auth and quic live under
   streamSettings.hysteriaSettings, and finalmask carries obfuscation and
   port hopping when the node uses either */
static int xray_hysteria_to_server(const cJSON *ob, const cJSON *settings,
                                   const char *remarks, int multi,
                                   vl_server_t *s) {
    const cJSON *stream = cJSON_GetObjectItemCaseSensitive(ob, "streamSettings");
    const cJSON *hy, *ts, *fm;

    if (jstr_copy(settings, "address", s->host, sizeof s->host) != 0) return -1;
    if (j_port(settings, "port", &s->port) != 0) return -1;
    if (!cJSON_IsObject(stream)) return -1;

    hy = cJSON_GetObjectItemCaseSensitive(stream, "hysteriaSettings");
    if (!cJSON_IsObject(hy) || jstr_copy(hy, "auth", s->pass, sizeof s->pass) != 0)
        return -1;

    s->proto = VL_PROTO_HYSTERIA2;
    s->security = VL_SEC_TLS;
    s->net = VL_NET_TCP;

    ts = cJSON_GetObjectItemCaseSensitive(stream, "tlsSettings");
    if (cJSON_IsObject(ts)) {
        (void)jstr_copy_any(ts, s->sni, sizeof s->sni, "serverName", "server_name");
        (void)jstr_copy(ts, "pinnedPeerCertSha256", s->pin_sha256, sizeof s->pin_sha256);
    }

    fm = cJSON_GetObjectItemCaseSensitive(stream, "finalmask");
    if (cJSON_IsObject(fm)) {
        const cJSON *udp = cJSON_GetObjectItemCaseSensitive(fm, "udp");
        if (cJSON_IsArray(udp) && cJSON_GetArraySize(udp) > 0) {
            const cJSON *mask = cJSON_GetArrayItem(udp, 0);
            if (cJSON_IsObject(mask) &&
                jstr_copy(mask, "type", s->obfs, sizeof s->obfs) == 0) {
                const cJSON *mset = cJSON_GetObjectItemCaseSensitive(mask, "settings");
                if (cJSON_IsObject(mset))
                    (void)jstr_copy(mset, "password", s->obfs_password,
                                   sizeof s->obfs_password);
            }
        }
        {
            const cJSON *qp = cJSON_GetObjectItemCaseSensitive(fm, "quicParams");
            const cJSON *hop = cJSON_IsObject(qp)
                ? cJSON_GetObjectItemCaseSensitive(qp, "udpHop") : NULL;
            if (cJSON_IsObject(hop))
                (void)jstr_copy(hop, "ports", s->port_hop, sizeof s->port_hop);
        }
    }

    return json_server_finish(s, ob, remarks, multi);
}

/* trojan and shadowsocks both list their one endpoint under settings.servers[0],
   the shape xray-core gives every outbound that is not vless */
static int xray_trojan_or_ss_to_server(const char *protocol_name,
                                       const cJSON *ob, const cJSON *settings,
                                       const char *remarks, int multi,
                                       vl_server_t *s) {
    const cJSON *servers = cJSON_GetObjectItemCaseSensitive(settings, "servers");
    const cJSON *srv;
    if (!cJSON_IsArray(servers) || cJSON_GetArraySize(servers) < 1) return -1;
    srv = cJSON_GetArrayItem(servers, 0);
    if (!cJSON_IsObject(srv)) return -1;
    if (jstr_copy(srv, "address", s->host, sizeof s->host) != 0) return -1;
    if (j_port(srv, "port", &s->port) != 0) return -1;
    if (jstr_copy(srv, "password", s->pass, sizeof s->pass) != 0) return -1;

    if (strcmp(protocol_name, "trojan") == 0) {
        const cJSON *stream = cJSON_GetObjectItemCaseSensitive(ob, "streamSettings");
        s->proto = VL_PROTO_TROJAN;
        s->security = VL_SEC_TLS;
        if (cJSON_IsObject(stream)) {
            const cJSON *netj = cJSON_GetObjectItemCaseSensitive(stream, "network");
            const cJSON *ts = cJSON_GetObjectItemCaseSensitive(stream, "tlsSettings");
            if (cJSON_IsString(netj) && netj->valuestring &&
                strcmp(netj->valuestring, "ws") == 0)
                s->net = VL_NET_WS;
            if (cJSON_IsObject(ts))
                (void)jstr_copy_any(ts, s->sni, sizeof s->sni, "serverName", "server_name");
            if (s->net == VL_NET_WS) {
                const cJSON *ws = cJSON_GetObjectItemCaseSensitive(stream, "wsSettings");
                if (cJSON_IsObject(ws))
                    (void)jstr_copy(ws, "path", s->path, sizeof s->path);
            }
        }
    } else {
        char method[32];
        s->proto = VL_PROTO_SHADOWSOCKS;
        if (jstr_copy(srv, "method", method, sizeof method) == 0) {
            snprintf(s->encryption, sizeof s->encryption, "%s", method);
            snprintf(s->user, sizeof s->user, "%s", method);
        }
    }
    return json_server_finish(s, ob, remarks, multi);
}

static int xray_outbound_to_server(const cJSON *ob, const char *remarks,
                                   int multi, vl_server_t *s) {
    const cJSON *proto, *settings, *stream, *vnext, *user, *netj, *secj;
    const char *network = "tcp";
    const char *security = "none";
    const char *protocol_name;

    if (!ob || !s) return -1;
    memset(s, 0, sizeof *s);

    proto = cJSON_GetObjectItemCaseSensitive(ob, "protocol");
    if (!cJSON_IsString(proto) || !proto->valuestring) return -1;
    protocol_name = proto->valuestring;

    settings = cJSON_GetObjectItemCaseSensitive(ob, "settings");
    if (!cJSON_IsObject(settings)) return -1;

    if (strcmp(protocol_name, "trojan") == 0 || strcmp(protocol_name, "shadowsocks") == 0)
        return xray_trojan_or_ss_to_server(protocol_name, ob, settings, remarks, multi, s);

    if (strcmp(protocol_name, "hysteria") == 0)
        return xray_hysteria_to_server(ob, settings, remarks, multi, s);

    if (strcmp(protocol_name, "vless") != 0) return -1;
    s->proto = VL_PROTO_VLESS;

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

    json_set_network(s, network);

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

    return json_server_finish(s, ob, remarks, multi);
}

/* sing-box names everything differently from xray: the protocol is "type", the
   endpoint is server/server_port, the credential sits on the outbound itself,
   and tls and transport are nested objects that carry their own enabled flags.
   it is what every current panel and gui exports, and a feed in it used to
   parse as no servers at all because none of the xray keys are present. trojan
   and shadowsocks spell the same endpoint shape, just without vless's users
   array */
static int singbox_trojan_or_ss_to_server(const char *type_name, const cJSON *ob,
                                          const char *remarks, int multi,
                                          vl_server_t *s) {
    if (jstr_copy(ob, "server", s->host, sizeof s->host) != 0) return -1;
    if (j_port(ob, "server_port", &s->port) != 0) return -1;
    if (jstr_copy(ob, "password", s->pass, sizeof s->pass) != 0) return -1;

    if (strcmp(type_name, "trojan") == 0) {
        const cJSON *tls = cJSON_GetObjectItemCaseSensitive(ob, "tls");
        const cJSON *transport = cJSON_GetObjectItemCaseSensitive(ob, "transport");
        s->proto = VL_PROTO_TROJAN;
        s->security = VL_SEC_TLS;
        if (cJSON_IsObject(tls))
            (void)jstr_copy_any(tls, s->sni, sizeof s->sni, "server_name", "serverName");
        if (cJSON_IsObject(transport)) {
            const cJSON *tt = cJSON_GetObjectItemCaseSensitive(transport, "type");
            if (cJSON_IsString(tt) && tt->valuestring && strcmp(tt->valuestring, "ws") == 0) {
                s->net = VL_NET_WS;
                (void)jstr_copy(transport, "path", s->path, sizeof s->path);
            }
        }
    } else {
        char method[32];
        s->proto = VL_PROTO_SHADOWSOCKS;
        if (jstr_copy(ob, "method", method, sizeof method) == 0) {
            snprintf(s->encryption, sizeof s->encryption, "%s", method);
            snprintf(s->user, sizeof s->user, "%s", method);
        }
    }
    return json_server_finish(s, ob, remarks, multi);
}

/* sing-box's hysteria2 outbound: server/server_port/password sit on the
   outbound itself, obfs is its own nested object, and tls only carries sni
   since this fork's hysteria transport has no allowInsecure to read */
static int singbox_hysteria2_to_server(const cJSON *ob, const char *remarks,
                                       int multi, vl_server_t *s) {
    const cJSON *tls, *obfs;

    if (jstr_copy(ob, "server", s->host, sizeof s->host) != 0) return -1;
    if (j_port(ob, "server_port", &s->port) != 0) return -1;
    if (jstr_copy(ob, "password", s->pass, sizeof s->pass) != 0) return -1;

    s->proto = VL_PROTO_HYSTERIA2;
    s->security = VL_SEC_TLS;
    s->net = VL_NET_TCP;

    tls = cJSON_GetObjectItemCaseSensitive(ob, "tls");
    if (cJSON_IsObject(tls))
        (void)jstr_copy_any(tls, s->sni, sizeof s->sni, "server_name", "serverName");

    obfs = cJSON_GetObjectItemCaseSensitive(ob, "obfs");
    if (cJSON_IsObject(obfs) && jstr_copy(obfs, "type", s->obfs, sizeof s->obfs) == 0)
        (void)jstr_copy(obfs, "password", s->obfs_password, sizeof s->obfs_password);

    return json_server_finish(s, ob, remarks, multi);
}

static int singbox_outbound_to_server(const cJSON *ob, const char *remarks,
                                      int multi, vl_server_t *s) {
    const cJSON *type, *tls, *transport;
    const char *network = "tcp";
    const char *type_name;

    if (!ob || !s) return -1;
    type = cJSON_GetObjectItemCaseSensitive(ob, "type");
    if (!cJSON_IsString(type) || !type->valuestring) return -1;
    type_name = type->valuestring;

    if (strcmp(type_name, "trojan") == 0 || strcmp(type_name, "shadowsocks") == 0) {
        memset(s, 0, sizeof *s);
        return singbox_trojan_or_ss_to_server(type_name, ob, remarks, multi, s);
    }

    if (strcmp(type_name, "hysteria2") == 0) {
        memset(s, 0, sizeof *s);
        return singbox_hysteria2_to_server(ob, remarks, multi, s);
    }

    if (strcmp(type_name, "vless") != 0) return -1;

    memset(s, 0, sizeof *s);
    s->proto = VL_PROTO_VLESS;

    if (jstr_copy(ob, "server", s->host, sizeof s->host) != 0) return -1;
    if (j_port(ob, "server_port", &s->port) != 0) return -1;
    if (jstr_copy(ob, "uuid", s->uuid, sizeof s->uuid) != 0) return -1;
    (void)jstr_copy(ob, "flow", s->flow, sizeof s->flow);
/* sing-box has no per-user encryption field: vless over it is always none */
    snprintf(s->encryption, sizeof s->encryption, "%s", "none");

    transport = cJSON_GetObjectItemCaseSensitive(ob, "transport");
    if (cJSON_IsObject(transport)) {
        const cJSON *tt = cJSON_GetObjectItemCaseSensitive(transport, "type");
        if (cJSON_IsString(tt) && tt->valuestring) network = tt->valuestring;
    }
    json_set_network(s, network);

    tls = cJSON_GetObjectItemCaseSensitive(ob, "tls");
    if (json_flag_on(tls, "enabled")) {
        const cJSON *reality = cJSON_GetObjectItemCaseSensitive(tls, "reality");
        const cJSON *utls = cJSON_GetObjectItemCaseSensitive(tls, "utls");
        s->security = VL_SEC_TLS;
        (void)jstr_copy_any(tls, s->sni, sizeof s->sni, "server_name", "serverName");
        if (json_flag_on(utls, "enabled"))
            (void)jstr_copy(utls, "fingerprint", s->fp, sizeof s->fp);
        if (json_flag_on(reality, "enabled")) {
            s->security = VL_SEC_REALITY;
            (void)jstr_copy_any(reality, s->pbk, sizeof s->pbk,
                                "public_key", "publicKey");
            j_copy_short_id_any(reality, s->sid, sizeof s->sid);
        }
    } else {
        s->security = VL_SEC_NONE;
    }

    if (cJSON_IsObject(transport)) {
        if (s->net == VL_NET_WS || s->net == VL_NET_HTTP) {
            (void)jstr_copy(transport, "path", s->path, sizeof s->path);
            {
                const cJSON *hdr = cJSON_GetObjectItemCaseSensitive(transport, "headers");
                if (cJSON_IsObject(hdr))
                    (void)jstr_copy_any(hdr, s->ws_host, sizeof s->ws_host,
                                        "Host", "host");
            }
            if (s->ws_host[0] == '\0') {
/* the http transport lists hosts as an array, the httpupgrade one as a string */
                const cJSON *h = cJSON_GetObjectItemCaseSensitive(transport, "host");
                if (cJSON_IsString(h) && h->valuestring)
                    snprintf(s->ws_host, sizeof s->ws_host, "%s", h->valuestring);
                else if (cJSON_IsArray(h) && cJSON_GetArraySize(h) > 0) {
                    const cJSON *first = cJSON_GetArrayItem(h, 0);
                    if (cJSON_IsString(first) && first->valuestring)
                        snprintf(s->ws_host, sizeof s->ws_host, "%s", first->valuestring);
                }
            }
        }
        if (s->net == VL_NET_GRPC)
            (void)jstr_copy_any(transport, s->path, sizeof s->path,
                                "service_name", "serviceName");
    }

    return json_server_finish(s, ob, remarks, multi);
}

/* every proxy outbound in one feed shares the panel's single "remarks"/name;
   count how many actually turn into servers so json_server_finish knows
   whether it must keep them apart with a host suffix or a shared name would
   leave the list with indistinguishable rows */
static int json_count_proxy_outbounds(const cJSON *outbounds) {
    const cJSON *ob;
    vl_server_t probe;
    int n = 0;
    if (!cJSON_IsArray(outbounds)) return 0;
    cJSON_ArrayForEach(ob, outbounds) {
        if (xray_outbound_to_server(ob, NULL, 0, &probe) == 0 ||
            singbox_outbound_to_server(ob, NULL, 0, &probe) == 0)
            n++;
    }
    return n;
}

/* both formats are accepted from the same array, because a panel that exports
   one of them still labels the file the same way */
static void json_collect_outbounds(const cJSON *outbounds, const char *remarks,
                                   vl_server_t *out, size_t max, size_t *count) {
    const cJSON *ob;
    int multi;
    if (!cJSON_IsArray(outbounds) || !out || !count) return;
    multi = json_count_proxy_outbounds(outbounds) > 1;
    cJSON_ArrayForEach(ob, outbounds) {
        if (*count >= max) return;
        if (xray_outbound_to_server(ob, remarks, multi, &out[*count]) == 0 ||
            singbox_outbound_to_server(ob, remarks, multi, &out[*count]) == 0)
            (*count)++;
    }
}

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
            char remarks[256];
            if (*count >= max) break;
            if (!cJSON_IsObject(item)) continue;
            remarks[0] = '\0';
            (void)jstr_copy(item, "remarks", remarks, sizeof remarks);
            outbounds = cJSON_GetObjectItemCaseSensitive(item, "outbounds");
            if (cJSON_IsArray(outbounds))
                json_collect_outbounds(outbounds, remarks, out, max, count);
        }
    } else if (cJSON_IsObject(root)) {
        const cJSON *outbounds = cJSON_GetObjectItemCaseSensitive(root, "outbounds");
        char remarks[256];
        remarks[0] = '\0';
        (void)jstr_copy(root, "remarks", remarks, sizeof remarks);
        if (cJSON_IsArray(outbounds))
            json_collect_outbounds(outbounds, remarks, out, max, count);
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

cfg_status_t cfg_parse_subscription(const char *blob, size_t blob_len,
                                    vl_server_t *out, size_t max_servers,
                                    size_t *out_count) {
    if (!blob || !out || !out_count || max_servers == 0) return CFG_ERR_BAD_ARG;
    *out_count = 0;

    if (looks_like_happ(blob, blob_len)) {
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

/* JSON must win because DNS URLs inside it look like standalone server links */
    if (looks_like_json(blob, blob_len)) {
        if (parse_xray_json(blob, blob_len, out, max_servers, out_count) == 0)
            return CFG_OK;
    }

/* the foreign client profiles are checked before the link scan because a clash
   document embeds urls in its dns and rule sections */
    if (profiles_looks_like_clash(blob, blob_len)) {
        *out_count = profiles_parse_clash(blob, blob_len, out, max_servers);
        return CFG_OK;
    }

    if (profiles_looks_like_surge(blob, blob_len)) {
        *out_count = profiles_parse_surge(blob, blob_len, out, max_servers);
        return CFG_OK;
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
    if (profiles_looks_like_clash((const char *)scratch, dec_len)) {
        *out_count = profiles_parse_clash((const char *)scratch, dec_len,
                                          out, max_servers);
        free(scratch);
        return CFG_OK;
    }
    if (profiles_looks_like_surge((const char *)scratch, dec_len)) {
        *out_count = profiles_parse_surge((const char *)scratch, dec_len,
                                          out, max_servers);
        free(scratch);
        return CFG_OK;
    }
    parse_link_lines((const char *)scratch, dec_len, out, max_servers, out_count);
    free(scratch);
    return CFG_OK;
}
rules_status_t cfg_parse_rule(const char *text, size_t len, rule_t *out) {
    return rules_parse(text, len, out);
}
