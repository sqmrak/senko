#include "routing_profile.h"

#include "b64.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static const char kProfile[] =
    "{\"Name\":\"Russia\",\"GlobalProxy\":\"true\",\"DomainStrategy\":\"IPIfNonMatch\","
    "\"DirectSites\":[\"geosite:category-ru\",\"domain:yandex.ru\",\"full:only.example\","
    "\"keyword:vk\",\"regexp:^x\",\"ext:foo.dat:bar\",\"localhost\"],"
    "\"DirectIp\":[\"geoip:ru\",\"10.0.0.0/8\",\"192.168.1.1\",\"2001:db8::1\",\"geoip:private\"],"
    "\"BlockSites\":[\"geosite:category-ads-all\"],"
    "\"ProxySites\":[\"domain:youtube.com\",\"geosite:google@cn\"],"
    "\"ProxyIp\":[\"geoip:cloudflare\"],\"LastUpdated\":\"1700000000\","
    "\"Geoipurl\":\"https://example.test/geoip.dat\","
    "\"Geositeurl\":\"http://insecure.test/geosite.dat\"}";

static char *b64_of(const char *text) {
    size_t n = strlen(text);
    size_t cap = b64_encoded_maxlen(n) + 4, out = 0;
    char *buf = malloc(cap);
    if (!buf || b64_encode((const unsigned char *)text, n, buf, cap, &out) != 0) exit(2);
    buf[out] = '\0';
    return buf;
}

static char *link_of(const char *prefix, const char *text) {
    char *b = b64_of(text);
    char *link = malloc(strlen(prefix) + strlen(b) + 1);
    strcpy(link, prefix);
    strcat(link, b);
    free(b);
    return link;
}

static void test_decode(void) {
    char json[ROUTING_JSON_MAX + 1];
    routing_cmd_t cmd;
    char *b = b64_of(kProfile);
    char *onadd = link_of("happ://routing/onadd/", kProfile);
    char *add = link_of("happ://routing/add/", kProfile);

    ok("onadd", routing_decode(onadd, strlen(onadd), &cmd, json, sizeof json) == ROUTING_OK &&
       cmd == ROUTING_CMD_ONADD && strcmp(json, kProfile) == 0);
    ok("add", routing_decode(add, strlen(add), &cmd, json, sizeof json) == ROUTING_OK &&
       cmd == ROUTING_CMD_ADD && strcmp(json, kProfile) == 0);
    ok("bare base64", routing_decode(b, strlen(b), &cmd, json, sizeof json) == ROUTING_OK &&
       cmd == ROUTING_CMD_ONADD && strcmp(json, kProfile) == 0);
    ok("bare json", routing_decode(kProfile, strlen(kProfile), &cmd, json, sizeof json) == ROUTING_OK &&
       strcmp(json, kProfile) == 0);
    ok("upper case scheme", routing_decode("HAPP://ROUTING/off", 18, &cmd, json, sizeof json) == ROUTING_OK &&
       cmd == ROUTING_CMD_OFF);
    ok("off", routing_decode("happ://routing/off\n", 19, &cmd, json, sizeof json) == ROUTING_OK &&
       cmd == ROUTING_CMD_OFF && json[0] == '\0');

    /* the url safe alphabet without padding, and a percent encoded payload */
    char *safe = b64_of(kProfile);
    for (char *p = safe; *p; ++p) {
        if (*p == '+') *p = '-';
        if (*p == '/') *p = '_';
    }
    char *eq = strchr(safe, '=');
    if (eq) *eq = '\0';
    char *safe_link = malloc(strlen(safe) + 32);
    sprintf(safe_link, "happ://routing/add/%s", safe);
    ok("url safe", routing_decode(safe_link, strlen(safe_link), &cmd, json, sizeof json) == ROUTING_OK &&
       strcmp(json, kProfile) == 0);
    char pct[] = "happ://routing/add/%7B%22Name%22%3A%22P%22%2C%22GlobalProxy%22%3A%22false%22%7D";
    ok("percent json", routing_decode(pct, strlen(pct), &cmd, json, sizeof json) == ROUTING_OK &&
       strcmp(json, "{\"Name\":\"P\",\"GlobalProxy\":\"false\"}") == 0);
    free(safe);
    free(safe_link);

    ok("empty", routing_decode("", 0, &cmd, json, sizeof json) == ROUTING_ERR_FORMAT);
    ok("unknown verb", routing_decode("happ://routing/zap/abcd", 23, &cmd, json, sizeof json) == ROUTING_ERR_FORMAT);
    ok("not base64", routing_decode("happ://routing/add/@@@@", 23, &cmd, json, sizeof json) == ROUTING_ERR_FORMAT);
    char *notjson = link_of("happ://routing/add/", "just text");
    ok("base64 of text", routing_decode(notjson, strlen(notjson), &cmd, json, sizeof json) == ROUTING_ERR_FORMAT);
    ok("small buffer", routing_decode(onadd, strlen(onadd), &cmd, json, 64) == ROUTING_ERR_TOOBIG);
    char *huge = malloc(ROUTING_LINK_MAX + 100);
    memset(huge, 'A', ROUTING_LINK_MAX + 99);
    huge[ROUTING_LINK_MAX + 99] = '\0';
    ok("oversized link", routing_decode(huge, strlen(huge), &cmd, json, sizeof json) == ROUTING_ERR_TOOBIG);
    free(huge);
    free(notjson);
    free(b);
    free(onadd);
    free(add);
}

static void test_find(void) {
    char json[ROUTING_JSON_MAX + 1];
    routing_cmd_t cmd;
    char *link = link_of("happ://routing/onadd/", kProfile);
    char *body = malloc(strlen(link) + 200);

    ok("header wins", routing_find(link, "vless://x\n", 10, &cmd, json, sizeof json) == ROUTING_OK &&
       strcmp(json, kProfile) == 0);
    sprintf(body, "vless://a@b:1\n  %s  \nvless://c@d:2\n", link);
    ok("body line", routing_find(NULL, body, strlen(body), &cmd, json, sizeof json) == ROUTING_OK &&
       cmd == ROUTING_CMD_ONADD && strcmp(json, kProfile) == 0);
    sprintf(body, "vless://a@b:1\r\n%s\r\n", link);
    ok("body crlf", routing_find("", body, strlen(body), &cmd, json, sizeof json) == ROUTING_OK);
    ok("none", routing_find(NULL, "vless://a@b:1\n", 14, &cmd, json, sizeof json) == ROUTING_ERR_NONE);
    ok("empty header and body", routing_find("  ", "", 0, &cmd, json, sizeof json) == ROUTING_ERR_NONE);
    ok("bad header is an error", routing_find("happ://routing/add/@@", body, strlen(body), &cmd,
                                              json, sizeof json) == ROUTING_ERR_FORMAT);
    ok("off in body", routing_find(NULL, "x\nhapp://routing/off\n", 20, &cmd, json, sizeof json) == ROUTING_OK &&
       cmd == ROUTING_CMD_OFF);
    ok("link in the middle of a line is not a routing line",
       routing_find(NULL, "see happ://routing/off now", 26, &cmd, json, sizeof json) == ROUTING_ERR_NONE);
    free(body);
    free(link);
}

static void test_inspect(void) {
    routing_info_t info;
    ok("inspect", routing_inspect(kProfile, strlen(kProfile), &info) == ROUTING_OK);
    ok("name", strcmp(info.name, "Russia") == 0);
    ok("global proxy", info.global_proxy == 1);
    ok("last updated", info.last_updated == 1700000000ULL);
    ok("geoip url kept", strcmp(info.geoip_url, "https://example.test/geoip.dat") == 0);
    ok("insecure geosite url replaced by the default",
       strncmp(info.geosite_url, "https://github.com/Loyalsoldier/", 32) == 0);

    const char *off = "{\"Name\":\"A\",\"GlobalProxy\":\"false\"}";
    ok("global proxy false", routing_inspect(off, strlen(off), &info) == ROUTING_OK && info.global_proxy == 0);
    off = "{\"GlobalProxy\":false,\"LastUpdated\":42}";
    ok("bool and number", routing_inspect(off, strlen(off), &info) == ROUTING_OK &&
       info.global_proxy == 0 && info.last_updated == 42 && info.name[0] == '\0');
    off = "{\"DirectSites\":[]}";
    ok("defaults", routing_inspect(off, strlen(off), &info) == ROUTING_OK && info.global_proxy == 1 &&
       strstr(info.geoip_url, "geoip.dat") != NULL);

    ok("not json", routing_inspect("nope", 4, &info) == ROUTING_ERR_FORMAT);
    ok("array root", routing_inspect("[1]", 3, &info) == ROUTING_ERR_PROFILE);
    ok("unrelated object", routing_inspect("{\"a\":1}", 7, &info) == ROUTING_ERR_PROFILE);
    ok("list not an array", routing_inspect("{\"DirectSites\":\"x\"}", 19, &info) == ROUTING_ERR_PROFILE);
    ok("empty", routing_inspect("", 0, &info) == ROUTING_ERR_FORMAT);
}

static void test_compile(void) {
    static ruleset_t rules;
    routing_info_t info;
    size_t index;
    ruleset_init(&rules);
    ok("compile", routing_compile(kProfile, strlen(kProfile), 4, &rules, &info) == ROUTING_OK);
    /* direct: category-ru, yandex, full, keyword vk, localhost keyword, ru, 10/8,
       /32, /128, private; block: ads; proxy: youtube, google@cn, cloudflare */
    ok("rule count", info.rules == 14 && rules.count == 14);
    ok("unsupported counted", info.unsupported == 2);
    ok("geo tags counted", info.geo_tags == 6);
    ok("all rules marked", ruleset_remove_source(&rules, 5) == 0);
    ok("source", rules.entries[0].source == 4 && rules.entries[rules.count - 1].source == 4);
    ok("suffix", ruleset_match_domain(&rules, "mail.yandex.ru", &index) == RULE_ACTION_DIRECT);
    ok("full", ruleset_match_domain(&rules, "only.example", NULL) == RULE_ACTION_DIRECT &&
       ruleset_match_domain(&rules, "a.only.example", NULL) == RULE_ACTION_PROXY);
    ok("keyword", ruleset_match_domain(&rules, "m.vk.example", NULL) == RULE_ACTION_DIRECT);
    ok("bare name is a keyword", ruleset_match_domain(&rules, "xlocalhostx.test", NULL) == RULE_ACTION_DIRECT);
    ok("proxy suffix", ruleset_match_domain(&rules, "www.youtube.com", &index) == RULE_ACTION_PROXY &&
       index != SIZE_MAX);
    ok("cidr", ruleset_match_ip(&rules, "10.9.8.7", NULL) == RULE_ACTION_DIRECT);
    ok("bare v4", ruleset_match_ip(&rules, "192.168.1.1", NULL) == RULE_ACTION_DIRECT &&
       ruleset_match_ip(&rules, "192.168.1.2", &index) == RULE_ACTION_PROXY);
    ok("bare v6", ruleset_match_ip(&rules, "2001:db8::1", NULL) == RULE_ACTION_DIRECT);
    ok("recompile after removal", ruleset_remove_source(&rules, 4) == 14 && rules.count == 0);

    /* the same name under two actions ends with the strictest */
    const char *dup = "{\"ProxySites\":[\"domain:both.test\"],\"DirectSites\":[\"domain:both.test\"],"
                      "\"BlockSites\":[\"domain:both.test\"]}";
    ruleset_init(&rules);
    ok("dup compile", routing_compile(dup, strlen(dup), 1, &rules, &info) == ROUTING_OK);
    ok("dup strictest", rules.count == 1 && rules.entries[0].action == RULE_ACTION_BLOCK);

    /* entries the ruleset cannot hold are counted, not silently lost */
    ruleset_init(&rules);
    rules.count = RULESET_MAX_RULES;
    ok("full ruleset", routing_compile(dup, strlen(dup), 1, &rules, &info) == ROUTING_OK && info.dropped >= 1 &&
       info.rules == 0);

    ruleset_init(&rules);
    ok("source zero refused", routing_compile(kProfile, strlen(kProfile), 0, &rules, NULL) == ROUTING_ERR_ARG);
    const char *junk = "{\"DirectSites\":[1,null,\"\",\"a b\",\"domain:\"],\"BlockIp\":[\"1.2.3.4/33\",\"x\"]}";
    ok("junk entries", routing_compile(junk, strlen(junk), 1, &rules, &info) == ROUTING_OK &&
       info.rules == 0 && info.unsupported == 7);
}

static void test_tags(void) {
    char out[8][GEO_TAG_MAX];
    size_t n = routing_geo_tags(kProfile, strlen(kProfile), GEO_KIND_SITE, out, 8);
    ok("site tags", n == 3);
    int found_ru = 0, found_ads = 0, found_google = 0;
    for (size_t i = 0; i < n && i < 8; ++i) {
        found_ru |= strcmp(out[i], "category-ru") == 0;
        found_ads |= strcmp(out[i], "category-ads-all") == 0;
        found_google |= strcmp(out[i], "google@cn") == 0;
    }
    ok("site tag names", found_ru && found_ads && found_google);
    n = routing_geo_tags(kProfile, strlen(kProfile), GEO_KIND_IP, out, 8);
    ok("ip tags skip private", n == 2 && ((!strcmp(out[0], "ru") && !strcmp(out[1], "cloudflare")) ||
                                         (!strcmp(out[1], "ru") && !strcmp(out[0], "cloudflare"))));
    ok("count beyond max", routing_geo_tags(kProfile, strlen(kProfile), GEO_KIND_SITE, out, 1) == 3);
    ok("bad json tags", routing_geo_tags("x", 1, GEO_KIND_IP, out, 8) == 0);
    routing_counts_t counts;
    ok("counts", routing_count(kProfile, strlen(kProfile), &counts) == ROUTING_OK &&
       counts.supported == 14 && counts.unsupported == 2 && counts.geo_tags == 6);
    ok("counts of junk", routing_count("x", 1, &counts) == ROUTING_ERR_FORMAT && counts.supported == 0);

    routing_entry_t entries[32];
    size_t total = routing_entries(kProfile, strlen(kProfile), entries, 32);
    ok("entries total", total == 16);
    size_t bad = 0;
    for (size_t i = 0; i < total; ++i) bad += !entries[i].supported;
    ok("entries unsupported", bad == 2);
    ok("entries order", entries[0].action == RULE_ACTION_BLOCK);
}

static void test_body_import(void) {
    /* the routing line shares the body with the nodes and must neither add a
       server nor show up as a link that failed to open */
    vl_server_t servers[8];
    cfg_import_stats_t stats;
    size_t count = 0;
    char *link = link_of("happ://routing/onadd/", kProfile);
    char *body = malloc(strlen(link) + 400);
    sprintf(body,
            "vless://11111111-2222-3333-4444-555555555555@node.example:443"
            "?security=tls&sni=node.example&type=tcp#one\n%s\n", link);
    memset(&stats, 0, sizeof stats);
    ok("routing line beside a node", cfg_parse_subscription_ex(body, strlen(body), servers, 8,
                                                               &count, &stats) == CFG_OK &&
       count == 1 && stats.skipped == 0);
    count = 0;
    memset(&stats, 0, sizeof stats);
    ok("routing line alone is no server", cfg_parse_subscription_ex(link, strlen(link), servers, 8,
                                                                    &count, &stats) == CFG_OK &&
       count == 0 && stats.skipped == 0);
    free(body);
    free(link);
}

int main(void) {
    test_body_import();
    test_decode();
    test_find();
    test_inspect();
    test_compile();
    test_tags();
    if (failures) {
        fprintf(stderr, "%d routing profile checks failed\n", failures);
        return 1;
    }
    puts("all routing profile checks passed");
    return 0;
}
