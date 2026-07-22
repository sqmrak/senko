#include "http.h"

#include <string.h>

void http_parser_init(http_parser_t *p, uint8_t *body_buf, size_t body_cap) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->state = HP_STATUS_LINE;
    p->framing = HTTP_FRAMING_UNKNOWN;
    p->status_code = 0;
    p->content_length = 0;
    p->body = body_buf;
    p->body_cap = body_cap;
}

static int hdr_is(const char *line, const char *pfx) {
    while (*pfx) {
        char a = *line, b = *pfx;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        ++line; ++pfx;
    }
    return 1;
}

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    return s;
}

/* cap response lengths */
#define HTTP_LEN_CAP (64L * 1024 * 1024)

static long parse_dec(const char *s) {
    long v = 0;
    if (*s < '0' || *s > '9') return -1;
    for (; *s >= '0' && *s <= '9'; ++s) {
        v = v * 10 + (*s - '0');
        if (v > HTTP_LEN_CAP) return -1; /* reject oversized values */
    }
    return v;
}

static long parse_hex(const char *s) {
    long v = 0;
    int any = 0;
    for (; ; ++s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = (v << 4) | d;
        any = 1;
        if (v > HTTP_LEN_CAP) return -1;
    }
    if (!any) return -1;
    return v;
}

static http_status_t consume_line(http_parser_t *p) {
    if (p->state == HP_STATUS_LINE) {
/* validate the status line */
        const char *s = p->line;
        if (!hdr_is(s, "http/")) return HTTP_ERR_PARSE;
        const char *sp = strchr(s, ' ');
        if (!sp) return HTTP_ERR_PARSE;
        long code = parse_dec(skip_ws(sp));
        if (code < 100 || code > 599) return HTTP_ERR_PARSE;
        p->status_code = (int)code;
        p->state = HP_HEADERS;
        return HTTP_NEED_MORE;
    }

    if (p->line_len == 0) {
        if (http_parser_is_redirect(p)) {
/* stop after redirect headers */
            p->state = HP_COMPLETE;
            return HTTP_DONE;
        }
        if (p->status_code < 200 || p->status_code >= 300) {
            return HTTP_ERR_STATUS; /* reject non-success status */
        }
        if (p->framing == HTTP_FRAMING_CHUNKED) {
            p->state = HP_BODY_CHUNK_SIZE;
        } else if (p->framing == HTTP_FRAMING_LENGTH) {
            if (p->content_length == 0) { p->state = HP_COMPLETE; return HTTP_DONE; }
            p->state = HP_BODY_LENGTH;
        } else {
            p->framing = HTTP_FRAMING_EOF;
            p->state = HP_BODY_EOF;
        }
        return HTTP_NEED_MORE;
    }

    if (hdr_is(p->line, "content-length:")) {
        long v = parse_dec(skip_ws(p->line + 15));
        if (v < 0) return HTTP_ERR_PARSE;
        if (p->framing != HTTP_FRAMING_CHUNKED) {
            p->framing = HTTP_FRAMING_LENGTH;
            p->content_length = (size_t)v;
        }
    } else if (hdr_is(p->line, "transfer-encoding:")) {
        const char *v = skip_ws(p->line + 18);
        if (hdr_is(v, "chunked")) {
            p->framing = HTTP_FRAMING_CHUNKED;
            p->content_length = 0; /* ignore content length */
        }
    } else if (hdr_is(p->line, "location:")) {
        const char *v = skip_ws(p->line + 9);
        size_t l = strlen(v);
        if (l >= sizeof p->location) l = sizeof p->location - 1;
        memcpy(p->location, v, l);
        p->location[l] = '\0';
        p->have_location = 1;
    } else if (hdr_is(p->line, "set-cookie:")) {
        const char *v = skip_ws(p->line + 11);
        size_t n = 0;
        while (v[n] && v[n] != ';' && v[n] != ' ' && v[n] != '\t') n++;
        if (n > 0) {
            size_t cur = p->have_set_cookie ? strlen(p->set_cookie) : 0;
            size_t need = n + (cur ? 2 : 0);
            if (cur + need + 1 < sizeof p->set_cookie) {
                if (cur) {
                    p->set_cookie[cur++] = ';';
                    p->set_cookie[cur++] = ' ';
                }
                memcpy(p->set_cookie + cur, v, n);
                p->set_cookie[cur + n] = '\0';
                p->have_set_cookie = 1;
            }
        }
    } else if (hdr_is(p->line, "subscription-userinfo:")) {
        const char *v = skip_ws(p->line + 22);
        size_t n = strlen(v);
        if (n >= sizeof p->subscription_userinfo)
            n = sizeof p->subscription_userinfo - 1;
        memcpy(p->subscription_userinfo, v, n);
        p->subscription_userinfo[n] = '\0';
        p->have_subscription_userinfo = 1;
    }
    return HTTP_NEED_MORE;
}

static http_status_t line_push(http_parser_t *p, char c) {
    if (++p->header_total > HTTP_MAX_HEADER) return HTTP_ERR_TOOBIG;
    if (c == '\r') return HTTP_NEED_MORE; /* wait for lf */
    if (c == '\n') {
        if (p->line_len >= sizeof p->line) return HTTP_ERR_TOOBIG;
        p->line[p->line_len] = '\0';
        http_status_t r = consume_line(p);
        p->line_len = 0;
        return r;
    }
    if (p->line_len + 1 >= sizeof p->line) return HTTP_ERR_TOOBIG;
    p->line[p->line_len++] = c;
    return HTTP_NEED_MORE;
}

static http_status_t body_push(http_parser_t *p, const uint8_t *b, size_t n) {
    if (p->body_len + n > p->body_cap) return HTTP_ERR_TOOBIG;
    memcpy(p->body + p->body_len, b, n);
    p->body_len += n;
    p->body_got += n;
    return HTTP_NEED_MORE;
}

http_status_t http_parser_feed(http_parser_t *p, const uint8_t *in, size_t len) {
    if (!p || (!in && len)) return HTTP_ERR_ARG;

    size_t i = 0;
    while (i < len) {
        switch (p->state) {
            case HP_STATUS_LINE:
            case HP_HEADERS: {
                http_status_t r = line_push(p, (char)in[i++]);
                if (r == HTTP_DONE) return HTTP_DONE;
                if (r != HTTP_NEED_MORE) { p->state = HP_ERROR; return r; }
                break;
            }

            case HP_BODY_LENGTH: {
                size_t want = p->content_length - p->body_got;
                size_t avail = len - i;
                size_t take = avail < want ? avail : want;
                http_status_t r = body_push(p, in + i, take);
                if (r != HTTP_NEED_MORE) { p->state = HP_ERROR; return r; }
                i += take;
                if (p->body_got >= p->content_length) {
                    p->state = HP_COMPLETE;
                    return HTTP_DONE;
                }
                break;
            }

            case HP_BODY_CHUNK_SIZE: {
                char c = (char)in[i++];
                if (c == '\r') break;
                if (c == '\n') {
                    p->line[p->line_len] = '\0';
                    long sz = parse_hex(p->line);
                    p->line_len = 0;
                    if (sz < 0) { p->state = HP_ERROR; return HTTP_ERR_PARSE; }
                    if (sz == 0) { /* finish after the last chunk */
                        p->state = HP_COMPLETE;
                        return HTTP_DONE;
                    }
                    p->chunk_remaining = (size_t)sz;
                    p->state = HP_BODY_CHUNK_DATA;
                } else {
                    if (p->line_len + 1 >= sizeof p->line) { p->state = HP_ERROR; return HTTP_ERR_TOOBIG; }
                    p->line[p->line_len++] = c;
                }
                break;
            }

            case HP_BODY_CHUNK_DATA: {
                size_t avail = len - i;
                size_t take = avail < p->chunk_remaining ? avail : p->chunk_remaining;
                http_status_t r = body_push(p, in + i, take);
                if (r != HTTP_NEED_MORE) { p->state = HP_ERROR; return r; }
                i += take;
                p->chunk_remaining -= take;
                if (p->chunk_remaining == 0) p->state = HP_BODY_CHUNK_CRLF;
                break;
            }

            case HP_BODY_CHUNK_CRLF: {
                char c = (char)in[i++];
                if (c == '\n') p->state = HP_BODY_CHUNK_SIZE;
                break;
            }

            case HP_BODY_EOF: {
                http_status_t r = body_push(p, in + i, len - i);
                if (r != HTTP_NEED_MORE) { p->state = HP_ERROR; return r; }
                i = len;
                break;
            }

            case HP_COMPLETE:
                return HTTP_DONE; /* ignore extra bytes */

            case HP_ERROR:
            default:
                return HTTP_ERR_PARSE;
        }
    }

    return (p->state == HP_COMPLETE) ? HTTP_DONE : HTTP_NEED_MORE;
}

http_status_t http_parser_eof(http_parser_t *p) {
    if (!p) return HTTP_ERR_ARG;
    if (p->state == HP_COMPLETE) return HTTP_DONE;
    if (p->state == HP_BODY_EOF) { /* eof closes the body */
        p->state = HP_COMPLETE;
        return HTTP_DONE;
    }
    p->state = HP_ERROR;
    return HTTP_ERR_PARSE;
}

int http_parser_is_redirect(const http_parser_t *p) {
    if (!p) return 0;
    return (p->status_code == 301 || p->status_code == 302 ||
            p->status_code == 303 || p->status_code == 307 ||
            p->status_code == 308);
}
