#define _DEFAULT_SOURCE

#include "net_safe.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
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
    struct in6_addr ip6;
    if (s && inet_pton(AF_INET6, s, &ip6) == 1) return 1;
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
        if ((ip & 0xffc00000u) == 0x64400000u) return 0;
        if ((ip >> 20) == 0xac1) return 0;
        if ((ip >> 16) == 0xa9fe) return 0;
        if ((ip >> 16) == 0xc0a8) return 0;
        if ((ip & 0xffffff00u) == 0xc0000000u) return 0;
        if ((ip & 0xffffff00u) == 0xc0000200u) return 0;
        if ((ip & 0xffffff00u) == 0xc0586300u) return 0;
        if ((ip & 0xfffe0000u) == 0xc6120000u) return 0;
        if ((ip & 0xffffff00u) == 0xc6336400u) return 0;
        if ((ip & 0xffffff00u) == 0xcb007100u) return 0;
        if ((ip >> 24) >= 224) return 0;
        return 1;
    }
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
        const unsigned char *a = sin6->sin6_addr.s6_addr;
        static const unsigned char loopback[16] =
            { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 };
        static const unsigned char zero[16] = { 0 };
        if (memcmp(a, zero, 16) == 0) return 0;
        if (memcmp(a, loopback, 16) == 0) return 0;
        if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) return 0;
        if ((a[0] & 0xfe) == 0xfc) return 0;
        if (a[0] == 0xfe && (a[1] & 0xc0) == 0xc0) return 0;
        if (a[0] == 0xff) return 0;
        if (a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0d && a[3] == 0xb8)
            return 0;
        if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
            struct sockaddr_in mapped;
            memset(&mapped, 0, sizeof mapped);
            mapped.sin_family = AF_INET;
            memcpy(&mapped.sin_addr, a + 12, 4);
            return net_addr_allowed((const struct sockaddr *)&mapped);
        }
        return 1;
    }
    return 0;
}

int net_resolve_public(const char *host, uint16_t port, char *numeric, size_t cap) {
    struct addrinfo hints, *res = NULL;
    char service[8];
    int found = 0;
    if (!host || !host[0] || !numeric || cap == 0 || port == 0) return 0;
    numeric[0] = '\0';
    snprintf(service, sizeof service, "%u", (unsigned)port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, service, &hints, &res) != 0 || !res) return 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        const void *src = NULL;
        if (ai->ai_family == AF_INET)
            src = &((const struct sockaddr_in *)ai->ai_addr)->sin_addr;
        else if (ai->ai_family == AF_INET6)
            src = &((const struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
        else
            continue;
        found = 1;
        if (!net_addr_allowed(ai->ai_addr)) {
            numeric[0] = '\0';
            freeaddrinfo(res);
            return 0;
        }
        if (!numeric[0] && !inet_ntop(ai->ai_family, src, numeric, (socklen_t)cap)) {
            freeaddrinfo(res);
            return 0;
        }
    }
    freeaddrinfo(res);
    return found && numeric[0];
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

int net_ip_list_contains(const char *list, const char *ip) {
    if (!list || !ip || !ip[0]) return 0;
    size_t iplen = strlen(ip);
    for (const char *p = list; *p; ) {
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        const char *start = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
        if ((size_t)(p - start) == iplen && memcmp(start, ip, iplen) == 0)
            return 1;
    }
    return 0;
}
