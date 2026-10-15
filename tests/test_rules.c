#define _POSIX_C_SOURCE 200809L

#include "rules.h"

#include "geo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}


static void test_extended_rules(void) {
    static ruleset_t rules;
    size_t index = 0;
    rule_action_t action = RULE_ACTION_PROXY;
    ruleset_init(&rules);

    ok("full add", ruleset_add_text(&rules, "direct domain-full Only.Test", 28, NULL) == RULES_OK);
    ok("full self", ruleset_match_domain(&rules, "only.test", &index) == RULE_ACTION_DIRECT);
    ok("full not child", ruleset_match_domain(&rules, "a.only.test", &index) ==
       RULE_ACTION_PROXY && index == SIZE_MAX);

    ok("app add", ruleset_add_text(&rules, "block app com.Apple.mobilesafari", 32, &index) == RULES_OK);
    ok("app value kept", strcmp(rules.entries[index].value, "com.Apple.mobilesafari") == 0);
    ok("app match", ruleset_match_app(&rules, "com.Apple.mobilesafari", &action, &index) &&
       action == RULE_ACTION_BLOCK);
    ok("app case", !ruleset_match_app(&rules, "com.apple.mobilesafari", &action, &index));
    ok("app other", !ruleset_match_app(&rules, "com.other", &action, &index) && index == SIZE_MAX);
    ok("app empty", !ruleset_match_app(&rules, "", &action, &index));
    ok("app not a domain rule", ruleset_match_domain(&rules, "com.apple.mobilesafari", NULL) ==
       RULE_ACTION_PROXY);
    ok("app readd replaces action", ruleset_add_text(&rules, "direct app com.Apple.mobilesafari", 33, NULL) == RULES_OK &&
       ruleset_match_app(&rules, "com.Apple.mobilesafari", &action, NULL) && action == RULE_ACTION_DIRECT);
    ok("has app", ruleset_has_type(&rules, RULE_TYPE_APP) && !ruleset_has_type(&rules, RULE_TYPE_GEOIP));

    const char *bad[] = {
        "direct app", "direct app bad/id", "direct app has space", "direct geosite ../x",
        "direct geoip a b", "direct geosite -x", "direct domain-full a b"
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        ok("bad extended rule", ruleset_add_text(&rules, bad[i], strlen(bad[i]), NULL) != RULES_OK);

    ok("geo tag lower", ruleset_add_text(&rules, "direct geosite CN", 17, &index) == RULES_OK &&
       strcmp(rules.entries[index].value, "cn") == 0);

    /* one source's rules go, the others stay in order */
    rule_t profile;
    ok("profile rule parse", rules_parse("proxy domain-suffix keep.test", 29, &profile) == RULES_OK);
    profile.source = 3;
    size_t before = rules.count;
    ok("profile add", ruleset_add(&rules, &profile, NULL) == RULES_OK);
    profile.action = RULE_ACTION_BLOCK;
    ok("same value other source is a second rule", ruleset_add(&rules, &profile, NULL) == RULES_OK &&
       rules.count == before + 1);
    rules_parse("proxy domain-suffix keep.test", 29, &profile);
    ok("user and profile coexist", ruleset_add(&rules, &profile, NULL) == RULES_OK &&
       rules.count == before + 2);
    ok("remove source", ruleset_remove_source(&rules, 3) == 1 && rules.count == before + 1);
    ok("remove absent source", ruleset_remove_source(&rules, 9) == 0);
    ok("user rule survived", ruleset_match_domain(&rules, "x.keep.test", NULL) == RULE_ACTION_PROXY);
}

static void test_geo_rules(void) {
    char dir[] = "/tmp/senko-rules-geo-XXXXXX";
    char path[512];
    static ruleset_t rules;
    size_t missing = 99, index = 0;
    if (!mkdtemp(dir)) {
        ok("mkdtemp", 0);
        return;
    }
    geo_site_t *site = geo_site_new();
    geo_site_add(site, GEO_SITE_SUFFIX, "geo.test", 8);
    geo_site_seal(site);
    geo_tag_path(dir, GEO_KIND_SITE, "ru", path, sizeof path);
    ok("site file", geo_site_save(site, path) == GEO_OK);
    geo_site_free(site);
    geo_ip_t *ip = geo_ip_new();
    const uint8_t net[4] = { 8, 8, 8, 0 };
    geo_ip_add(ip, net, 4, 24);
    geo_ip_seal(ip);
    geo_tag_path(dir, GEO_KIND_IP, "ru", path, sizeof path);
    ok("ip file", geo_ip_save(ip, path) == GEO_OK);
    geo_ip_free(ip);

    ruleset_init(&rules);
    ruleset_add_text(&rules, "direct geosite ru", 17, NULL);
    ruleset_add_text(&rules, "direct geoip ru", 15, NULL);
    ruleset_add_text(&rules, "block geoip private", 19, NULL);
    ruleset_add_text(&rules, "block geosite absent", 20, NULL);
    ok("unbound matches nothing", ruleset_match_domain(&rules, "a.geo.test", NULL) == RULE_ACTION_PROXY);
    ok("bind", ruleset_bind_geo(&rules, dir, &missing) == RULES_OK && missing == 1);
    ok("geosite match", ruleset_match_domain(&rules, "a.geo.test", &index) == RULE_ACTION_DIRECT && index == 0);
    ok("geosite miss", ruleset_match_domain(&rules, "other.test", NULL) == RULE_ACTION_PROXY);
    ok("geoip match", ruleset_match_ip(&rules, "8.8.8.77", &index) == RULE_ACTION_DIRECT && index == 1);
    ok("geoip miss", ruleset_match_ip(&rules, "198.20.0.1", NULL) == RULE_ACTION_PROXY);
    ok("geoip private built in", ruleset_match_ip(&rules, "192.168.4.4", &index) == RULE_ACTION_BLOCK && index == 2);
    ok("geoip ipv6 private", ruleset_match_ip(&rules, "fd00::1", NULL) == RULE_ACTION_BLOCK);
    ok("rebind keeps sets", ruleset_bind_geo(&rules, dir, &missing) == RULES_OK && missing == 1 &&
       ruleset_match_domain(&rules, "geo.test", NULL) == RULE_ACTION_DIRECT);
    ruleset_release_geo(&rules);
    ok("released matches nothing", ruleset_match_domain(&rules, "a.geo.test", NULL) == RULE_ACTION_PROXY &&
       ruleset_match_ip(&rules, "192.168.4.4", NULL) == RULE_ACTION_PROXY);
    ruleset_release_geo(&rules);

    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0) ok("cleanup", 0);
}

int main(void) {
    test_extended_rules();
    test_geo_rules();
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
       "direct ip-cidr 192.0.2.129/24", 29, &index) == RULES_OK);
    ok("ipv4 canonical", strcmp(rules.entries[index].value, "192.0.2.0/24") == 0);
    ok("ipv4 match", ruleset_match_ip(&rules, "192.0.2.240", &index) ==
       RULE_ACTION_DIRECT);
    ok("ipv4 miss", ruleset_match_ip(&rules, "192.0.3.1", &index) ==
       RULE_ACTION_PROXY && index == SIZE_MAX);
    ok("ipv6 add", ruleset_add_text(&rules,
       "block ip-cidr 2001:db8:1::f/48", 30, NULL) == RULES_OK);
    ok("ipv6 match", ruleset_match_ip(&rules, "2001:db8:1::123", &index) ==
       RULE_ACTION_BLOCK);

    size_t before = rules.count;
    ok("duplicate changes action", ruleset_add_text(&rules,
       "block ip-cidr 192.0.2.5/24", 26, &index) == RULES_OK &&
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
