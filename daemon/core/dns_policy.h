#ifndef SENKO_DNS_POLICY_H
#define SENKO_DNS_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/* which way a flow to an address goes when the only thing that names it is a
   dns answer. an address is direct only while a direct-rule domain resolved to
   it and no proxied domain did, so a cdn address shared with a proxied site
   never leaves the tunnel because an unrelated direct rule also hit it */

#define DNS_POLICY_CAP 2048
#define DNS_POLICY_DOMAIN_MAX 64

typedef struct {
    uint8_t  address[4]; /* network byte order, as it appears in a packet */
    uint64_t direct_until;
    uint64_t proxy_until;
    uint64_t last_used;
    char     domain[DNS_POLICY_DOMAIN_MAX]; /* the direct domain, for the log */
    int      used;
} dns_policy_entry_t;

typedef struct {
    dns_policy_entry_t entries[DNS_POLICY_CAP];
    uint64_t use_clock;
    uint64_t evicted; /* a full table forgets the oldest; counted, never silent */
} dns_policy_t;

typedef struct {
    size_t addresses;
    size_t direct;
} dns_policy_counts_t;

void dns_policy_init(dns_policy_t *policy);

/* forgets every address, keeps the eviction count */
void dns_policy_clear(dns_policy_t *policy);

/* records the ipv4 answers (host order, as dns_msg_response_info gives them)
   of one lookup. only RULE_ACTION_DIRECT and RULE_ACTION_PROXY mean anything */
void dns_policy_record(dns_policy_t *policy, const char *domain, rule_action_t action,
                       const uint32_t *addresses, size_t count,
                       uint64_t now, uint32_t ttl);

/* 1 when flows to this address go direct right now; the domain that made it
   direct is copied to domain_out when given */
int dns_policy_is_direct(dns_policy_t *policy, const uint8_t address[4], uint64_t now,
                         char *domain_out, size_t domain_cap);

/* what the answers seen so far say about an address: RULE_ACTION_DIRECT only
   while a direct domain resolved to it and no proxied one did, RULE_ACTION_PROXY
   while a proxied domain resolved to it, and -1 when no live answer names it. a
   profile that sends unnamed traffic straight out needs the third case apart
   from the second */
int dns_policy_verdict(dns_policy_t *policy, const uint8_t address[4], uint64_t now,
                       char *domain_out, size_t domain_cap);

void dns_policy_counts(const dns_policy_t *policy, uint64_t now,
                       dns_policy_counts_t *out);

#ifdef __cplusplus
}
#endif

#endif
