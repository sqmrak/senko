#include "dns_cache.h"
#include "dns_msg.h"

#include <stdio.h>
#include <string.h>

static int failures;
static dns_cache_t cache;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static size_t make_response(uint8_t *response) {
    static const uint8_t query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0,
        3, 'a', 'd', 's', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0,
        0, 1, 0, 1
    };
    size_t len = 0;
    if (dns_msg_build_block(query, sizeof query, DNS_BLOCK_ZERO,
                            response, DNS_CACHE_RESPONSE_MAX, &len) != DNS_MSG_OK)
        return 0;
    return len;
}

int main(void) {
    uint8_t stored[DNS_CACHE_RESPONSE_MAX];
    uint8_t fetched[DNS_CACHE_RESPONSE_MAX];
    size_t stored_len = make_response(stored);
    size_t fetched_len = 0;
    rule_action_t verdict = RULE_ACTION_PROXY;
    int stale = 0;
    dns_response_info_t info;

    dns_cache_init(&cache);
    ok("put response",
       dns_cache_put(&cache, "ads.example", 1, stored, stored_len,
                     100, 60, RULE_ACTION_BLOCK) == DNS_CACHE_OK);
    ok("get fresh response",
       dns_cache_get(&cache, "ads.example", 1, 125, 0,
                     fetched, sizeof fetched, &fetched_len,
                     &verdict, &stale) == DNS_CACHE_OK &&
       verdict == RULE_ACTION_BLOCK && !stale && fetched_len == stored_len);
    ok("fresh response carries remaining ttl",
       dns_msg_response_info(fetched, fetched_len, &info) == DNS_MSG_OK &&
       info.min_ttl == 35);
    ok("expired response misses normally",
       dns_cache_get(&cache, "ads.example", 1, 160, 0,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, NULL) == DNS_CACHE_MISS);
    ok("expired response serves stale",
       dns_cache_get(&cache, "ads.example", 1, 160, 1,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, &stale) == DNS_CACHE_OK && stale);
    ok("stale response expires after a day",
       dns_cache_get(&cache, "ads.example", 1,
                     160 + DNS_CACHE_STALE_SECONDS + 1, 1,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, NULL) == DNS_CACHE_MISS);

    dns_cache_init(&cache);
    char name[64];
    for (size_t i = 0; i < DNS_CACHE_CAP; ++i) {
        snprintf(name, sizeof name, "host%lu.example", (unsigned long)i);
        ok("fill cache",
           dns_cache_put(&cache, name, 1, stored, stored_len,
                         100, 60, RULE_ACTION_PROXY) == DNS_CACHE_OK);
    }
    ok("touch oldest entry",
       dns_cache_get(&cache, "host0.example", 1, 110, 0,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, NULL) == DNS_CACHE_OK);
    ok("insert over lru",
       dns_cache_put(&cache, "new.example", 1, stored, stored_len,
                     110, 60, RULE_ACTION_DIRECT) == DNS_CACHE_OK);
    ok("recent entry survives lru",
       dns_cache_get(&cache, "host0.example", 1, 111, 0,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, NULL) == DNS_CACHE_OK);
    ok("least recent entry is evicted",
       dns_cache_get(&cache, "host1.example", 1, 111, 0,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, NULL) == DNS_CACHE_MISS);

    dns_cache_init(&cache);
    ok("counters start empty",
       cache.hits == 0 && cache.misses == 0 && cache.stale_hits == 0 &&
       dns_cache_entry_count(&cache) == 0);
    ok("store one answer",
       dns_cache_put(&cache, "counted.example", 1, stored, stored_len,
                     100, 60, RULE_ACTION_PROXY) == DNS_CACHE_OK);
    ok("a miss is counted",
       dns_cache_get(&cache, "absent.example", 1, 110, 0,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, NULL) == DNS_CACHE_MISS &&
       cache.misses == 1 && cache.hits == 0);
    ok("a fresh hit is counted and is not stale",
       dns_cache_get(&cache, "counted.example", 1, 110, 0,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, NULL) == DNS_CACHE_OK &&
       cache.hits == 1 && cache.stale_hits == 0);
    ok("a stale hit is counted twice over",
       dns_cache_get(&cache, "counted.example", 1, 200, 1,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, &stale) == DNS_CACHE_OK && stale &&
       cache.hits == 2 && cache.stale_hits == 1);
    ok("one entry is held",
       dns_cache_entry_count(&cache) == 1);
    dns_cache_clear(&cache);
    ok("a flush drops the answers and keeps the counters",
       dns_cache_entry_count(&cache) == 0 &&
       cache.hits == 2 && cache.misses == 1 && cache.stale_hits == 1 &&
       dns_cache_get(&cache, "counted.example", 1, 110, 0,
                     fetched, sizeof fetched, &fetched_len,
                     NULL, NULL) == DNS_CACHE_MISS);

    if (failures) {
        fprintf(stderr, "%d dns_cache check(s) failed\n", failures);
        return 1;
    }
    puts("dns_cache tests passed");
    return 0;
}
