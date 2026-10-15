#include "dns_policy.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static dns_policy_t policy;

int main(void) {
    dns_policy_init(&policy);
    const uint32_t direct_ip[1] = { 0xc0000201u };  /* 192.0.2.1 */
    const uint32_t shared_ip[1] = { 0xc6336401u };  /* 198.51.100.1 */
    const uint8_t direct_bytes[4] = { 192, 0, 2, 1 };
    const uint8_t shared_bytes[4] = { 198, 51, 100, 1 };
    const uint8_t unknown[4] = { 203, 0, 113, 9 };
    char domain[DNS_POLICY_DOMAIN_MAX];

    dns_policy_record(&policy, "bank.example", RULE_ACTION_DIRECT, direct_ip, 1, 100, 60);
    ok("a direct domain's address goes direct",
       dns_policy_is_direct(&policy, direct_bytes, 120, domain, sizeof domain) &&
       strcmp(domain, "bank.example") == 0);
    ok("an address no answer named stays in the tunnel",
       !dns_policy_is_direct(&policy, unknown, 120, NULL, 0));
    ok("the direct mark ends with its ttl",
       !dns_policy_is_direct(&policy, direct_bytes, 160, NULL, 0));

    /* a cdn address that also serves a proxied site */
    dns_policy_record(&policy, "shop.example", RULE_ACTION_DIRECT, shared_ip, 1, 200, 300);
    dns_policy_record(&policy, "video.example", RULE_ACTION_PROXY, shared_ip, 1, 210, 60);
    ok("a proxied domain on the same address keeps it in the tunnel",
       !dns_policy_is_direct(&policy, shared_bytes, 220, NULL, 0));
    ok("and it goes direct again once the proxied answer expires",
       dns_policy_is_direct(&policy, shared_bytes, 280, NULL, 0));

    dns_policy_record(&policy, "ads.example", RULE_ACTION_BLOCK, direct_ip, 1, 300, 60);
    ok("a block rule does not make an address direct",
       !dns_policy_is_direct(&policy, direct_bytes, 310, NULL, 0));

    dns_policy_counts_t counts;
    dns_policy_counts(&policy, 280, &counts);
    ok("counts show the live addresses", counts.addresses == 1 && counts.direct == 1);

    /* a full table of live entries evicts the least recently used one */
    dns_policy_clear(&policy);
    for (uint32_t i = 0; i < DNS_POLICY_CAP + 3; ++i) {
        uint32_t address = 0x0a000000u + i;
        dns_policy_record(&policy, "many.example", RULE_ACTION_DIRECT, &address, 1, 1000, 600);
    }
    ok("overflow is counted", policy.evicted == 3);
    const uint8_t newest[4] = { 10, 0, 8, 2 }; /* 0x0a000802 = cap + 2 */
    ok("the newest address survives the overflow",
       dns_policy_is_direct(&policy, newest, 1001, NULL, 0));
    dns_policy_clear(&policy);
    ok("clear forgets addresses but keeps the eviction count",
       policy.evicted == 3 && !dns_policy_is_direct(&policy, newest, 1001, NULL, 0));

    {
        /* the verdict tells an address nothing resolved to from one a proxied name did */
        const uint8_t seen[4] = { 10, 9, 9, 1 }, unseen[4] = { 10, 9, 9, 2 };
        uint32_t proxied = 0x0a090901u, direct = 0x0a090903u;
        const uint8_t direct_bytes[4] = { 10, 9, 9, 3 };
        char domain[DNS_POLICY_DOMAIN_MAX];
        dns_policy_clear(&policy);
        dns_policy_record(&policy, "via.example", RULE_ACTION_PROXY, &proxied, 1, 100, 60);
        dns_policy_record(&policy, "go.example", RULE_ACTION_DIRECT, &direct, 1, 100, 60);
        ok("verdict proxy", dns_policy_verdict(&policy, seen, 110, NULL, 0) == RULE_ACTION_PROXY);
        ok("verdict unknown", dns_policy_verdict(&policy, unseen, 110, NULL, 0) == -1);
        ok("verdict direct names the domain",
           dns_policy_verdict(&policy, direct_bytes, 110, domain, sizeof domain) == RULE_ACTION_DIRECT &&
           strcmp(domain, "go.example") == 0);
        ok("verdict expires", dns_policy_verdict(&policy, seen, 161, NULL, 0) == -1 &&
           dns_policy_verdict(&policy, direct_bytes, 161, NULL, 0) == -1);
        dns_policy_record(&policy, "go.example", RULE_ACTION_DIRECT, &proxied, 1, 100, 60);
        ok("a proxied answer wins over a direct one",
           dns_policy_verdict(&policy, seen, 110, NULL, 0) == RULE_ACTION_PROXY);
        ok("verdict with no table", dns_policy_verdict(NULL, seen, 1, NULL, 0) == -1);
    }

    if (failures) {
        fprintf(stderr, "%d dns policy check(s) failed\n", failures);
        return 1;
    }
    puts("all dns policy checks passed");
    return 0;
}
