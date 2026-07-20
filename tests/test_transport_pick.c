/* transport_for_server must match cfg_validate for every common combo*/
#include "../daemon/core/config.h"
#include "../daemon/core/transport_pick.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(const char *name, int cond) {
    if (cond) printf("  ok  %s\n", name);
    else { printf(" FAIL %s\n", name); fails++; }
}

static void check_link(const char *link, int expect_vt) {
    vl_server_t s;
    char reason[128];
    ok(link, cfg_parse_link(link, &s) == CFG_OK);
    int valid = cfg_validate_server(&s, reason, sizeof reason);
    const transport_vt_t *vt = transport_for_server(&s);
    if (expect_vt) {
        ok("valid", valid);
        ok("has vt", vt != NULL);
    } else {
        ok("no vt or invalid", !valid || vt == NULL);
    }
}

int main(void) {
    fails = 0;
    check_link("vless://b831381d-6324-4d53-ad4f-8cda48b30811@1.2.3.4:443"
               "?security=none&type=tcp#t", 1);
    check_link("vless://b831381d-6324-4d53-ad4f-8cda48b30811@1.2.3.4:443"
               "?security=tls&type=tcp&sni=ex.com&flow=xtls-rprx-vision#t", 1);
    check_link("vless://b831381d-6324-4d53-ad4f-8cda48b30811@1.2.3.4:443"
               "?security=tls&type=ws&sni=ex.com&path=%2F#t", 1);
    check_link("vless://b831381d-6324-4d53-ad4f-8cda48b30811@1.2.3.4:443"
               "?security=reality&type=ws"
               "&pbk=AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=&fp=chrome#t", 1);
    check_link("vless://b831381d-6324-4d53-ad4f-8cda48b30811@1.2.3.4:443"
               "?security=none&type=xhttp&path=%2Fxh#t", 1);
    check_link("vless://b831381d-6324-4d53-ad4f-8cda48b30811@1.2.3.4:443"
               "?security=reality&type=xhttp&path=%2Fxh"
               "&pbk=AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=&fp=chrome#t", 1);
    check_link("vless://b831381d-6324-4d53-ad4f-8cda48b30811@1.2.3.4:443"
               "?security=none&type=xhttp&path=%2Fxh&mode=packet-up#t", 1);
    check_link("vless://b831381d-6324-4d53-ad4f-8cda48b30811@1.2.3.4:443"
               "?security=tls&type=xhttp&path=%2Fxh&mode=stream-up&sni=ex.com#t", 1);
    check_link("socks5://u:p@1.2.3.4:1080#s", 1);
    check_link("http://1.2.3.4:8080#h", 1);

    if (fails) {
        printf("%d transport_pick checks failed\n", fails);
        return 1;
    }
    printf("all transport_pick checks passed\n");
    return 0;
}
