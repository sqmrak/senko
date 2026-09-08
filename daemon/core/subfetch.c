#define _DEFAULT_SOURCE

#include "subfetch.h"
#include "url.h"
#include "http.h"
#include "net_safe.h"
#include "b64.h"

#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <zlib.h>

/* unpack gzip bodies */
static int maybe_gunzip_body(uint8_t *buf, size_t *len, size_t cap) {
    if (!buf || !len || *len < 10) return 0;
    if (buf[0] != 0x1f || buf[1] != 0x8b) return 0;

    size_t in_len = *len;
    uint8_t *out = (uint8_t *)malloc(cap);
    if (!out) return -1;

    z_stream zs;
    memset(&zs, 0, sizeof zs);
    zs.next_in = buf;
    zs.avail_in = (uInt)in_len;
    zs.next_out = out;
    zs.avail_out = (uInt)cap;
/* use the gzip wrapper */
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        free(out);
        return -1;
    }
    int ir = inflate(&zs, Z_FINISH);
    size_t out_len = (size_t)zs.total_out;
    inflateEnd(&zs);
    if (ir != Z_STREAM_END || out_len == 0 || out_len > cap) {
        free(out);
        return -1;
    }
    memcpy(buf, out, out_len);
    *len = out_len;
    free(out);
    fprintf(stderr, "senkod: subfetch gunzip %zu -> %zu\n", in_len, out_len);
    return 0;
}

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static uint64_t userinfo_value(const char *value, const char *wanted) {
    if (!value || !wanted) return 0;
    const char *p = value;
    size_t wanted_len = strlen(wanted);
    while (*p) {
        while (*p == ' ' || *p == ';' || *p == '\t') ++p;
        const char *eq = strchr(p, '=');
        if (!eq) break;
        /* the value runs to the next ';' or to the end of the header. the end of
           the header is eq + 1 + strlen(eq + 1); computing it without the +1
           dropped the last digit of whichever field came last, which turned
           every expiry timestamp into one decades in the past */
        const char *end = strchr(eq + 1, ';');
        if (!end) end = eq + 1 + strlen(eq + 1);
        size_t key_len = (size_t)(eq - p);
        while (key_len > 0 && (p[key_len - 1] == ' ' || p[key_len - 1] == '\t'))
            key_len--;
        if (key_len == wanted_len && memcmp(p, wanted, wanted_len) == 0) {
            const char *vs = eq + 1;
            const char *ve = end;
            while (vs < ve && (*vs == ' ' || *vs == '\t')) ++vs;
            /* a panel that pads the last field left a trailing space inside the
               number, and the whole field was then dropped */
            while (ve > vs && (ve[-1] == ' ' || ve[-1] == '\t')) --ve;
            char number[32];
            size_t n = (size_t)(ve - vs);
            if (n == 0 || n >= sizeof number) return 0;
            memcpy(number, vs, n);
            number[n] = '\0';
            char *stop = NULL;
            unsigned long long v = strtoull(number, &stop, 10);
            return stop == number + n ? (uint64_t)v : 0;
        }
        p = *end ? end + 1 : end;
    }
    return 0;
}

/* seconds since the epoch that a subscription can plausibly carry: panels have
   shipped the expiry in milliseconds, and one shipped days remaining. read
   literally, both land in 1970 and the list calls a live subscription expired */
#define SUBFETCH_EXPIRE_MIN 946684800ULL   /* 2000-01-01 */
#define SUBFETCH_EXPIRE_MAX 4102444800ULL  /* 2100-01-01 */

static uint64_t userinfo_expire(const char *value) {
    uint64_t v = userinfo_value(value, "expire");
    if (v >= SUBFETCH_EXPIRE_MIN && v <= SUBFETCH_EXPIRE_MAX) return v;
    if (v / 1000ULL >= SUBFETCH_EXPIRE_MIN && v / 1000ULL <= SUBFETCH_EXPIRE_MAX)
        return v / 1000ULL;
    return 0;
}

#ifdef SENKO_HOST_TEST
uint64_t subfetch_userinfo_expire_for_test(const char *value) {
    return userinfo_expire(value);
}

uint64_t subfetch_userinfo_value_for_test(const char *value, const char *wanted) {
    return userinfo_value(value, wanted);
}
#endif

static void parser_info(const http_parser_t *hp, subfetch_info_t *info);

#ifdef SENKO_HOST_TEST
void subfetch_parser_info_for_test(const http_parser_t *hp, subfetch_info_t *info) {
    parser_info(hp, info);
}
#endif

static void copy_metadata_text(const char *raw, char *out, size_t cap) {
    if (!raw || !out || cap == 0) return;
    out[0] = '\0';
    if (strncasecmp(raw, "base64:", 7) == 0) {
        size_t n = 0;
        if (b64_decode(raw + 7, strlen(raw + 7),
                       (unsigned char *)out, cap - 1, &n) == 0 && n > 0 &&
            memchr(out, '\0', n) == NULL) {
            out[n] = '\0';
            return;
        }
    }
    snprintf(out, cap, "%s", raw);
}

static void parser_info(const http_parser_t *hp, subfetch_info_t *info) {
    if (!hp || !info) return;
    if (hp->have_subscription_userinfo) {
        info->expire = userinfo_expire(hp->subscription_userinfo);
        info->upload = userinfo_value(hp->subscription_userinfo, "upload");
        info->download = userinfo_value(hp->subscription_userinfo, "download");
        info->total = userinfo_value(hp->subscription_userinfo, "total");
    }
    if (hp->have_subscription_description)
        copy_metadata_text(hp->subscription_description,
                           info->description, sizeof info->description);
    if (hp->have_subscription_support_url)
        snprintf(info->support_url, sizeof info->support_url, "%s",
                 hp->subscription_support_url);
    if (hp->hwid_rejected) {
        info->gated = 1;
        if (hp->have_announce)
            copy_metadata_text(hp->announce, info->gate_reason,
                               sizeof info->gate_reason);
        if (!info->gate_reason[0])
            snprintf(info->gate_reason, sizeof info->gate_reason,
                     "the panel refused this device");
    }
}

/* wait with a deadline */
static int wait_io(int fd, int want_write, long deadline, const subfetch_cfg_t *cfg) {
    while (now_ms() < deadline) {
        if (cfg && cfg->pump) cfg->pump(cfg->pump_ctx);
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = want_write ? POLLOUT : POLLIN;
        pfd.revents = 0;
        int r = poll(&pfd, 1, 10);
        if (r > 0 && pfd.revents) return 1;
        if (r < 0) return 0;
    }
    return 0;
}

/* keep redirect cookies */
static void cookie_jar_merge(char *jar, size_t cap, const char *add) {
    if (!jar || !cap || !add || !add[0]) return;
    if (!jar[0]) {
        size_t n = strlen(add);
        if (n + 1 > cap) n = cap - 1;
        memcpy(jar, add, n);
        jar[n] = '\0';
        return;
    }
    size_t cur = strlen(jar);
    size_t n = strlen(add);
    if (cur + 2 + n + 1 > cap) return;
    jar[cur++] = ';';
    jar[cur++] = ' ';
    memcpy(jar + cur, add, n);
    jar[cur + n] = '\0';
}

static int same_origin(const url_t *a, const url_t *b) {
    return a && b && a->is_https == b->is_https && a->port == b->port &&
           strcasecmp(a->host, b->host) == 0;
}

/* fetch one url */
static subfetch_status_t fetch_once(const subfetch_cfg_t *cfg, const url_t *u,
                                    uint8_t *body, size_t body_cap, size_t *body_len,
                                    char *redir, size_t redir_cap, long deadline,
                                    char *cookie_jar, size_t cookie_cap,
                                    subfetch_info_t *info) {
    const transport_vt_t *vt = u->is_https ? cfg->tls : cfg->tcp;
    if (!vt) return SUBFETCH_ERR_TRANSPORT; /* tls transport is missing */

    char numeric[INET6_ADDRSTRLEN];
    if (!net_resolve_public(u->host, u->port, numeric, sizeof numeric))
        return SUBFETCH_ERR_URL;
    int fd = cfg->dial(cfg->dial_ctx, numeric, u->port);
    if (fd < 0) return SUBFETCH_ERR_DIAL;

    transport_tls_cfg_t tcfg;
    memset(&tcfg, 0, sizeof tcfg);
    tcfg.sni = u->host; /* use the subscription host */

    void *th = vt->open(fd, &tcfg);
    if (!th) { close(fd); return SUBFETCH_ERR_TRANSPORT; }

    subfetch_status_t result = SUBFETCH_ERR_HTTP;
    do {
        char req[2048]; size_t reqlen = 0;
        if (url_build_get_cookie_header(u, cookie_jar, cfg->request_header,
                                        req, sizeof req, &reqlen) != URL_OK) {
            result = SUBFETCH_ERR_URL; break;
        }
        size_t off = 0;
        int io_ok = 1;
        while (off < reqlen) {
            int w = vt->write(th, (const uint8_t *)req + off, reqlen - off);
            if (w > 0) { off += (size_t)w; continue; }
            if (w == TRANSPORT_WANT_WRITE) {
                if (!wait_io(fd, 1, deadline, cfg)) { io_ok = 0; break; }
            } else if (w == TRANSPORT_WANT_READ) {
                if (!wait_io(fd, 0, deadline, cfg)) { io_ok = 0; break; }
            } else { io_ok = 0; break; }
        }
        if (!io_ok) { result = SUBFETCH_ERR_TRANSPORT; break; }

        http_parser_t hp;
        http_parser_init(&hp, body, body_cap);
        uint8_t rb[8192];
        for (;;) {
            int n = vt->read(th, rb, sizeof rb);
            if (n > 0) {
                http_status_t hs = http_parser_feed(&hp, rb, (size_t)n);
                if (hs == HTTP_DONE) {
                    if (hp.have_set_cookie && cookie_jar && cookie_cap)
                        cookie_jar_merge(cookie_jar, cookie_cap, hp.set_cookie);
                    if (http_parser_is_redirect(&hp)) {
                        if (!hp.have_location) { result = SUBFETCH_ERR_REDIRECT; break; }
                        if (url_resolve_redirect(u, hp.location, redir, redir_cap) != URL_OK) {
                            result = SUBFETCH_ERR_REDIRECT;
                            break;
                        }
                        result = SUBFETCH_ERR_REDIRECT; /* follow the redirect */
                    } else {
                        *body_len = hp.body_len;
                        parser_info(&hp, info);
                        result = SUBFETCH_OK;
                    }
                    break;
                }
                if (hs == HTTP_ERR_TOOBIG) { result = SUBFETCH_ERR_TOOBIG; break; }
                if (hs != HTTP_NEED_MORE) { result = SUBFETCH_ERR_HTTP; break; }
            } else if (n == TRANSPORT_WANT_READ) {
                if (!wait_io(fd, 0, deadline, cfg)) { result = SUBFETCH_ERR_TRANSPORT; break; }
            } else if (n == TRANSPORT_WANT_WRITE) {
                if (!wait_io(fd, 1, deadline, cfg)) { result = SUBFETCH_ERR_TRANSPORT; break; }
            } else if (n == TRANSPORT_EOF) {
                http_status_t hs = http_parser_eof(&hp);
                if (hs == HTTP_DONE) {
                    if (hp.have_set_cookie && cookie_jar && cookie_cap)
                        cookie_jar_merge(cookie_jar, cookie_cap, hp.set_cookie);
                    if (http_parser_is_redirect(&hp) && hp.have_location) {
                        if (url_resolve_redirect(u, hp.location, redir, redir_cap) == URL_OK) {
                            result = SUBFETCH_ERR_REDIRECT;
                        } else result = SUBFETCH_ERR_REDIRECT;
                    } else {
                        *body_len = hp.body_len;
                        parser_info(&hp, info);
                        result = SUBFETCH_OK;
                    }
                } else {
                    result = SUBFETCH_ERR_HTTP;
                }
                break;
            } else {
                result = SUBFETCH_ERR_TRANSPORT;
                break;
            }
        }
    } while (0);

    vt->close(th);
    close(fd);
    return result;
}

subfetch_status_t subfetch_get(const subfetch_cfg_t *cfg, const char *url,
                               uint8_t *body_buf, size_t body_cap,
                               size_t *body_len, int timeout_ms) {
    return subfetch_get_info(cfg, url, body_buf, body_cap, body_len,
                             timeout_ms, NULL);
}

subfetch_status_t subfetch_get_info(const subfetch_cfg_t *cfg, const char *url,
                                    uint8_t *body_buf, size_t body_cap,
                                    size_t *body_len, int timeout_ms,
                                    subfetch_info_t *info) {
    if (!cfg || !cfg->dial || !cfg->tcp || !url || !body_buf || !body_len)
        return SUBFETCH_ERR_ARG;
    *body_len = 0;
    if (info) memset(info, 0, sizeof *info);

    int max_redir = cfg->max_redirects > 0 ? cfg->max_redirects : 5;
    long deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 15000);

    char current[HTTP_MAX_LOCATION];
    size_t ul = strlen(url);
    if (ul + 1 > sizeof current) return SUBFETCH_ERR_URL;
    memcpy(current, url, ul + 1);

    url_t origin;
    if (url_parse(current, &origin) != URL_OK) return SUBFETCH_ERR_URL;

/* carry cookies across redirects */
    char cookie_jar[512];
    cookie_jar[0] = '\0';

    for (int hop = 0; hop <= max_redir; ++hop) {
        url_t u;
        if (url_parse(current, &u) != URL_OK) return SUBFETCH_ERR_URL;
        subfetch_cfg_t hop_cfg = *cfg;
        if (!same_origin(&origin, &u)) hop_cfg.request_header = NULL;
        char redir[HTTP_MAX_LOCATION];
        redir[0] = '\0';
        subfetch_status_t r = fetch_once(&hop_cfg, &u, body_buf, body_cap, body_len,
                                         redir, sizeof redir, deadline,
                                         cookie_jar, sizeof cookie_jar, info);
        if (r == SUBFETCH_OK) {
            if (maybe_gunzip_body(body_buf, body_len, body_cap) != 0)
                return SUBFETCH_ERR_HTTP;
            return SUBFETCH_OK;
        }
        if (r != SUBFETCH_ERR_REDIRECT) return r;

/* require absolute redirects */
        if (strncmp(redir, "http://", 7) != 0 &&
            strncmp(redir, "https://", 8) != 0) {
            return SUBFETCH_ERR_REDIRECT;
        }
        size_t rl = strlen(redir);
        if (rl + 1 > sizeof current) return SUBFETCH_ERR_REDIRECT;
        url_t next;
        if (url_parse(redir, &next) != URL_OK || (u.is_https && !next.is_https))
            return SUBFETCH_ERR_REDIRECT;
/* cookies are never forwarded to a different scheme, host, or port */
        if (!same_origin(&u, &next)) cookie_jar[0] = '\0';
/* reject a redirect that cannot progress */
        if (strcmp(redir, current) == 0 && !cookie_jar[0])
            return SUBFETCH_ERR_REDIRECT;
        memcpy(current, redir, rl + 1);
    }
    return SUBFETCH_ERR_REDIRECT; /* redirect limit reached */
}
