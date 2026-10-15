#ifndef SENKO_RULES_H
#define SENKO_RULES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RULESET_MAX_RULES 4096
#define RULE_VALUE_MAX 256

typedef enum {
    RULE_TYPE_DOMAIN_SUFFIX = 0,
    RULE_TYPE_DOMAIN_KEYWORD,
    RULE_TYPE_IP_CIDR,
    RULE_TYPE_DOMAIN_FULL, /* the name itself, no subdomains */
    RULE_TYPE_GEOSITE,     /* value is a geo tag: a domain set from geosite.dat */
    RULE_TYPE_GEOIP,       /* value is a geo tag: an address set from geoip.dat */
    RULE_TYPE_APP          /* value is a bundle identifier; the flow's owner decides */
} rule_type_t;

typedef enum {
    RULE_ACTION_PROXY = 0,
    RULE_ACTION_DIRECT,
    RULE_ACTION_BLOCK
} rule_action_t;

typedef struct {
    rule_type_t type;
    rule_action_t action;
    char value[RULE_VALUE_MAX];
    uint8_t address[16];
    uint8_t prefix;
    uint8_t address_len;
    /* 0 for a rule the user typed, n + 1 for one that came from the routing
       profile of subscription n. profile rules are rebuilt from the profile
       and never saved with the user's own */
    uint8_t source;
    /* the loaded geo set of a geosite or geoip rule, owned by the ruleset while
       it is bound (ruleset_bind_geo) and NULL otherwise */
    void *geo;
    /* the counters are atomic, and armv7 ldrexd faults (EXC_ARM_DA_ALIGN) on
       the 4 byte alignment its abi gives a uint64_t inside a struct */
    uint64_t hits __attribute__((aligned(8)));
} rule_t;

typedef struct {
    rule_t entries[RULESET_MAX_RULES];
    size_t count;
    /* what a flow that no rule names does. a routing profile with global proxy
       off sends it straight out; everything else sends it through the tunnel */
    rule_action_t fallback;
} ruleset_t;

typedef enum {
    RULES_OK = 0,
    RULES_ERR_ARG = -1,
    RULES_ERR_TYPE = -2,
    RULES_ERR_ACTION = -3,
    RULES_ERR_VALUE = -4,
    RULES_ERR_FULL = -5,
    RULES_ERR_RANGE = -6
} rules_status_t;

void ruleset_init(ruleset_t *rules);
rules_status_t rules_parse(const char *text, size_t len, rule_t *out);
rules_status_t ruleset_add(ruleset_t *rules, const rule_t *rule, size_t *out_index);
rules_status_t ruleset_add_text(ruleset_t *rules, const char *text, size_t len,
                                size_t *out_index);
rules_status_t ruleset_remove(ruleset_t *rules, size_t index);
/* drops every rule that came from this profile source and returns how many */
size_t ruleset_remove_source(ruleset_t *rules, uint8_t source);

/* loads the set of every geosite and geoip rule from the per tag files in dir.
   a rule whose file is missing or unreadable stays unbound and matches nothing;
   missing counts those. a ruleset must be released before it is bound again,
   edited, or discarded */
rules_status_t ruleset_bind_geo(ruleset_t *rules, const char *dir, size_t *missing);
void ruleset_release_geo(ruleset_t *rules);

rule_action_t ruleset_match_domain(ruleset_t *rules, const char *domain,
                                   size_t *matched_index);
rule_action_t ruleset_match_ip(ruleset_t *rules, const char *ip,
                               size_t *matched_index);
/* an app rule for this bundle identifier: 1 and the strictest action when one
   exists, 0 otherwise. an app rule outranks every domain and address rule */
int ruleset_match_app(ruleset_t *rules, const char *bundle_id,
                      rule_action_t *action, size_t *matched_index);
int ruleset_has_type(const ruleset_t *rules, rule_type_t type);

const char *rule_type_name(rule_type_t type);
const char *rule_action_name(rule_action_t action);
uint64_t rule_hit_count(const rule_t *rule);

#ifdef __cplusplus
}
#endif

#endif
