#include "tun_policy.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static ruleset_t rules;
static tun_policy_t policy;

static size_t query(uint8_t *out, uint16_t id, const char *name) {
    size_t n = 12;
    memset(out, 0, 12);
    out[0] = (uint8_t)(id >> 8); out[1] = (uint8_t)id; out[2] = 1; out[5] = 1;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        out[n++] = (uint8_t)len;
        memcpy(out + n, p, len);
        n += len;
        p += len;
        if (*p) ++p;
    }
    out[n++] = 0;
    out[n++] = 0; out[n++] = 1; out[n++] = 0; out[n++] = 1;
    return n;
}

/* the query turned into an answer with one A record */
static size_t answer(const uint8_t *q, size_t qlen, const uint8_t a[4], uint32_t ttl,
                     uint8_t *out) {
    memcpy(out, q, qlen);
    out[2] = 0x81; out[3] = 0x80; out[7] = 1;
    size_t n = qlen;
    const uint8_t rr[] = { 0xc0, 0x0c, 0, 1, 0, 1,
                           (uint8_t)(ttl >> 24), (uint8_t)(ttl >> 16),
                           (uint8_t)(ttl >> 8), (uint8_t)ttl, 0, 4 };
    memcpy(out + n, rr, sizeof rr);
    n += sizeof rr;
    memcpy(out + n, a, 4);
    return n + 4;
}

static void add(const char *text) {
    if (ruleset_add_text(&rules, text, strlen(text), NULL) != RULES_OK) {
        ++failures;
        fprintf(stderr, "FAIL rule %s\n", text);
    }
}


static int fake_app_calls;
static uint16_t fake_app_port;

/* the owner of a flow by the port it leaves from, as the daemon's table would */
static int fake_owner(void *ctx, const tun_flow_key_t *key, char *bundle, size_t cap) {
    (void)ctx;
    ++fake_app_calls;
    fake_app_port = key->source_port;
    if (key->protocol == TUN_FLOW_TCP && key->source_port == 40001) { snprintf(bundle, cap, "com.example.browser"); return 1; }
    if (key->protocol == TUN_FLOW_TCP && key->source_port == 40002) { snprintf(bundle, cap, "com.example.player"); return 1; }
    if (key->protocol == TUN_FLOW_UDP && key->source_port == 40003) { snprintf(bundle, cap, "com.example.browser"); return 1; }
    return 0;
}

static tun_flow_key_t flow_key(uint8_t protocol, uint16_t port, const uint8_t dest[4]) {
    tun_flow_key_t key;
    memset(&key, 0, sizeof key);
    key.protocol = protocol;
    key.source_port = port;
    key.address_len = 4;
    memcpy(key.destination, dest, 4);
    return key;
}

static void test_fallback_and_apps(void) {
    static ruleset_t r;
    static tun_policy_t p;
    static const uint8_t dest[4] = { 192, 0, 2, 10 }, other[4] = { 192, 0, 2, 11 };
    char why[96];
    ruleset_init(&r);
    r.fallback = RULE_ACTION_DIRECT;
    ok("a direct fallback alone keeps a policy and an address table",
       tun_policy_init(&p, &r) == TUN_POLICY_OK && p.rules == &r && p.map != NULL);
    ok("an unnamed domain follows the fallback", tun_policy_domain(&p, "any.example") == RULE_ACTION_DIRECT);
    ok("an unnamed address follows the fallback",
       tun_policy_flow(&p, dest, 4, 100, why, sizeof why) == RULE_ACTION_DIRECT && why[0]);
    tun_policy_free(&p);

    add("proxy domain-suffix via.example");
    add("direct domain-suffix local.example");
    add("block domain-suffix ads.example");
    add("proxy ip-cidr 203.0.113.0/24");
    ok("init with a fallback and rules", tun_policy_init(&p, &rules) == TUN_POLICY_OK);
    tun_policy_free(&p);

    /* the fallback is what the ruleset says, and the table separates "a proxied
       name resolved here" from "no name resolved here" */
    ruleset_init(&r);
    r.fallback = RULE_ACTION_DIRECT;
    ruleset_add_text(&r, "proxy domain-suffix via.example", 31, NULL);
    ruleset_add_text(&r, "proxy ip-cidr 203.0.113.0/24", 29, NULL);
    ruleset_add_text(&r, "block domain-suffix ads.example", 31, NULL);
    ok("init", tun_policy_init(&p, &r) == TUN_POLICY_OK);
    ok("a named proxy domain", tun_policy_domain(&p, "x.via.example") == RULE_ACTION_PROXY);
    ok("a block domain", tun_policy_domain(&p, "ads.example") == RULE_ACTION_BLOCK);
    ok("an unnamed domain goes direct", tun_policy_domain(&p, "plain.example") == RULE_ACTION_DIRECT);

    uint8_t q[512], a[512], out[512];
    size_t qlen = query(q, 7, "x.via.example");
    size_t alen = answer(q, qlen, dest, 300, a);
    dns_question_t question;
    ok("question", dns_msg_parse_question(q, qlen, &question) == DNS_MSG_OK);
    tun_policy_answer(&p, &question, RULE_ACTION_PROXY, a, alen, 100);
    ok("an address a proxied name resolved to stays on the tunnel",
       tun_policy_flow(&p, dest, 4, 101, why, sizeof why) == RULE_ACTION_PROXY);
    ok("an address nothing resolved to follows the fallback",
       tun_policy_flow(&p, other, 4, 101, why, sizeof why) == RULE_ACTION_DIRECT);
    ok("an ip rule outranks the fallback",
       tun_policy_flow(&p, (const uint8_t[]){ 203, 0, 113, 5 }, 4, 101, why, sizeof why) == RULE_ACTION_PROXY &&
       strstr(why, "ip rule") != NULL);
    qlen = query(q, 8, "plain.example");
    alen = answer(q, qlen, other, 300, a);
    ok("question 2", dns_msg_parse_question(q, qlen, &question) == DNS_MSG_OK);
    tun_policy_answer(&p, &question, RULE_ACTION_DIRECT, a, alen, 100);
    ok("an address an unnamed name resolved to goes direct",
       tun_policy_flow(&p, other, 4, 102, why, sizeof why) == RULE_ACTION_DIRECT);
    ok("the proxied address survives a second answer",
       tun_policy_flow(&p, dest, 4, 102, why, sizeof why) == RULE_ACTION_PROXY);
    (void)out;

    /* app rules decide ahead of names and addresses */
    ruleset_add_text(&r, "proxy app com.example.browser", 29, NULL);
    ruleset_add_text(&r, "block app com.example.player", 28, NULL);
    ruleset_add_text(&r, "direct app com.example.unseen", 29, NULL);
    tun_policy_free(&p);
    ok("init with app rules", tun_policy_init(&p, &r) == TUN_POLICY_OK);
    p.app_of = fake_owner;
    fake_app_calls = 0;
    tun_flow_key_t browser = flow_key(TUN_FLOW_TCP, 40001, other);
    tun_flow_key_t player = flow_key(TUN_FLOW_TCP, 40002, other);
    tun_flow_key_t browser_udp = flow_key(TUN_FLOW_UDP, 40003, other);
    tun_flow_key_t nobody = flow_key(TUN_FLOW_TCP, 40099, other);
    tun_flow_key_t browser_ip = flow_key(TUN_FLOW_TCP, 40001, (const uint8_t[]){ 203, 0, 113, 9 });
    ok("an app rule sends its flow through the tunnel against the fallback",
       tun_policy_flow_from(&p, &browser, 200, why, sizeof why) == RULE_ACTION_PROXY &&
       strcmp(why, "app com.example.browser") == 0 && fake_app_port == 40001);
    ok("an app rule blocks its flow",
       tun_policy_flow_from(&p, &player, 200, why, sizeof why) == RULE_ACTION_BLOCK);
    ok("udp is asked with its own protocol",
       tun_policy_flow_from(&p, &browser_udp, 200, why, sizeof why) == RULE_ACTION_PROXY);
    ok("an unknown owner falls back to the name and address rules",
       tun_policy_flow_from(&p, &nobody, 200, why, sizeof why) == RULE_ACTION_DIRECT);
    ok("an app rule outranks an ip rule",
       tun_policy_flow_from(&p, &browser_ip, 200, why, sizeof why) == RULE_ACTION_PROXY &&
       strcmp(why, "app com.example.browser") == 0);
    ok("app verdicts are counted", p.stats.flows_blocked == 1);
    int calls = fake_app_calls;
    tun_policy_free(&p);

    /* no app rules: the owner is never looked up */
    ruleset_init(&r);
    ruleset_add_text(&r, "direct domain-suffix a.example", 30, NULL);
    ok("init without app rules", tun_policy_init(&p, &r) == TUN_POLICY_OK);
    p.app_of = fake_owner;
    fake_app_calls = 0;
    tun_flow_key_t again = flow_key(TUN_FLOW_TCP, 40001, other);
    (void)tun_policy_flow_from(&p, &again, 200, why, sizeof why);
    ok("no lookup without an app rule", fake_app_calls == 0 && calls > 0);
    tun_policy_free(&p);
    tun_flow_key_t lone = flow_key(TUN_FLOW_TCP, 1, other);
    ok("no policy: through the tunnel", tun_policy_flow_from(NULL, &lone, 1, NULL, 0) == RULE_ACTION_PROXY &&
       tun_policy_flow_from(&p, NULL, 1, NULL, 0) == RULE_ACTION_PROXY);
}

int main(void) {
    ruleset_init(&rules);
    ok("no rules: everything goes through the server",
       tun_policy_init(&policy, &rules) == TUN_POLICY_OK && !policy.map &&
       tun_policy_domain(&policy, "any.example") == RULE_ACTION_PROXY);
    tun_policy_free(&policy);

    add("direct domain-suffix bank.example");
    add("block domain-suffix ads.example");
    add("block ip-cidr 203.0.113.0/24");
    add("direct ip-cidr 198.51.100.0/24");
    ok("init with a direct domain rule keeps an address table",
       tun_policy_init(&policy, &rules) == TUN_POLICY_OK && policy.map != NULL);

    ok("a block domain rule", tun_policy_domain(&policy, "x.ads.example") == RULE_ACTION_BLOCK);
    ok("a direct domain rule", tun_policy_domain(&policy, "www.bank.example") == RULE_ACTION_DIRECT);
    ok("an unruled domain", tun_policy_domain(&policy, "video.example") == RULE_ACTION_PROXY);

    char why[96];
    const uint8_t blocked[4] = { 203, 0, 113, 7 };
    const uint8_t cidr_direct[4] = { 198, 51, 100, 7 };
    const uint8_t resolved[4] = { 192, 0, 2, 44 };
    ok("an ip block rule", tun_policy_flow(&policy, blocked, 4, 100, why, sizeof why) ==
       RULE_ACTION_BLOCK && strstr(why, "203.0.113.0/24"));
    ok("an ip direct rule", tun_policy_flow(&policy, cidr_direct, 4, 100, why, sizeof why) ==
       RULE_ACTION_DIRECT);
    ok("an address nothing named goes through the server",
       tun_policy_flow(&policy, resolved, 4, 100, why, sizeof why) == RULE_ACTION_PROXY);

    /* the answer for a direct domain sends its address direct */
    uint8_t q[128], a[256], out[512];
    size_t qlen = query(q, 0x1111, "www.bank.example");
    size_t alen = answer(q, qlen, resolved, 600, a);
    dns_question_t question;
    ok("fixture question parses", dns_msg_parse_question(q, qlen, &question) == DNS_MSG_OK);
    tun_policy_answer(&policy, &question, RULE_ACTION_DIRECT, a, alen, 100);
    ok("a direct domain's answer sends its address direct",
       tun_policy_flow(&policy, resolved, 4, 110, why, sizeof why) == RULE_ACTION_DIRECT &&
       strstr(why, "www.bank.example"));
    ok("an ip rule outranks what dns said",
       tun_policy_flow(&policy, blocked, 4, 110, NULL, 0) == RULE_ACTION_BLOCK);

    /* cache: fresh with the new id, then stale only when allowed */
    uint8_t q2[128];
    size_t q2len = query(q2, 0x2222, "www.bank.example");
    size_t outlen = 0;
    ok("a fresh cached answer carries the new query id",
       tun_policy_cached(&policy, q2, q2len, &question, RULE_ACTION_DIRECT, 0, 120,
                         out, sizeof out, &outlen) &&
       outlen == alen && out[0] == 0x22 && out[1] == 0x22);
    ok("a cached answer is not reused under another verdict",
       !tun_policy_cached(&policy, q2, q2len, &question, RULE_ACTION_PROXY, 0, 120,
                          out, sizeof out, &outlen));
    ok("the ttl is clamped to the cache maximum, so it expires by then",
       !tun_policy_cached(&policy, q2, q2len, &question, RULE_ACTION_DIRECT, 0,
                          100 + DNS_CACHE_TTL_MAX + 1, out, sizeof out, &outlen));
    ok("but a slow upstream may still get the stale answer",
       tun_policy_cached(&policy, q2, q2len, &question, RULE_ACTION_DIRECT, 1,
                         100 + DNS_CACHE_TTL_MAX + 1, out, sizeof out, &outlen) &&
       policy.stats.dns_stale_answers == 1 && policy.stats.dns_cache_hits == 1);

    tun_policy_flush_map(&policy);
    ok("flushing the address table sends the address back through the server",
       tun_policy_flow(&policy, resolved, 4, 130, NULL, 0) == RULE_ACTION_PROXY);
    tun_policy_flush_cache(&policy);
    ok("flushing the cache forgets answers",
       !tun_policy_cached(&policy, q2, q2len, &question, RULE_ACTION_DIRECT, 1, 130,
                          out, sizeof out, &outlen));
    ok("flow verdicts are counted",
       policy.stats.flows_blocked == 2 && policy.stats.flows_direct == 2);
    tun_policy_free(&policy);

    test_fallback_and_apps();

    if (failures) {
        fprintf(stderr, "%d tun policy check(s) failed\n", failures);
        return 1;
    }
    puts("all tun policy checks passed");
    return 0;
}
