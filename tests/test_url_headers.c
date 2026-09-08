#include "url.h"
#include "net_safe.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static int check(int ok, const char *name) {
    if (!ok) fprintf(stderr, "FAIL %s\n", name);
    return ok ? 0 : 1;
}

int main(void) {
    url_t u;
    char req[2048];
    size_t n = 0;
    int failed = 0;

    failed += check(url_parse("https://sub.example/feed", &u) == URL_OK,
                    "parse subscription url");
    failed += check(url_build_get_cookie_header(&u, NULL,
                                                "Authorization: Bearer abc",
                                                req, sizeof req, &n) == URL_OK,
                    "build request with header");
    failed += check(strstr(req, "Authorization: Bearer abc\r\n") != NULL,
                    "request contains custom header");
    failed += check(strstr(req, "Authorization: Bearer abc\r\nConnection: close") != NULL,
                    "custom header is before connection header");
    failed += check(url_build_get_cookie_header(&u, NULL, "User-Agent: legacy-client",
                                                req, sizeof req, &n) == URL_OK &&
                   strstr(req, "User-Agent: senko/1") == NULL &&
                   strstr(req, "User-Agent: legacy-client\r\n") != NULL,
                   "custom user agent replaces default");
    failed += check(url_build_get_cookie_header(&u, NULL, "X-Test: a\r\nX-Evil: b",
                                                req, sizeof req, &n) == URL_ERR_UNSAFE,
                    "reject header injection");
    failed += check(url_build_get_cookie_header(&u, NULL, NULL,
                                                req, sizeof req, &n) == URL_OK &&
                   strstr(req, "Authorization:") == NULL &&
                   strstr(req, "User-Agent: Happ/3.26.1\r\n") != NULL &&
                   strstr(req, "x-hwid:") != NULL,
                   "happ-compatible default user agent and hwid");

    char redirect[1024];
    failed += check(url_resolve_redirect(&u, "/sub/token/",
                                         redirect, sizeof redirect) == URL_OK &&
                   strcmp(redirect, "https://sub.example/sub/token/") == 0,
                   "resolve root-relative redirect");
    failed += check(url_resolve_redirect(&u, "?format=base64",
                                         redirect, sizeof redirect) == URL_OK &&
                   strcmp(redirect, "https://sub.example/feed?format=base64") == 0,
                   "resolve query redirect");
    failed += check(url_parse("https://[2606:4700:4700::1111]:8443/feed", &u) == URL_OK &&
                    strcmp(u.host, "2606:4700:4700::1111") == 0 && u.port == 8443,
                    "parse ipv6 url");
    failed += check(url_build_get(&u, req, sizeof req, &n) == URL_OK &&
                    strstr(req, "Host: [2606:4700:4700::1111]:8443") != NULL,
                    "build ipv6 host header");
    struct sockaddr_in private4;
    memset(&private4, 0, sizeof private4);
    private4.sin_family = AF_INET;
    inet_pton(AF_INET, "100.64.0.1", &private4.sin_addr);
    failed += check(!net_addr_allowed((struct sockaddr *)&private4), "reject cgnat");
    struct sockaddr_in6 private6;
    memset(&private6, 0, sizeof private6);
    private6.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::ffff:127.0.0.1", &private6.sin6_addr);
    failed += check(!net_addr_allowed((struct sockaddr *)&private6), "reject mapped loopback");

    if (failed) return 1;
    puts("all url header checks passed");
    return 0;
}
