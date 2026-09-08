#include "url.h"
#include "../../common/senko_paths.h"
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
    if (*p == '[') {
        host_start = ++p;
        while (*p && *p != ']') ++p;
        if (*p != ']') return URL_ERR_HOST;
    } else {
        while (*p && *p != '/' && *p != '?' && *p != ':') ++p;
    }

    size_t hlen = (size_t)(p - host_start);
    if (hlen == 0) return URL_ERR_HOST;
    if (hlen >= sizeof out->host) return URL_ERR_TOOLONG;
    memcpy(out->host, host_start, hlen);
    out->host[hlen] = '\0';
    if (!net_url_host_safe(out->host)) return URL_ERR_UNSAFE;

    if (*p == ']') ++p;
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

static url_status_t url_copy_redirect(const char *url, char *buf, size_t cap) {
    size_t len;
    url_t parsed;
    if (!url || !buf || cap == 0) return URL_ERR_ARG;
    len = strlen(url);
    if (len + 1 > cap) return URL_ERR_TOOLONG;
    memcpy(buf, url, len + 1);
    return url_parse(buf, &parsed);
}

url_status_t url_resolve_redirect(const url_t *base, const char *location,
                                  char *buf, size_t cap) {
    const char *scheme;
    const char *path;
    const char *query;
    size_t path_len;
    size_t dir_len;
    int default_port;
    int n;

    if (!base || !location || !buf || cap == 0) return URL_ERR_ARG;
    while (*location == ' ' || *location == '\t') ++location;
    if (!location[0]) return URL_ERR_HOST;

    if (strncmp(location, "http://", 7) == 0 ||
        strncmp(location, "https://", 8) == 0)
        return url_copy_redirect(location, buf, cap);

    scheme = base->is_https ? "https" : "http";
    default_port = base->is_https ? 443 : 80;
    if (strncmp(location, "//", 2) == 0) {
        n = snprintf(buf, cap, "%s:%s", scheme, location);
        if (n < 0 || (size_t)n >= cap) return URL_ERR_TOOLONG;
        {
            url_t parsed;
            return url_parse(buf, &parsed);
        }
    }

    n = strchr(base->host, ':') ?
        snprintf(buf, cap, "%s://[%s]", scheme, base->host) :
        snprintf(buf, cap, "%s://%s", scheme, base->host);
    if (n < 0 || (size_t)n >= cap) return URL_ERR_TOOLONG;
    size_t off = (size_t)n;
    if (base->port != (uint16_t)default_port) {
        n = snprintf(buf + off, cap - off, ":%u", base->port);
        if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
        off += (size_t)n;
    }

    if (location[0] == '/') {
        path = location;
    } else if (location[0] == '?') {
        path = base->path;
        query = strchr(path, '?');
        path_len = query ? (size_t)(query - path) : strlen(path);
        if (off + path_len + strlen(location) + 1 > cap)
            return URL_ERR_TOOLONG;
        memcpy(buf + off, path, path_len);
        off += path_len;
        memcpy(buf + off, location, strlen(location) + 1);
        {
            url_t parsed;
            return url_parse(buf, &parsed);
        }
    } else {
        path = base->path;
        query = strchr(path, '?');
        path_len = query ? (size_t)(query - path) : strlen(path);
        dir_len = path_len;
        while (dir_len > 0 && path[dir_len - 1] != '/') --dir_len;
        if (dir_len == 0) dir_len = 1;
        if (off + dir_len + strlen(location) + 1 > cap)
            return URL_ERR_TOOLONG;
        memcpy(buf + off, path, dir_len);
        off += dir_len;
        memcpy(buf + off, location, strlen(location) + 1);
        {
            url_t parsed;
            return url_parse(buf, &parsed);
        }
    }

    n = snprintf(buf + off, cap - off, "%s", path);
    if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
    {
        url_t parsed;
        return url_parse(buf, &parsed);
    }
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

void url_device_hwid(char *out, size_t cap) {
    if (!out || cap < 33) return;
    out[0] = '\0';
    FILE *f = fopen(SENKO_HWID_PATH, "r");
    if (f) {
        if (fgets(out, (int)cap, f)) {
            char *nl = strchr(out, '\n');
            if (nl) *nl = '\0';
            nl = strchr(out, '\r');
            if (nl) *nl = '\0';
            size_t len = strlen(out);
            if (len >= 10 && len <= 64) {
                fclose(f);
                return;
            }
        }
        fclose(f);
    }
    /* generate a persistent 32-char hex id */
    static const char hex[] = "0123456789abcdef";
    unsigned char rnd[16];
    FILE *urand = fopen("/dev/urandom", "rb");
    if (urand) {
        if (fread(rnd, 1, sizeof rnd, urand) != sizeof rnd) {
            memset(rnd, 0x42, sizeof rnd);
        }
        fclose(urand);
    } else {
        memset(rnd, 0x42, sizeof rnd);
    }
    for (size_t i = 0; i < 16; ++i) {
        out[i * 2] = hex[(rnd[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[rnd[i] & 0x0f];
    }
    out[32] = '\0';
    f = fopen(SENKO_HWID_PATH, "w");
    if (f) {
        fprintf(f, "%s\n", out);
        fclose(f);
    }
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
    int custom_hwid = header_name_is(hdr, "x-hwid") || header_name_is(hdr, "X-HWID");
    size_t off = 0;
    int n = strchr(u->host, ':') ?
        snprintf(buf + off, cap - off, "GET %s HTTP/1.0\r\nHost: [%s]",
                 u->path, u->host) :
        snprintf(buf + off, cap - off, "GET %s HTTP/1.0\r\nHost: %s",
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
        /* providers use the client id to select a compatible feed format */
        n = snprintf(buf + off, cap - off, "User-Agent: Happ/3.26.1\r\n");
        if (n < 0 || (size_t)n >= cap - off) return URL_ERR_TOOLONG;
        off += (size_t)n;
    }
    if (!custom_hwid) {
        char hwid[65];
        url_device_hwid(hwid, sizeof hwid);
        n = snprintf(buf + off, cap - off,
                     "x-hwid: %s\r\n"
                     "x-device-os: iOS\r\n"
                     "x-ver-os: 15.0\r\n"
                     "x-device-model: iPhone10,3\r\n",
                     hwid);
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
