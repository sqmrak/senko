#define _DEFAULT_SOURCE

#include "net_safe.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failed;

static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", name); failed = 1; }
}

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* a dns lookup that never comes back (a censored network blackholing the
   resolver, not a normal NXDOMAIN) must not block the caller past its bound
   this stands in for that hang with a worker that sleeps well past the
   timeout, so the test is deterministic instead of depending on real,
   unpredictable network behavior. */
static void *slow_work(void *arg) {
    usleep(300 * 1000);
    int *marker = (int *)malloc(sizeof *marker);
    if (marker) *marker = *(int *)arg;
    return marker;
}

static int g_free_calls;
static void free_marker(void *p) {
    g_free_calls++;
    free(p);
}

static void test_bounded_timeout(void) {
    int arg = 42;
    void *result = (void *)0x1; /* poison: must come back NULL on timeout */
    long start = now_ms();
    int ok = net_run_bounded(slow_work, &arg, free_marker, 30, &result);
    long elapsed = now_ms() - start;
    check(!ok, "bounded call times out instead of blocking");
    check(result == NULL, "timed-out call reports no result");
    check(elapsed < 250, "timed-out call returns near the bound, not the work duration");

    /* the worker is still running in the background; give it time to finish
       and free its own (abandoned) result through free_marker */
    usleep(500 * 1000);
    check(g_free_calls == 1, "abandoned result is freed exactly once by the worker");
}

static void *fast_work(void *arg) {
    int *out = (int *)malloc(sizeof *out);
    if (out) *out = *(int *)arg * 2;
    return out;
}

static void test_bounded_completes(void) {
    int arg = 21;
    void *result = NULL;
    int ok = net_run_bounded(fast_work, &arg, free_marker, 2000, &result);
    check(ok, "bounded call reports completion");
    check(result != NULL && *(int *)result == 42, "bounded call hands back the worker's result");
    free(result);
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

    {
        char ip[INET_ADDRSTRLEN];
        check(net_resolve_public_ipv4("8.8.8.8", 443, ip, sizeof ip) &&
              strcmp(ip, "8.8.8.8") == 0,
              "direct probe resolves a public ipv4 literal");
        check(!net_resolve_public_ipv4("127.0.0.1", 443, ip, sizeof ip),
              "direct probe still rejects loopback");
    }

    test_bounded_timeout();
    test_bounded_completes();

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
