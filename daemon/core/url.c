#include "url.h"
#include "net_safe.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

url_status_t url_parse(const char *url, url_t *out) {
    if (!url || !out) return URL_ERR_ARG;
    memset(out, 0, sizeof *out);

    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        out->is_https = 1;
        out->port = 443; /* default, may be overridden by:port */
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        out->is_https = 0;
        out->port = 80;
        p += 7;
    } else {
        return URL_ERR_SCHEME;
    }

    const char *host_start = p;
    while (*p && *p != '/' && *p != '?' && *p != ':') ++p;

    size_t hlen = (size_t)(p - host_start);
    if (hlen == 0) return URL_ERR_HOST;
    if (hlen >= sizeof out->host) return URL_ERR_TOOLONG;
    memcpy(out->host, host_start, hlen);
    out->host[hlen] = '\0';
    if (!net_url_host_safe(out->host)) return URL_ERR_UNSAFE;

    if (*p == ':') {
        ++p;
        unsigned long port = 0;
        if (*p < '0' || *p > '9') return URL_ERR_PORT;
        for (; *p >= '0' && *p <= '9'; ++p) {
            port = port * 10 + (unsigned long)(*p - '0');
            if (port > 65535) return URL_ERR_PORT;
        }
        if (port == 0) return URL_ERR_PORT;
        out->port = (uint16_t)port;
    }

    if (*p == '\0') {
        out->path[0] = '/';
        out->path[1] = '\0';
    } else {
        if (*p == '?') {
            size_t qlen = strlen(p);
            if (qlen + 1 >= sizeof out->path) return URL_ERR_TOOLONG;
            out->path[0] = '/';
            memcpy(out->path + 1, p, qlen);
            out->path[1 + qlen] = '\0';
        } else {
            size_t plen = strlen(p);
            if (plen >= sizeof out->path) return URL_ERR_TOOLONG;
            memcpy(out->path, p, plen);
            out->path[plen] = '\0';
        }
    }
    if (!net_url_path_safe(out->path, strlen(out->path))) return URL_ERR_UNSAFE;
    return URL_OK;
}

static int header_valid(const char *header) {
    if (!header || !header[0]) return 1;
    const char *colon = strchr(header, ':');
    if (!colon || colon == header) return 0;
    for (const char *p = header; p < colon; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c <= 0x20 || c >= 0x7f) return 0;
    }
    for (const char *p = colon + 1; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f) return 0;
    }
    return 1;
}

static int header_name_is(const char *header, const char *name) {
    if (!header || !header[0] || !name) return 0;
    const char *colon = strchr(header, ':');
    size_t n = strlen(name);
    if (!colon || (size_t)(colon - header) != n) return 0;
    for (size_t i = 0; i < n; ++i) {
        if (tolower((unsigned char)header[i]) != tolower((unsigned char)name[i]))
            return 0;
    }
    return 1;
}

url_status_t url_build_get_cookie_header(const url_t *u, const char *cookie,
                                         const char *request_header,
                                         char *buf, size_t cap, size_t *out_len) {
    if (!u || !buf) return URL_ERR_ARG;
    if (!header_valid(request_header)) return URL_ERR_UNSAFE;

    int default_port = u->is_https ? 443 : 80;
    const char *ck = (cookie && cookie[0]) ? cookie : NULL;
    const char *hdr = (request_header && request_header[0]) ? request_header : NULL;
    int custom_ua = header_name_is(hdr, "User-Agent");
    int custom_accept = header_name_is(hdr, "Accept");
    int custom_cookie = header_name_is(hdr, "Cookie");
    size_t off = 0;
    int n = snprintf(buf + off, cap - off, "GET %s HTTP/1.0\r\nHost: %s",
                     u->path, u->host);
    if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
    off += (size_t)n;
    if (u->port != default_port) {
        n = snprintf(buf + off, cap - off, ":%u", u->port);
        if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
        off += (size_t)n;
    }
    n = snprintf(buf + off, cap - off, "\r\n");
    if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
    off += (size_t)n;
    if (!custom_ua) {
        n = snprintf(buf + off, cap - off, "User-Agent: senko/1\r\n");
        if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
        off += (size_t)n;
    }
    if (!custom_accept) {
        n = snprintf(buf + off, cap - off, "Accept: */*\r\n");
        if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
        off += (size_t)n;
    }
    if (hdr) {
        n = snprintf(buf + off, cap - off, "%s\r\n", hdr);
        if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
        off += (size_t)n;
    }
    if (ck && !custom_cookie) {
        n = snprintf(buf + off, cap - off, "Cookie: %s\r\n", ck);
        if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
        off += (size_t)n;
    }
    n = snprintf(buf + off, cap - off, "Connection: close\r\n\r\n");
    if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
    off += (size_t)n;
    if (out_len) *out_len = off;
    return URL_OK;
}

url_status_t url_build_get_cookie(const url_t *u, const char *cookie,
                                  char *buf, size_t cap, size_t *out_len) {
    return url_build_get_cookie_header(u, cookie, NULL, buf, cap, out_len);
}

url_status_t url_build_get(const url_t *u, char *buf, size_t cap, size_t *out_len) {
    return url_build_get_cookie_header(u, NULL, NULL, buf, cap, out_len);
}
