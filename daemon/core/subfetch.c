#define _DEFAULT_SOURCE

#include "subfetch.h"
#include "url.h"
#include "http.h"
#include "net_safe.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <zlib.h>

/* inflate gzip because binary subscription bodies contain no parseable links */
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
/* select the gzip wrapper expected by subscription servers */
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

/* wait with a deadline while pumping the shared loop between polls */
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

/* retain redirect cookies so the next request can authenticate */
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

/* fetch one url and return redirect data without hiding cookie state */
static subfetch_status_t fetch_once(const subfetch_cfg_t *cfg, const url_t *u,
                                    uint8_t *body, size_t body_cap, size_t *body_len,
                                    char *redir, size_t redir_cap, long deadline,
                                    char *cookie_jar, size_t cookie_cap) {
    const transport_vt_t *vt = u->is_https ? cfg->tls : cfg->tcp;
    if (!vt) return SUBFETCH_ERR_TRANSPORT; /* https requested, no tls vt */

    int fd = cfg->dial(cfg->dial_ctx, u->host, u->port);
    if (fd < 0) return SUBFETCH_ERR_DIAL;

    transport_tls_cfg_t tcfg;
    memset(&tcfg, 0, sizeof tcfg);
    tcfg.sni = u->host; /* present the sub host as sni for https */

    void *th = vt->open(fd, &tcfg);
    if (!th) { close(fd); return SUBFETCH_ERR_TRANSPORT; }

    subfetch_status_t result = SUBFETCH_ERR_HTTP;
    do {
        char req[2048]; size_t reqlen = 0;
        if (url_build_get_cookie(u, cookie_jar, req, sizeof req, &reqlen) != URL_OK) {
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
                        size_t ll = strlen(hp.location);
                        if (ll + 1 > redir_cap) { result = SUBFETCH_ERR_REDIRECT; break; }
                        memcpy(redir, hp.location, ll + 1);
                        result = SUBFETCH_ERR_REDIRECT; /* signal: follow it */
                    } else {
                        *body_len = hp.body_len;
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
                        size_t ll = strlen(hp.location);
                        if (ll + 1 <= redir_cap) {
                            memcpy(redir, hp.location, ll + 1);
                            result = SUBFETCH_ERR_REDIRECT;
                        } else result = SUBFETCH_ERR_REDIRECT;
                    } else {
                        *body_len = hp.body_len;
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
    if (!cfg || !cfg->dial || !cfg->tcp || !url || !body_buf || !body_len)
        return SUBFETCH_ERR_ARG;
    *body_len = 0;

    int max_redir = cfg->max_redirects > 0 ? cfg->max_redirects : 5;
    long deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 15000);

    char current[HTTP_MAX_LOCATION];
    size_t ul = strlen(url);
    if (ul + 1 > sizeof current) return SUBFETCH_ERR_URL;
    memcpy(current, url, ul + 1);

/* carry retry cookies so guarded redirects can reach the body */
    char cookie_jar[512];
    cookie_jar[0] = '\0';

    for (int hop = 0; hop <= max_redir; ++hop) {
        url_t u;
        if (url_parse(current, &u) != URL_OK) return SUBFETCH_ERR_URL;
/* stop ssrf into lan/loopback before we open a socket */
        if (!net_ipv4_host_allowed(u.host)) return SUBFETCH_ERR_URL;

        char redir[HTTP_MAX_LOCATION];
        redir[0] = '\0';
        subfetch_status_t r = fetch_once(cfg, &u, body_buf, body_cap, body_len,
                                         redir, sizeof redir, deadline,
                                         cookie_jar, sizeof cookie_jar);
        if (r == SUBFETCH_OK) {
            if (maybe_gunzip_body(body_buf, body_len, body_cap) != 0)
                return SUBFETCH_ERR_HTTP;
            return SUBFETCH_OK;
        }
        if (r != SUBFETCH_ERR_REDIRECT) return r;

/* require absolute redirects because relative resolution is unsupported */
        if (strncmp(redir, "http://", 7) != 0 &&
            strncmp(redir, "https://", 8) != 0) {
            return SUBFETCH_ERR_REDIRECT;
        }
        size_t rl = strlen(redir);
        if (rl + 1 > sizeof current) return SUBFETCH_ERR_REDIRECT;
/* reject a same-url redirect without a cookie because it cannot progress */
        if (strcmp(redir, current) == 0 && !cookie_jar[0])
            return SUBFETCH_ERR_REDIRECT;
        memcpy(current, redir, rl + 1);
    }
    return SUBFETCH_ERR_REDIRECT; /* too many hops */
}
