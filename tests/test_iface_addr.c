#include "iface_addr.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

struct addr_case {
    const char *text;
    int family;
    int usable;
};

static const struct addr_case cases[] = {
    { "192.168.1.23", AF_INET, 1 },
    { "10.0.0.2", AF_INET, 1 },
    { "100.64.0.9", AF_INET, 1 },
    { "0.0.0.0", AF_INET, 0 },
    { "127.0.0.1", AF_INET, 0 },
    { "127.255.255.254", AF_INET, 0 },
    { "169.254.0.1", AF_INET, 0 },
    { "169.254.255.255", AF_INET, 0 },
    { "169.253.255.255", AF_INET, 1 },
    { "169.255.0.1", AF_INET, 1 },
    { "2a00:1450:4001::1", AF_INET6, 1 },
    { "fd12:3456:789a::2", AF_INET6, 1 },
    { "::", AF_INET6, 0 },
    { "::1", AF_INET6, 0 },
    { "::2", AF_INET6, 1 },
    { "fe80::1", AF_INET6, 0 },
    { "febf::1", AF_INET6, 0 },
    { "fec0::1", AF_INET6, 1 },
    { "fe7f::1", AF_INET6, 1 },
};

static int check_text(const struct addr_case *c) {
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof ss);
    if (c->family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
        sin->sin_family = AF_INET;
        if (inet_pton(AF_INET, c->text, &sin->sin_addr) != 1) return 1;
    } else {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
        sin6->sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, c->text, &sin6->sin6_addr) != 1) return 1;
    }
    if (iface_addr_usable((const struct sockaddr *)&ss) == c->usable) return 0;
    fprintf(stderr, "failed: %s expected usable=%d\n", c->text, c->usable);
    return 1;
}

int main(void) {
    int bad = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) bad |= check_text(&cases[i]);

    if (iface_addr_usable(NULL) != 0) {
        fprintf(stderr, "failed: NULL address\n");
        bad = 1;
    }
    struct sockaddr_storage other;
    memset(&other, 0, sizeof other);
    other.ss_family = AF_UNIX;
    if (iface_addr_usable((const struct sockaddr *)&other) != 0) {
        fprintf(stderr, "failed: link-layer family\n");
        bad = 1;
    }
    return bad;
}
