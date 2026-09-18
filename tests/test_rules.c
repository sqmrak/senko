#include "rules.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

int main(void) {
    ruleset_t rules;
    ruleset_init(&rules);
    size_t index = 99;
    ok("suffix add", ruleset_add_text(&rules,
       "direct domain-suffix .Example.COM.", 34, &index) == RULES_OK && index == 0);
    ok("suffix normalized", strcmp(rules.entries[0].value, "example.com") == 0);
    ok("suffix exact", ruleset_match_domain(&rules, "EXAMPLE.COM", &index) ==
       RULE_ACTION_DIRECT && index == 0);
    ok("suffix child", ruleset_match_domain(&rules, "img.example.com.", &index) ==
       RULE_ACTION_DIRECT);
    ok("suffix boundary", ruleset_match_domain(&rules, "notexample.com", &index) ==
       RULE_ACTION_PROXY && index == SIZE_MAX);

    ok("keyword add", ruleset_add_text(&rules,
       "block domain-keyword tracker", 28, NULL) == RULES_OK);
    ok("keyword match", ruleset_match_domain(&rules, "cdn-Tracker.test", &index) ==
       RULE_ACTION_BLOCK && index == 1);
    ok("block priority", ruleset_add_text(&rules,
       "direct domain-suffix tracker.test", 33, NULL) == RULES_OK &&
       ruleset_match_domain(&rules, "tracker.test", &index) == RULE_ACTION_BLOCK);
    ok("hit winning rule", rules.entries[1].hits == 2 && rules.entries[2].hits == 0);

    ok("ipv4 add", ruleset_add_text(&rules,
       "direct ip-cidr 192.0.2.129/24", 31, &index) == RULES_OK);
    ok("ipv4 canonical", strcmp(rules.entries[index].value, "192.0.2.0/24") == 0);
    ok("ipv4 match", ruleset_match_ip(&rules, "192.0.2.240", &index) ==
       RULE_ACTION_DIRECT);
    ok("ipv4 miss", ruleset_match_ip(&rules, "192.0.3.1", &index) ==
       RULE_ACTION_PROXY && index == SIZE_MAX);
    ok("ipv6 add", ruleset_add_text(&rules,
       "block ip-cidr 2001:db8:1::f/48", 31, NULL) == RULES_OK);
    ok("ipv6 match", ruleset_match_ip(&rules, "2001:db8:1::123", &index) ==
       RULE_ACTION_BLOCK);

    size_t before = rules.count;
    ok("duplicate changes action", ruleset_add_text(&rules,
       "block ip-cidr 192.0.2.5/24", 28, &index) == RULES_OK &&
       rules.count == before && rules.entries[index].action == RULE_ACTION_BLOCK);
    ok("ip block priority", ruleset_match_ip(&rules, "192.0.2.2", NULL) ==
       RULE_ACTION_BLOCK);
    ok("remove", ruleset_remove(&rules, index) == RULES_OK && rules.count == before - 1);

    const char *bad[] = {
        "direct domain-suffix", "unknown domain-suffix example.com",
        "direct unknown example.com", "direct domain-suffix bad..name",
        "direct domain-keyword bad name", "direct ip-cidr 192.0.2.1",
        "direct ip-cidr 192.0.2.1/33", "direct ip-cidr 2001:db8::/129"
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        ok("bad rule rejected", ruleset_add_text(&rules, bad[i], strlen(bad[i]), NULL) !=
           RULES_OK);

    rules.count = RULESET_MAX_RULES;
    rule_t rule;
    ok("parse for full", rules_parse("proxy domain-suffix full.test", 29, &rule) == RULES_OK);
    ok("rule cap", ruleset_add(&rules, &rule, NULL) == RULES_ERR_FULL);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("all rules checks passed");
    return 0;
}
