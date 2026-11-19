#include "iface_addr.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>

static int ipv4_usable(const struct in_addr *addr) {
    uint32_t ip = ntohl(addr->s_addr);
    if (ip == 0) return 0;
    if ((ip & 0xff000000U) == 0x7f000000U) return 0;
    if ((ip & 0xffff0000U) == 0xa9fe0000U) return 0;
    return 1;
}

static int ipv6_usable(const struct in6_addr *addr) {
    const uint8_t *a = addr->s6_addr;
    int leading_zero = 1;
    for (int i = 0; i < 15; ++i) {
        if (a[i] != 0) {
            leading_zero = 0;
            break;
        }
    }
    /* :: and ::1 */
    if (leading_zero && a[15] <= 1) return 0;
    /* fe80::/10 */
    if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) return 0;
    return 1;
}

int iface_addr_usable(const struct sockaddr *sa) {
    if (!sa) return 0;
    if (sa->sa_family == AF_INET)
        return ipv4_usable(&((const struct sockaddr_in *)sa)->sin_addr);
    if (sa->sa_family == AF_INET6)
        return ipv6_usable(&((const struct sockaddr_in6 *)sa)->sin6_addr);
    return 0;
}
