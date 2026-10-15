#include "vpn_conflict.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main(void) {
    CHECK(vpn_ifname_is_tunnel("utun3"));
    CHECK(vpn_ifname_is_tunnel("ppp0"));
    CHECK(vpn_ifname_is_tunnel("ipsec0"));
    CHECK(!vpn_ifname_is_tunnel("en0"));
    CHECK(!vpn_ifname_is_tunnel("pdp_ip0"));
    CHECK(!vpn_ifname_is_tunnel("lo0"));
    CHECK(!vpn_ifname_is_tunnel(NULL));

    char text[160];
    vpn_conflict_text(NULL, 0, text, sizeof text);
    CHECK(text[0] == '\0');

    vpn_iface_t two[2];
    memset(two, 0, sizeof two);
    snprintf(two[0].ifname, sizeof two[0].ifname, "utun2");
    snprintf(two[0].address, sizeof two[0].address, "10.8.0.2");
    snprintf(two[1].ifname, sizeof two[1].ifname, "ppp0");
    snprintf(two[1].address, sizeof two[1].address, "172.16.0.9");
    vpn_conflict_text(two, 2, text, sizeof text);
    CHECK(strstr(text, "another VPN is active") != NULL);
    CHECK(strstr(text, "utun2 10.8.0.2, ppp0 172.16.0.9") != NULL);

    /* a small buffer cuts the sentence and keeps it terminated */
    char tiny[24];
    vpn_conflict_text(two, 2, tiny, sizeof tiny);
    CHECK(strlen(tiny) < sizeof tiny);

    /* the host has no vpn interface the scan could report for itself, and the
       call must not overrun a one entry list whatever the host holds */
    vpn_iface_t one[1];
    CHECK(vpn_conflict_scan(NULL, one, 1) <= 1);
    CHECK(vpn_conflict_scan(NULL, NULL, 1) == 0);
    CHECK(vpn_conflict_scan(NULL, one, 0) == 0);

    char reason[64] = "cannot look up route";
    vpn_conflict_explain(reason, sizeof reason, NULL);
    CHECK(strncmp(reason, "cannot look up route", 20) == 0);
    CHECK(strlen(reason) < sizeof reason);

    if (failures) return 1;
    puts("all vpn conflict checks passed");
    return 0;
}
