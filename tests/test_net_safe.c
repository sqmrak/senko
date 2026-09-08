#include "net_safe.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static int failed;

static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", name); failed = 1; }
}

static int v4_allowed(const char *lit) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    inet_pton(AF_INET, lit, &sa.sin_addr);
    return net_addr_allowed((struct sockaddr *)&sa);
}

int main(void) {
    /* exact-token membership: the bug was strstr treating a shorter address as
       present inside a longer one and skipping its bypass rule */
    check(net_ip_list_contains("1.2.3.4,5.6.7.8", "5.6.7.8"), "list plain hit");
    check(net_ip_list_contains("1.2.3.4, 5.6.7.8", "1.2.3.4"), "list space delim hit");
    check(!net_ip_list_contains("11.2.3.40,85.6.7.8", "1.2.3.4"),
          "substring is not a member");
    check(!net_ip_list_contains("1.2.3.40", "1.2.3.4"), "prefix is not a member");
    check(!net_ip_list_contains("85.6.7.8", "5.6.7.8"), "suffix is not a member");
    check(net_ip_list_contains("1.2.3.4", "1.2.3.4"), "single element hit");
    check(!net_ip_list_contains("", "1.2.3.4"), "empty list");
    check(!net_ip_list_contains("1.2.3.4", ""), "empty needle");
    check(!net_ip_list_contains(NULL, "1.2.3.4"), "null list");
    check(net_ip_list_contains("  ,, 1.2.3.4 ,,", "1.2.3.4"), "runs of delimiters");
    check(net_ip_list_contains("a,bb,1.2.3.4", "1.2.3.4"), "trailing element");

    /* regression cover for the ssrf address gate that has no other host test */
    check(v4_allowed("8.8.8.8"), "public v4 allowed");
    check(!v4_allowed("127.0.0.1"), "loopback rejected");
    check(!v4_allowed("10.1.2.3"), "rfc1918 10/8 rejected");
    check(!v4_allowed("172.16.9.9"), "rfc1918 172.16/12 rejected");
    check(!v4_allowed("192.168.1.1"), "rfc1918 192.168/16 rejected");
    check(!v4_allowed("169.254.1.1"), "link-local rejected");
    check(!v4_allowed("100.64.0.1"), "cgnat rejected");
    check(!v4_allowed("224.0.0.1"), "multicast rejected");
    check(!v4_allowed("0.0.0.0"), "this-network rejected");

    check(net_hostname_safe("example.com"), "plain hostname safe");
    check(!net_hostname_safe("ex ample.com"), "space in hostname unsafe");
    check(!net_hostname_safe("a/b"), "slash in hostname unsafe");
    check(!net_hostname_safe(""), "empty hostname unsafe");
    check(!net_url_path_safe("/a\r\nHost: x", 11), "crlf in path unsafe");
    check(net_url_path_safe("/normal/path", 12), "plain path safe");

    if (failed) return 1;
    puts("all net_safe checks passed");
    return 0;
}
