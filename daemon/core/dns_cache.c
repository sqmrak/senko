#include "dns_cache.h"

#include "dns_msg.h"

#include <limits.h>
#include <string.h>

void dns_cache_init(dns_cache_t *cache) {
    if (cache) memset(cache, 0, sizeof *cache);
}

void dns_cache_clear(dns_cache_t *cache) {
    if (!cache) return;
    memset(cache->entries, 0, sizeof cache->entries);
    cache->use_clock = 0;
}

size_t dns_cache_entry_count(const dns_cache_t *cache) {
    size_t n = 0;
    if (!cache) return 0;
    for (size_t i = 0; i < DNS_CACHE_CAP; ++i)
        if (cache->entries[i].used) ++n;
    return n;
}

static int key_matches(const dns_cache_entry_t *entry,
                       const char *name, uint16_t qtype) {
    return entry->used && entry->qtype == qtype &&
           strcmp(entry->name, name) == 0;
}

static dns_cache_entry_t *find_entry(dns_cache_t *cache,
                                     const char *name, uint16_t qtype) {
    for (size_t i = 0; i < DNS_CACHE_CAP; ++i)
        if (key_matches(&cache->entries[i], name, qtype))
            return &cache->entries[i];
    return NULL;
}

static dns_cache_entry_t *replacement_entry(dns_cache_t *cache) {
    dns_cache_entry_t *oldest = &cache->entries[0];
    for (size_t i = 0; i < DNS_CACHE_CAP; ++i) {
        dns_cache_entry_t *entry = &cache->entries[i];
        if (!entry->used) return entry;
        if (entry->last_used < oldest->last_used) oldest = entry;
    }
    return oldest;
}

dns_cache_status_t dns_cache_put(dns_cache_t *cache,
                                 const char *name, uint16_t qtype,
                                 const uint8_t *response, size_t response_len,
                                 uint64_t now, uint32_t ttl,
                                 rule_action_t verdict) {
    size_t name_len;
    dns_cache_entry_t *entry;
    if (!cache || !name || !response || response_len == 0)
        return DNS_CACHE_ERR_ARG;
    name_len = strlen(name);
    if (name_len == 0 || name_len >= RULE_VALUE_MAX ||
        response_len > DNS_CACHE_RESPONSE_MAX)
        return DNS_CACHE_ERR_SPACE;
    if (ttl < DNS_CACHE_TTL_MIN) ttl = DNS_CACHE_TTL_MIN;
    if (ttl > DNS_CACHE_TTL_MAX) ttl = DNS_CACHE_TTL_MAX;
    entry = find_entry(cache, name, qtype);
    if (!entry) entry = replacement_entry(cache);
    memset(entry, 0, sizeof *entry);
    memcpy(entry->name, name, name_len + 1);
    memcpy(entry->response, response, response_len);
    entry->response_len = (uint16_t)response_len;
    entry->qtype = qtype;
    entry->stored_at = now;
    entry->expires_at = now + ttl;
    entry->stale_until = entry->expires_at + DNS_CACHE_STALE_SECONDS;
    entry->last_used = ++cache->use_clock;
    entry->verdict = verdict;
    entry->used = 1;
    return DNS_CACHE_OK;
}

dns_cache_status_t dns_cache_get(dns_cache_t *cache,
                                 const char *name, uint16_t qtype,
                                 uint64_t now, int allow_stale,
                                 uint8_t *response, size_t cap,
                                 size_t *response_len,
                                 rule_action_t *verdict, int *stale) {
    dns_cache_entry_t *entry;
    uint64_t age;
    uint32_t elapsed;
    if (response_len) *response_len = 0;
    if (stale) *stale = 0;
    if (!cache || !name || !response || !response_len)
        return DNS_CACHE_ERR_ARG;
    entry = find_entry(cache, name, qtype);
    if (!entry) {
        ++cache->misses;
        return DNS_CACHE_MISS;
    }
    if (now >= entry->expires_at && (!allow_stale || now > entry->stale_until)) {
        if (now > entry->stale_until) memset(entry, 0, sizeof *entry);
        ++cache->misses;
        return DNS_CACHE_MISS;
    }
    if (entry->response_len > cap) return DNS_CACHE_ERR_SPACE;
    memcpy(response, entry->response, entry->response_len);
    age = now > entry->stored_at ? now - entry->stored_at : 0;
    elapsed = age > UINT_MAX ? UINT_MAX : (uint32_t)age;
    if (dns_msg_patch_ttls(response, entry->response_len, elapsed) != DNS_MSG_OK)
        return DNS_CACHE_ERR_FORMAT;
    *response_len = entry->response_len;
    if (verdict) *verdict = entry->verdict;
    if (stale) *stale = now >= entry->expires_at;
    ++cache->hits;
    if (now >= entry->expires_at) ++cache->stale_hits;
    entry->last_used = ++cache->use_clock;
    return DNS_CACHE_OK;
}
