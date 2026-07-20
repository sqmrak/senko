#include "url.h"
#include "net_safe.h"

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

url_status_t url_build_get_cookie(const url_t *u, const char *cookie,
                                  char *buf, size_t cap, size_t *out_len) {
    if (!u || !buf) return URL_ERR_ARG;

    int default_port = u->is_https ? 443 : 80;
    int n;
    const char *ck = (cookie && cookie[0]) ? cookie : NULL;
    if (u->port == default_port) {
        if (ck)
            n = snprintf(buf, cap,
                "GET %s HTTP/1.0\r\n"
                "Host: %s\r\n"
                "User-Agent: senko/1\r\n"
                "Accept: */*\r\n"
                "Cookie: %s\r\n"
                "Connection: close\r\n"
                "\r\n",
                u->path, u->host, ck);
        else
            n = snprintf(buf, cap,
                "GET %s HTTP/1.0\r\n"
                "Host: %s\r\n"
                "User-Agent: senko/1\r\n"
                "Accept: */*\r\n"
                "Connection: close\r\n"
                "\r\n",
                u->path, u->host);
    } else {
        if (ck)
            n = snprintf(buf, cap,
                "GET %s HTTP/1.0\r\n"
                "Host: %s:%u\r\n"
                "User-Agent: senko/1\r\n"
                "Accept: */*\r\n"
                "Cookie: %s\r\n"
                "Connection: close\r\n"
                "\r\n",
                u->path, u->host, u->port, ck);
        else
            n = snprintf(buf, cap,
                "GET %s HTTP/1.0\r\n"
                "Host: %s:%u\r\n"
                "User-Agent: senko/1\r\n"
                "Accept: */*\r\n"
                "Connection: close\r\n"
                "\r\n",
                u->path, u->host, u->port);
    }
    if (n < 0) return URL_ERR_ARG;
    if ((size_t)n >= cap) return URL_ERR_TOOLONG;
    if (out_len) *out_len = (size_t)n;
    return URL_OK;
}

url_status_t url_build_get(const url_t *u, char *buf, size_t cap, size_t *out_len) {
    return url_build_get_cookie(u, NULL, buf, cap, out_len);
}
