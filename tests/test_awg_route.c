#include "awg_route.h"

#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *what) {
    if (condition) return 0;
    fprintf(stderr, "failed: %s\n", what);
    return 1;
}

int main(void) {
    awg_config_t cfg;
    awg_config_init(&cfg);
    snprintf(cfg.addresses[0], sizeof cfg.addresses[0], "172.16.0.2/32");
    snprintf(cfg.addresses[1], sizeof cfg.addresses[1], "2606:4700:110::2/128");
    cfg.address_count = 2;
    awg_route_plan_t plan;
    int bad = 0;
    bad |= expect(awg_route_plan_build(&cfg, "utun3", "162.159.192.1", "192.168.1.1", &plan) == 0,
                  "plan build");
    bad |= expect(plan.has_ipv4 && plan.has_ipv6, "address families");
    bad |= expect(plan.mtu == 1280, "interface mtu");
    bad |= expect(strcmp(plan.endpoint, "162.159.192.1") == 0 &&
                  strcmp(plan.gateway, "192.168.1.1") == 0, "endpoint bypass");
    bad |= expect(awg_route_plan_build(&cfg, "utun;bad", "162.159.192.1", "192.168.1.1", &plan) != 0,
                  "interface validation");
    return bad ? 1 : 0;
}
