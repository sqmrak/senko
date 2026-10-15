#include "rules.h"

#include "geo.h"

#include <stddef.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int action_rank(rule_action_t action) {
    if (action == RULE_ACTION_BLOCK) return 3;
    if (action == RULE_ACTION_DIRECT) return 2;
    return 1;
}

const char *rule_type_name(rule_type_t type) {
    if (type == RULE_TYPE_DOMAIN_SUFFIX) return "domain-suffix";
    if (type == RULE_TYPE_DOMAIN_KEYWORD) return "domain-keyword";
    if (type == RULE_TYPE_IP_CIDR) return "ip-cidr";
    if (type == RULE_TYPE_DOMAIN_FULL) return "domain-full";
    if (type == RULE_TYPE_GEOSITE) return "geosite";
    if (type == RULE_TYPE_GEOIP) return "geoip";
    if (type == RULE_TYPE_APP) return "app";
    return NULL;
}

const char *rule_action_name(rule_action_t action) {
    if (action == RULE_ACTION_PROXY) return "proxy";
    if (action == RULE_ACTION_DIRECT) return "direct";
    if (action == RULE_ACTION_BLOCK) return "block";
    return NULL;
}

/* a build where hits lost its alignment would crash on the first matched rule */
typedef char rule_hits_are_aligned[offsetof(rule_t, hits) % 8 == 0 ? 1 : -1];

uint64_t rule_hit_count(const rule_t *rule) {
    if (!rule) return 0;
    return __sync_fetch_and_add((uint64_t *)&rule->hits, 0);
}

void ruleset_init(ruleset_t *rules) {
    if (rules) memset(rules, 0, sizeof *rules);
}

static int parse_action(const char *text, size_t len, rule_action_t *out) {
    if (len == 5 && memcmp(text, "proxy", 5) == 0) *out = RULE_ACTION_PROXY;
    else if (len == 6 && memcmp(text, "direct", 6) == 0) *out = RULE_ACTION_DIRECT;
    else if (len == 5 && memcmp(text, "block", 5) == 0) *out = RULE_ACTION_BLOCK;
    else return -1;
    return 0;
}

static int parse_type(const char *text, size_t len, rule_type_t *out) {
    if (len == 13 && memcmp(text, "domain-suffix", 13) == 0)
        *out = RULE_TYPE_DOMAIN_SUFFIX;
    else if (len == 14 && memcmp(text, "domain-keyword", 14) == 0)
        *out = RULE_TYPE_DOMAIN_KEYWORD;
    else if (len == 7 && memcmp(text, "ip-cidr", 7) == 0)
        *out = RULE_TYPE_IP_CIDR;
    else if (len == 11 && memcmp(text, "domain-full", 11) == 0)
        *out = RULE_TYPE_DOMAIN_FULL;
    else if (len == 7 && memcmp(text, "geosite", 7) == 0)
        *out = RULE_TYPE_GEOSITE;
    else if (len == 5 && memcmp(text, "geoip", 5) == 0)
        *out = RULE_TYPE_GEOIP;
    else if (len == 3 && memcmp(text, "app", 3) == 0)
        *out = RULE_TYPE_APP;
    else return -1;
    return 0;
}

static int normalize_domain(const char *text, size_t len, char *out, size_t cap) {
    while (len > 0 && text[0] == '.') {
        ++text;
        --len;
    }
    while (len > 0 && text[len - 1] == '.') --len;
    if (len == 0 || len >= cap || len > 253) return -1;
    size_t label = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '.') {
            if (label == 0 || label > 63) return -1;
            label = 0;
            out[i] = '.';
            continue;
        }
        if (c >= 0x80 || isspace(c) || c == '/' || c == '\\') return -1;
        out[i] = (char)tolower(c);
        ++label;
    }
    if (label == 0 || label > 63) return -1;
    out[len] = '\0';
    return 0;
}

/* a geo tag is the name inside a geosite or geoip rule, lower case */
static int normalize_geo_tag(const char *text, size_t len, char *out, size_t cap) {
    if (len == 0 || len >= cap || len >= GEO_TAG_MAX) return -1;
    for (size_t i = 0; i < len; ++i) out[i] = (char)tolower((unsigned char)text[i]);
    out[len] = '\0';
    return geo_tag_valid(out) ? 0 : -1;
}

/* a bundle identifier is reverse dns text; anything else could never name an app
   and would only reach logs and the control socket */
static int normalize_bundle_id(const char *text, size_t len, char *out, size_t cap) {
    if (len == 0 || len >= cap || len > 200) return -1;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!(isalnum(c) || c == '.' || c == '-' || c == '_')) return -1;
        out[i] = (char)c;
    }
    out[len] = '\0';
    return 0;
}

static int parse_cidr(rule_t *rule, const char *text, size_t len) {
    char input[RULE_VALUE_MAX];
    char canonical[INET6_ADDRSTRLEN];
    if (len == 0 || len >= sizeof input) return -1;
    memcpy(input, text, len);
    input[len] = '\0';
    char *slash = strchr(input, '/');
    if (!slash || slash == input || !slash[1]) return -1;
    *slash++ = '\0';
    unsigned prefix = 0;
    for (const char *p = slash; *p; ++p) {
        if (*p < '0' || *p > '9') return -1;
        prefix = prefix * 10u + (unsigned)(*p - '0');
        if (prefix > 128) return -1;
    }
    int family;
    if (inet_pton(AF_INET, input, rule->address) == 1) {
        if (prefix > 32) return -1;
        family = AF_INET;
        rule->address_len = 4;
    } else if (inet_pton(AF_INET6, input, rule->address) == 1) {
        family = AF_INET6;
        rule->address_len = 16;
    } else {
        return -1;
    }
    rule->prefix = (uint8_t)prefix;
    unsigned whole = prefix / 8;
    unsigned bits = prefix % 8;
    if (bits && whole < rule->address_len)
        rule->address[whole] &= (uint8_t)(0xffu << (8 - bits));
    for (unsigned i = whole + (bits ? 1u : 0u); i < rule->address_len; ++i)
        rule->address[i] = 0;
    if (!inet_ntop(family, rule->address, canonical, sizeof canonical)) return -1;
    int n = snprintf(rule->value, sizeof rule->value, "%s/%u", canonical, prefix);
    return n > 0 && (size_t)n < sizeof rule->value ? 0 : -1;
}

rules_status_t rules_parse(const char *text, size_t len, rule_t *out) {
    if (!text || !out) return RULES_ERR_ARG;
    memset(out, 0, sizeof *out);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) --len;
    const char *first = memchr(text, ' ', len);
    if (!first) return RULES_ERR_ACTION;
    size_t action_len = (size_t)(first - text);
    if (parse_action(text, action_len, &out->action) != 0) return RULES_ERR_ACTION;
    const char *type = first + 1;
    size_t left = len - action_len - 1;
    const char *second = memchr(type, ' ', left);
    if (!second) return RULES_ERR_TYPE;
    size_t type_len = (size_t)(second - type);
    if (parse_type(type, type_len, &out->type) != 0) return RULES_ERR_TYPE;
    const char *value = second + 1;
    size_t value_len = left - type_len - 1;
    if (value_len == 0 || memchr(value, ' ', value_len)) return RULES_ERR_VALUE;
    if (out->type == RULE_TYPE_IP_CIDR) {
        if (parse_cidr(out, value, value_len) != 0) return RULES_ERR_VALUE;
    } else if (out->type == RULE_TYPE_GEOSITE || out->type == RULE_TYPE_GEOIP) {
        if (normalize_geo_tag(value, value_len, out->value, sizeof out->value) != 0)
            return RULES_ERR_VALUE;
    } else if (out->type == RULE_TYPE_APP) {
        if (normalize_bundle_id(value, value_len, out->value, sizeof out->value) != 0)
            return RULES_ERR_VALUE;
    } else if (normalize_domain(value, value_len, out->value,
                                sizeof out->value) != 0) {
        return RULES_ERR_VALUE;
    }
    return RULES_OK;
}

rules_status_t ruleset_add(ruleset_t *rules, const rule_t *rule, size_t *out_index) {
    if (!rules || !rule || !rule_type_name(rule->type) ||
        !rule_action_name(rule->action) || !rule->value[0]) return RULES_ERR_ARG;
    for (size_t i = 0; i < rules->count; ++i) {
        rule_t *old = &rules->entries[i];
        if (old->type != rule->type || old->source != rule->source ||
            strcmp(old->value, rule->value) != 0) continue;
        old->action = rule->action;
        old->hits = 0;
        if (out_index) *out_index = i;
        return RULES_OK;
    }
    if (rules->count >= RULESET_MAX_RULES) return RULES_ERR_FULL;
    rules->entries[rules->count] = *rule;
    rules->entries[rules->count].hits = 0;
    rules->entries[rules->count].geo = NULL;
    if (out_index) *out_index = rules->count;
    ++rules->count;
    return RULES_OK;
}

rules_status_t ruleset_add_text(ruleset_t *rules, const char *text, size_t len,
                                size_t *out_index) {
    rule_t rule;
    rules_status_t status = rules_parse(text, len, &rule);
    if (status != RULES_OK) return status;
    return ruleset_add(rules, &rule, out_index);
}

rules_status_t ruleset_remove(ruleset_t *rules, size_t index) {
    if (!rules) return RULES_ERR_ARG;
    if (index >= rules->count) return RULES_ERR_RANGE;
    if (index + 1 < rules->count)
        memmove(&rules->entries[index], &rules->entries[index + 1],
                (rules->count - index - 1) * sizeof rules->entries[0]);
    --rules->count;
    memset(&rules->entries[rules->count], 0, sizeof rules->entries[0]);
    return RULES_OK;
}

size_t ruleset_remove_source(ruleset_t *rules, uint8_t source) {
    size_t kept = 0;
    if (!rules) return 0;
    for (size_t i = 0; i < rules->count; ++i) {
        if (rules->entries[i].source == source) continue;
        if (kept != i) rules->entries[kept] = rules->entries[i];
        ++kept;
    }
    size_t removed = rules->count - kept;
    if (removed) memset(&rules->entries[kept], 0, removed * sizeof rules->entries[0]);
    rules->count = kept;
    return removed;
}

static int suffix_matches(const char *domain, const char *suffix) {
    size_t dl = strlen(domain);
    size_t sl = strlen(suffix);
    if (dl < sl || memcmp(domain + dl - sl, suffix, sl) != 0) return 0;
    return dl == sl || domain[dl - sl - 1] == '.';
}

rule_action_t ruleset_match_domain(ruleset_t *rules, const char *domain,
                                   size_t *matched_index) {
    char normalized[RULE_VALUE_MAX];
    int best_rank = 0;
    size_t best = SIZE_MAX;
    if (matched_index) *matched_index = SIZE_MAX;
    if (!rules || !domain ||
        normalize_domain(domain, strlen(domain), normalized, sizeof normalized) != 0)
        return RULE_ACTION_PROXY;
    for (size_t i = 0; i < rules->count; ++i) {
        rule_t *rule = &rules->entries[i];
        int match = 0;
        switch (rule->type) {
            case RULE_TYPE_DOMAIN_SUFFIX: match = suffix_matches(normalized, rule->value); break;
            case RULE_TYPE_DOMAIN_KEYWORD: match = strstr(normalized, rule->value) != NULL; break;
            case RULE_TYPE_DOMAIN_FULL: match = strcmp(normalized, rule->value) == 0; break;
            case RULE_TYPE_GEOSITE: match = geo_site_match((const geo_site_t *)rule->geo, normalized); break;
            default: break;
        }
        int rank = action_rank(rule->action);
        if (match && rank > best_rank) {
            best_rank = rank;
            best = i;
        }
    }
    if (best == SIZE_MAX) return RULE_ACTION_PROXY;
    (void)__sync_fetch_and_add(&rules->entries[best].hits, 1);
    if (matched_index) *matched_index = best;
    return rules->entries[best].action;
}

static int cidr_matches(const uint8_t *address, const rule_t *rule) {
    unsigned whole = rule->prefix / 8;
    unsigned bits = rule->prefix % 8;
    if (whole && memcmp(address, rule->address, whole) != 0) return 0;
    if (!bits) return 1;
    uint8_t mask = (uint8_t)(0xffu << (8 - bits));
    return (address[whole] & mask) == rule->address[whole];
}

rule_action_t ruleset_match_ip(ruleset_t *rules, const char *ip,
                               size_t *matched_index) {
    uint8_t address[16];
    uint8_t address_len;
    int best_rank = 0;
    size_t best = SIZE_MAX;
    if (matched_index) *matched_index = SIZE_MAX;
    if (!rules || !ip) return RULE_ACTION_PROXY;
    if (inet_pton(AF_INET, ip, address) == 1) address_len = 4;
    else if (inet_pton(AF_INET6, ip, address) == 1) address_len = 16;
    else return RULE_ACTION_PROXY;
    for (size_t i = 0; i < rules->count; ++i) {
        rule_t *rule = &rules->entries[i];
        if (rule->type == RULE_TYPE_GEOIP) {
            if (!geo_ip_match((const geo_ip_t *)rule->geo, address, address_len)) continue;
        } else if (rule->type != RULE_TYPE_IP_CIDR || rule->address_len != address_len ||
                   !cidr_matches(address, rule)) {
            continue;
        }
        int rank = action_rank(rule->action);
        if (rank > best_rank) {
            best_rank = rank;
            best = i;
        }
    }
    if (best == SIZE_MAX) return RULE_ACTION_PROXY;
    (void)__sync_fetch_and_add(&rules->entries[best].hits, 1);
    if (matched_index) *matched_index = best;
    return rules->entries[best].action;
}

int ruleset_match_app(ruleset_t *rules, const char *bundle_id,
                      rule_action_t *action, size_t *matched_index) {
    int best_rank = 0;
    size_t best = SIZE_MAX;
    if (matched_index) *matched_index = SIZE_MAX;
    if (!rules || !bundle_id || !bundle_id[0]) return 0;
    for (size_t i = 0; i < rules->count; ++i) {
        rule_t *rule = &rules->entries[i];
        if (rule->type != RULE_TYPE_APP || strcmp(rule->value, bundle_id) != 0) continue;
        int rank = action_rank(rule->action);
        if (rank > best_rank) {
            best_rank = rank;
            best = i;
        }
    }
    if (best == SIZE_MAX) return 0;
    (void)__sync_fetch_and_add(&rules->entries[best].hits, 1);
    if (matched_index) *matched_index = best;
    if (action) *action = rules->entries[best].action;
    return 1;
}

int ruleset_has_type(const ruleset_t *rules, rule_type_t type) {
    for (size_t i = 0; rules && i < rules->count; ++i)
        if (rules->entries[i].type == type) return 1;
    return 0;
}

void ruleset_release_geo(ruleset_t *rules) {
    if (!rules) return;
    for (size_t i = 0; i < rules->count; ++i) {
        rule_t *rule = &rules->entries[i];
        if (!rule->geo) continue;
        if (rule->type == RULE_TYPE_GEOSITE) geo_site_free((geo_site_t *)rule->geo);
        else if (rule->type == RULE_TYPE_GEOIP) geo_ip_free((geo_ip_t *)rule->geo);
        rule->geo = NULL;
    }
}

rules_status_t ruleset_bind_geo(ruleset_t *rules, const char *dir, size_t *missing) {
    size_t lost = 0;
    if (missing) *missing = 0;
    if (!rules || !dir) return RULES_ERR_ARG;
    for (size_t i = 0; i < rules->count; ++i) {
        rule_t *rule = &rules->entries[i];
        char path[512];
        if (rule->type != RULE_TYPE_GEOSITE && rule->type != RULE_TYPE_GEOIP) continue;
        if (rule->geo) continue;
        if (rule->type == RULE_TYPE_GEOIP && strcmp(rule->value, "private") == 0) {
            /* the private ranges are fixed, so no dat has to supply them */
            rule->geo = geo_ip_private();
            if (!rule->geo) {
                ruleset_release_geo(rules);
                return RULES_ERR_ARG;
            }
            continue;
        }
        geo_status_t status;
        if (geo_tag_path(dir, rule->type == RULE_TYPE_GEOSITE ? GEO_KIND_SITE : GEO_KIND_IP,
                         rule->value, path, sizeof path) != 0) {
            ++lost;
            continue;
        }
        if (rule->type == RULE_TYPE_GEOSITE) {
            geo_site_t *set = NULL;
            status = geo_site_load(path, &set);
            rule->geo = set;
        } else {
            geo_ip_t *set = NULL;
            status = geo_ip_load(path, &set);
            rule->geo = set;
        }
        if (status == GEO_ERR_MEMORY) {
            ruleset_release_geo(rules);
            return RULES_ERR_FULL;
        }
        if (status != GEO_OK) ++lost;
    }
    if (missing) *missing = lost;
    return RULES_OK;
}
