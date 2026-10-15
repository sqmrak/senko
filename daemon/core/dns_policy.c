#include "dns_policy.h"

#include <stdio.h>
#include <string.h>

void dns_policy_init(dns_policy_t *policy) {
    if (policy) memset(policy, 0, sizeof *policy);
}

void dns_policy_clear(dns_policy_t *policy) {
    if (!policy) return;
    uint64_t evicted = policy->evicted;
    memset(policy, 0, sizeof *policy);
    policy->evicted = evicted;
}

static void host_to_bytes(uint32_t host, uint8_t out[4]) {
    out[0] = (uint8_t)(host >> 24);
    out[1] = (uint8_t)(host >> 16);
    out[2] = (uint8_t)(host >> 8);
    out[3] = (uint8_t)host;
}

static dns_policy_entry_t *find(dns_policy_t *policy, const uint8_t address[4]) {
    for (size_t i = 0; i < DNS_POLICY_CAP; ++i)
        if (policy->entries[i].used &&
            memcmp(policy->entries[i].address, address, 4) == 0)
            return &policy->entries[i];
    return NULL;
}

/* an expired entry is free space; only a table of live entries evicts */
static dns_policy_entry_t *slot_for(dns_policy_t *policy, uint64_t now) {
    dns_policy_entry_t *oldest = NULL;
    for (size_t i = 0; i < DNS_POLICY_CAP; ++i) {
        dns_policy_entry_t *entry = &policy->entries[i];
        if (!entry->used ||
            (entry->direct_until <= now && entry->proxy_until <= now))
            return entry;
        if (!oldest || entry->last_used < oldest->last_used) oldest = entry;
    }
    ++policy->evicted;
    return oldest;
}

void dns_policy_record(dns_policy_t *policy, const char *domain, rule_action_t action,
                       const uint32_t *addresses, size_t count,
                       uint64_t now, uint32_t ttl) {
    if (!policy || !addresses ||
        (action != RULE_ACTION_DIRECT && action != RULE_ACTION_PROXY))
        return;
    if (ttl == 0) ttl = 1;
    for (size_t i = 0; i < count; ++i) {
        uint8_t address[4];
        host_to_bytes(addresses[i], address);
        dns_policy_entry_t *entry = find(policy, address);
        if (!entry) {
            entry = slot_for(policy, now);
            memset(entry, 0, sizeof *entry);
            memcpy(entry->address, address, 4);
            entry->used = 1;
        }
        entry->last_used = ++policy->use_clock;
        uint64_t until = now + ttl;
        if (action == RULE_ACTION_DIRECT) {
            if (entry->direct_until < until) entry->direct_until = until;
            snprintf(entry->domain, sizeof entry->domain, "%s", domain ? domain : "");
        } else if (entry->proxy_until < until) {
            entry->proxy_until = until;
        }
    }
}

int dns_policy_is_direct(dns_policy_t *policy, const uint8_t address[4], uint64_t now,
                         char *domain_out, size_t domain_cap) {
    if (domain_out && domain_cap) domain_out[0] = '\0';
    if (!policy || !address) return 0;
    dns_policy_entry_t *entry = find(policy, address);
    if (!entry || entry->direct_until <= now || entry->proxy_until > now) return 0;
    entry->last_used = ++policy->use_clock;
    if (domain_out && domain_cap) snprintf(domain_out, domain_cap, "%s", entry->domain);
    return 1;
}

int dns_policy_verdict(dns_policy_t *policy, const uint8_t address[4], uint64_t now,
                       char *domain_out, size_t domain_cap) {
    if (domain_out && domain_cap) domain_out[0] = '\0';
    if (!policy || !address) return -1;
    dns_policy_entry_t *entry = find(policy, address);
    if (!entry) return -1;
    if (entry->proxy_until > now) {
        entry->last_used = ++policy->use_clock;
        return RULE_ACTION_PROXY;
    }
    if (entry->direct_until > now) {
        entry->last_used = ++policy->use_clock;
        if (domain_out && domain_cap) snprintf(domain_out, domain_cap, "%s", entry->domain);
        return RULE_ACTION_DIRECT;
    }
    return -1;
}

void dns_policy_counts(const dns_policy_t *policy, uint64_t now,
                       dns_policy_counts_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!policy) return;
    for (size_t i = 0; i < DNS_POLICY_CAP; ++i) {
        const dns_policy_entry_t *entry = &policy->entries[i];
        if (!entry->used || (entry->direct_until <= now && entry->proxy_until <= now))
            continue;
        ++out->addresses;
        if (entry->direct_until > now && entry->proxy_until <= now) ++out->direct;
    }
}
