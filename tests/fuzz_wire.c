/* wire parsers that take bytes straight from a local app or a remote server:
   the socks5 request path, the vless response header, and url/redirect handling */
#include "socks5.h"
#include "url.h"
#include "vless.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    size_t consumed = 0;
    vless_dest_t dest;
    (void)socks5_parse_greeting(data, size, &consumed);
    (void)socks5_parse_request(data, size, &dest, &consumed);

    size_t hdr_len = 0;
    (void)vless_parse_response(data, size, &hdr_len);

    if (size == 0 || size > 8192) return 0;

    char *text = (char *)malloc(size + 1);
    if (!text) return 0;
    memcpy(text, data, size);
    text[size] = '\0';

    uint8_t uuid[VLESS_UUID_LEN];
    (void)vless_uuid_parse(text, uuid);

    char req[4096];
    char redirect[2048];
    size_t out_len = 0;
    url_t u;

    if (url_parse(text, &u) == URL_OK) {
        (void)url_build_get(&u, req, sizeof req, &out_len);
        (void)url_build_get_cookie(&u, text, req, sizeof req, &out_len);
        (void)url_build_get_cookie_header(&u, NULL, text, req, sizeof req, &out_len);
        (void)url_resolve_redirect(&u, text, redirect, sizeof redirect);
    }

    /* redirects arrive as a Location value against a known base */
    url_t base;
    if (url_parse("https://sub.example/feed/a?x=1", &base) == URL_OK)
        (void)url_resolve_redirect(&base, text, redirect, sizeof redirect);

    free(text);
    return 0;
}
