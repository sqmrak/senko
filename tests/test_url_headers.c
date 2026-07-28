#include "url.h"

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
                   strstr(req, "Authorization:") == NULL,
                   "no custom header by default");

    if (failed) return 1;
    puts("all url header checks passed");
    return 0;
}
