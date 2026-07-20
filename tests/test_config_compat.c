#include "config.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

int main(void) {
    vl_server_t s;
    char reason[128];

    ok("parse raw reality",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@example.com:443?security=reality&type=raw&flow=xtls-rprx-vision&pbk=abc&sid=00#raw",
                      &s) == CFG_OK);
    ok("raw is tcp", s.net == VL_NET_TCP);
    ok("default sni host", strcmp(s.sni, "example.com") == 0);

    ok("parse serverName",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@1.2.3.4:443?security=tls&type=tcp&flow=xtls-rprx-vision&serverName=edge.example#tls",
                      &s) == CFG_OK);
    ok("serverName sni", strcmp(s.sni, "edge.example") == 0);

    ok("parse ws host separate from sni",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@1.2.3.4:443?security=tls&type=ws&sni=tls.example&host=cdn.example&path=%2Fws#ws",
                      &s) == CFG_OK);
    ok("ws keeps tls sni", strcmp(s.sni, "tls.example") == 0);
    ok("ws keeps http host", strcmp(s.ws_host, "cdn.example") == 0);
    ok("ws path decoded", strcmp(s.path, "/ws") == 0);

    ok("decode ws host and sni",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@1.2.3.4:443?security=tls&type=ws&sni=tls%2Eexample&host=cdn%2Eexample%3A443&path=%2Fws#wsenc",
                      &s) == CFG_OK);
    ok("decoded sni", strcmp(s.sni, "tls.example") == 0);
    ok("decoded ws host", strcmp(s.ws_host, "cdn.example:443") == 0);

    reason[0] = '\0';
    ok("ws flow rejected",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@1.2.3.4:443?security=tls&type=ws&sni=tls.example&host=cdn.example&path=%2Fws&flow=xtls-rprx-vision#badws",
                      &s) == CFG_OK &&
       !cfg_validate_server(&s, reason, sizeof reason) &&
       strcmp(reason, "ws flow is unsupported") == 0);

    reason[0] = '\0';
    ok("plain tls valid",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@example.com:443?security=tls&type=tcp#tlsplain",
                      &s) == CFG_OK &&
       cfg_validate_server(&s, reason, sizeof reason));

    ok("encryption none valid",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@example.com:443?security=tls&type=tcp&encryption=none#tlsnone",
                      &s) == CFG_OK &&
       cfg_validate_server(&s, reason, sizeof reason));

    reason[0] = '\0';
    ok("non none encryption rejected",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@example.com:443?security=tls&type=tcp&encryption=mlkem768x25519plus#badenc",
                      &s) == CFG_OK &&
       !cfg_validate_server(&s, reason, sizeof reason) &&
       strcmp(reason, "unsupported encryption") == 0);

    ok("valid reality contract",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@example.com:443?security=reality&type=raw&flow=xtls-rprx-vision&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA&sid=00#ok",
                      &s) == CFG_OK &&
       cfg_validate_server(&s, reason, sizeof reason));

    reason[0] = '\0';
    ok("invalid reality rejected",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@example.com:443?security=reality&type=tcp&flow=xtls-rprx-vision#bad",
                      &s) == CFG_OK &&
       !cfg_validate_server(&s, reason, sizeof reason) &&
       strcmp(reason, "reality requires valid pbk") == 0);

    reason[0] = '\0';
    ok("bad tls flow rejected",
       cfg_parse_link("vless://11111111-1111-4111-8111-111111111111@example.com:443?security=tls&type=tcp&flow=none#bad",
                      &s) == CFG_OK &&
       !cfg_validate_server(&s, reason, sizeof reason) &&
       strcmp(reason, "unsupported tls flow") == 0);

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all config compat checks passed\n");
    return 0;
}
