#define _DEFAULT_SOURCE

#include "net_safe.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

int net_hostname_safe(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x21 || c >= 0x7f) return 0;
        if (c == ' ' || c == '\t' || c == '/' || c == '?' || c == ':' ||
            c == '%' || c == '\\')
            return 0;
    }
    return 1;
}

int net_url_host_safe(const char *s) {
    return net_hostname_safe(s);
}

int net_url_path_safe(const char *s, size_t len) {
    if (!s) return 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) return 0;
        if (c == '\r' || c == '\n') return 0;
    }
    return 1;
}

int net_addr_allowed(const struct sockaddr *sa) {
    if (!sa) return 0;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        if ((ip >> 24) == 0) return 0;
        if ((ip >> 24) == 10) return 0;
        if ((ip >> 24) == 127) return 0;
        if ((ip >> 20) == 0xac1) return 0;
        if ((ip >> 16) == 0xa9fe) return 0;
        if ((ip >> 16) == 0xc0a8) return 0;
        if ((ip >> 24) >= 224) return 0;
        return 1;
    }
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
        const unsigned char *a = sin6->sin6_addr.s6_addr;
        static const unsigned char loopback[16] =
            { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 };
        if (memcmp(a, loopback, 16) == 0) return 0;
        if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) return 0;
        if ((a[0] & 0xfe) == 0xfc) return 0;
        if (a[0] == 0xff) return 0;
        return 1;
    }
    return 0;
}

int net_ipv4_host_allowed(const char *host) {
    if (!host || !host[0]) return 0;
    struct in_addr a;
    if (inet_pton(AF_INET, host, &a) != 1)
        return 1; /* hostname: resolve-time filter applies */
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_addr = a;
    return net_addr_allowed((struct sockaddr *)&sin);
}

int net_ipv4_literal(const char *s, char *out, size_t cap) {
    if (!s || !out || cap < 8) return 0;
    struct in_addr a;
    if (inet_pton(AF_INET, s, &a) != 1) return 0;
    if (!inet_ntop(AF_INET, &a, out, (socklen_t)cap)) return 0;
    return 1;
}