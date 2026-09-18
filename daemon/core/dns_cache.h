#ifndef SENKO_DNS_CACHE_H
#define SENKO_DNS_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DNS_CACHE_CAP 1024
#define DNS_CACHE_RESPONSE_MAX 2048
#define DNS_CACHE_TTL_MIN 30
#define DNS_CACHE_TTL_MAX 3600
#define DNS_CACHE_STALE_SECONDS (24u * 60u * 60u)

typedef struct {
    char name[RULE_VALUE_MAX];
    uint8_t response[DNS_CACHE_RESPONSE_MAX];
    uint16_t response_len;
    uint16_t qtype;
    uint64_t stored_at;
    uint64_t expires_at;
    uint64_t stale_until;
    uint64_t last_used;
    rule_action_t verdict;
    int used;
} dns_cache_entry_t;

typedef struct {
    dns_cache_entry_t entries[DNS_CACHE_CAP];
    uint64_t use_clock;
/* a forwarder that answers slowly and a forwarder that answers from a day old
   entry look identical from the outside, so the lookups are counted here and
   reported by the diagnostics screen */
    uint64_t hits;
    uint64_t misses;
    uint64_t stale_hits;
} dns_cache_t;

typedef enum {
    DNS_CACHE_OK = 0,
    DNS_CACHE_MISS = 1,
    DNS_CACHE_ERR_ARG = -1,
    DNS_CACHE_ERR_SPACE = -2,
    DNS_CACHE_ERR_FORMAT = -3
} dns_cache_status_t;

void dns_cache_init(dns_cache_t *cache);

/* drop every stored answer and keep the counters, because a manual flush is a
   thing the tester did, not a reason to lose the numbers it was measured against */
void dns_cache_clear(dns_cache_t *cache);

/* entries holding an answer, used or stale */
size_t dns_cache_entry_count(const dns_cache_t *cache);

dns_cache_status_t dns_cache_put(dns_cache_t *cache,
                                 const char *name, uint16_t qtype,
                                 const uint8_t *response, size_t response_len,
                                 uint64_t now, uint32_t ttl,
                                 rule_action_t verdict);

dns_cache_status_t dns_cache_get(dns_cache_t *cache,
                                 const char *name, uint16_t qtype,
                                 uint64_t now, int allow_stale,
                                 uint8_t *response, size_t cap,
                                 size_t *response_len,
                                 rule_action_t *verdict, int *stale);

#ifdef __cplusplus
}
#endif

#endif
