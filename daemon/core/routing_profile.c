#include "routing_profile.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "b64.h"
#include "config.h"
#include "happ.h"
#include "third_party/cJSON.h"


#define GEO_URL_DEFAULT_IP \
    "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/geoip.dat"
#define GEO_URL_DEFAULT_SITE \
    "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/geosite.dat"

static const struct {
    const char *key;
    rule_action_t action;
    int is_ip;
} kLists[] = {
    /* strictest first: a name listed twice keeps the strictest action */
    { "BlockSites", RULE_ACTION_BLOCK, 0 },
    { "BlockIp", RULE_ACTION_BLOCK, 1 },
    { "DirectSites", RULE_ACTION_DIRECT, 0 },
    { "DirectIp", RULE_ACTION_DIRECT, 1 },
    { "ProxySites", RULE_ACTION_PROXY, 0 },
    { "ProxyIp", RULE_ACTION_PROXY, 1 }
};
#define LIST_COUNT (sizeof kLists / sizeof kLists[0])

static const char *const kProfileKeys[] = {
    "Name", "GlobalProxy", "DirectSites", "DirectIp", "ProxySites", "ProxyIp",
    "BlockSites", "BlockIp", "Geoipurl", "Geositeurl", "DomainStrategy",
    "RemoteDNSType", "RemoteDNSDomain", "RemoteDNSIP", "DomesticDNSType",
    "DomesticDNSDomain", "DomesticDNSIP", "DnsHosts", "FakeDNS", "LastUpdated"
};

static const char *skip_space(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) ++p;
    return p;
}

static int has_prefix(const char *p, size_t len, const char *prefix) {
    size_t n = strlen(prefix);
    return len >= n && strncasecmp(p, prefix, n) == 0;
}

static routing_status_t decode_payload(const char *p, size_t len, char *json,
                                       size_t json_cap) {
    char *plain = NULL;
    unsigned char *raw = NULL;
    routing_status_t result = ROUTING_ERR_FORMAT;
    size_t n = 0;
    if (len == 0) return ROUTING_ERR_FORMAT;
    if (len > ROUTING_LINK_MAX) return ROUTING_ERR_TOOBIG;
    plain = (char *)malloc(len + 1);
    if (!plain) return ROUTING_ERR_ARG;
    if (memchr(p, '%', len)) {
        int d = url_percent_decode(p, len, plain, len + 1);
        if (d < 0) goto done;
        n = (size_t)d;
    } else {
        memcpy(plain, p, len);
        n = len;
    }
    plain[n] = '\0';
    {
        const char *s = skip_space(plain, plain + n);
        if (s < plain + n && *s == '{') {
            size_t jl = (size_t)(plain + n - s);
            if (jl >= json_cap || jl > ROUTING_JSON_MAX) { result = ROUTING_ERR_TOOBIG; goto done; }
            memcpy(json, s, jl);
            json[jl] = '\0';
            result = ROUTING_OK;
            goto done;
        }
    }
    {
        size_t cap = b64_decoded_maxlen(n) + 4;
        size_t out_len = 0;
        raw = (unsigned char *)malloc(cap);
        if (!raw) { result = ROUTING_ERR_ARG; goto done; }
        if (happ_b64_decode_ex(plain, n, raw, cap, &out_len) != 0 || out_len == 0) goto done;
        const char *s = skip_space((const char *)raw, (const char *)raw + out_len);
        size_t left = (size_t)((const char *)raw + out_len - s);
        if (left == 0 || *s != '{') goto done;
        if (left >= json_cap || left > ROUTING_JSON_MAX) { result = ROUTING_ERR_TOOBIG; goto done; }
        memcpy(json, s, left);
        json[left] = '\0';
        result = ROUTING_OK;
    }
done:
    free(plain);
    free(raw);
    return result;
}

routing_status_t routing_decode(const char *text, size_t len, routing_cmd_t *cmd,
                                char *json, size_t json_cap) {
    if (!text || !cmd || !json || json_cap < 8) return ROUTING_ERR_ARG;
    json[0] = '\0';
    *cmd = ROUTING_CMD_ONADD;
    const char *end = text + len;
    const char *p = skip_space(text, end);
    while (end > p && isspace((unsigned char)end[-1])) --end;
    size_t n = (size_t)(end - p);
    if (n == 0) return ROUTING_ERR_FORMAT;
    if (has_prefix(p, n, "happ://routing/")) {
        p += 15;
        n -= 15;
        if (n == 3 && strncasecmp(p, "off", 3) == 0) {
            *cmd = ROUTING_CMD_OFF;
            return ROUTING_OK;
        }
        if (has_prefix(p, n, "onadd/")) {
            *cmd = ROUTING_CMD_ONADD;
            p += 6;
            n -= 6;
        } else if (has_prefix(p, n, "add/")) {
            *cmd = ROUTING_CMD_ADD;
            p += 4;
            n -= 4;
        } else {
            return ROUTING_ERR_FORMAT;
        }
    }
    /* a payload is one token: the profile of a body line ends at the line */
    size_t token = 0;
    if (n && *p == '{') token = n;
    else while (token < n && !isspace((unsigned char)p[token])) ++token;
    return decode_payload(p, token, json, json_cap);
}

routing_status_t routing_find(const char *header, const char *body, size_t body_len,
                              routing_cmd_t *cmd, char *json, size_t json_cap) {
    if (!cmd || !json || json_cap < 8) return ROUTING_ERR_ARG;
    json[0] = '\0';
    if (header) {
        const char *h = skip_space(header, header + strlen(header));
        if (*h) return routing_decode(h, strlen(h), cmd, json, json_cap);
    }
    if (!body) return ROUTING_ERR_NONE;
    const char *end = body + body_len;
    const char *line = body;
    while (line < end) {
        const char *nl = memchr(line, '\n', (size_t)(end - line));
        const char *le = nl ? nl : end;
        const char *p = skip_space(line, le);
        if (has_prefix(p, (size_t)(le - p), "happ://routing/"))
            return routing_decode(p, (size_t)(le - p), cmd, json, json_cap);
        line = nl ? nl + 1 : end;
    }
    return ROUTING_ERR_NONE;
}

static int url_ok(const char *s) {
    if (!s || strncmp(s, "https://", 8) != 0 || strlen(s) < 12 || strlen(s) >= ROUTING_URL_MAX)
        return 0;
    for (const char *p = s; *p; ++p)
        if ((unsigned char)*p <= 0x20 || (unsigned char)*p >= 0x7f) return 0;
    return 1;
}

static const char *string_of(const cJSON *item) {
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : NULL;
}

static routing_status_t parse_root(const char *json, size_t len, cJSON **out) {
    *out = NULL;
    if (!json || len == 0) return ROUTING_ERR_FORMAT;
    if (len > ROUTING_JSON_MAX) return ROUTING_ERR_TOOBIG;
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return ROUTING_ERR_FORMAT;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ROUTING_ERR_PROFILE;
    }
    int known = 0;
    for (size_t i = 0; i < sizeof kProfileKeys / sizeof kProfileKeys[0]; ++i)
        if (cJSON_GetObjectItem(root, kProfileKeys[i])) known = 1;
    if (!known) {
        cJSON_Delete(root);
        return ROUTING_ERR_PROFILE;
    }
    /* a list that is not an array would be read as empty, and a profile that
       silently routes nothing is worse than one that is refused */
    for (size_t i = 0; i < LIST_COUNT; ++i) {
        const cJSON *list = cJSON_GetObjectItem(root, kLists[i].key);
        if (list && !cJSON_IsArray(list) && !cJSON_IsNull(list)) {
            cJSON_Delete(root);
            return ROUTING_ERR_PROFILE;
        }
    }
    *out = root;
    return ROUTING_OK;
}

static void read_info(const cJSON *root, routing_info_t *info) {
    memset(info, 0, sizeof *info);
    info->global_proxy = 1;
    const char *name = string_of(cJSON_GetObjectItem(root, "Name"));
    if (name) snprintf(info->name, sizeof info->name, "%s", name);
    const cJSON *global = cJSON_GetObjectItem(root, "GlobalProxy");
    if (cJSON_IsFalse(global)) {
        info->global_proxy = 0;
    } else if (string_of(global)) {
        const char *g = string_of(global);
        if (strcasecmp(g, "false") == 0 || strcmp(g, "0") == 0) info->global_proxy = 0;
    }
    const cJSON *stamp = cJSON_GetObjectItem(root, "LastUpdated");
    if (cJSON_IsNumber(stamp) && stamp->valuedouble > 0) {
        info->last_updated = (uint64_t)stamp->valuedouble;
    } else if (string_of(stamp)) {
        char *stop = NULL;
        unsigned long long v = strtoull(string_of(stamp), &stop, 10);
        if (stop && stop != string_of(stamp) && *stop == '\0') info->last_updated = v;
    }
    const char *ip_url = string_of(cJSON_GetObjectItem(root, "Geoipurl"));
    const char *site_url = string_of(cJSON_GetObjectItem(root, "Geositeurl"));
    snprintf(info->geoip_url, sizeof info->geoip_url, "%s",
             url_ok(ip_url) ? ip_url : GEO_URL_DEFAULT_IP);
    snprintf(info->geosite_url, sizeof info->geosite_url, "%s",
             url_ok(site_url) ? site_url : GEO_URL_DEFAULT_SITE);
}

routing_status_t routing_inspect(const char *json, size_t len, routing_info_t *info) {
    cJSON *root;
    if (!info) return ROUTING_ERR_ARG;
    routing_status_t s = parse_root(json, len, &root);
    if (s != ROUTING_OK) return s;
    read_info(root, info);
    cJSON_Delete(root);
    return ROUTING_OK;
}

/* an entry as xray writes it, turned into the rule line the ruleset parses.
   0 when the entry maps, -1 when senko cannot match it */
static int entry_line(int is_ip, rule_action_t action, const char *entry,
                      char *line, size_t cap) {
    const char *type = NULL;
    const char *value = entry;
    char cidr[RULE_VALUE_MAX + 8];
    size_t n = strlen(entry);
    if (n == 0 || n >= RULE_VALUE_MAX) return -1;
    if (is_ip) {
        if (has_prefix(entry, n, "geoip:")) {
            type = "geoip";
            value = entry + 6;
        }
        if (!type) {
            uint8_t tmp[16];
            if (strchr(entry, '/')) {
                type = "ip-cidr";
            } else if (inet_pton(AF_INET, entry, tmp) == 1) {
                snprintf(cidr, sizeof cidr, "%s/32", entry);
                type = "ip-cidr";
                value = cidr;
            } else if (inet_pton(AF_INET6, entry, tmp) == 1) {
                snprintf(cidr, sizeof cidr, "%s/128", entry);
                type = "ip-cidr";
                value = cidr;
            } else {
                return -1;
            }
        }
    } else if (has_prefix(entry, n, "geosite:")) {
        type = "geosite";
        value = entry + 8;
    } else if (has_prefix(entry, n, "domain:")) {
        type = "domain-suffix";
        value = entry + 7;
    } else if (has_prefix(entry, n, "full:")) {
        type = "domain-full";
        value = entry + 5;
    } else if (has_prefix(entry, n, "keyword:")) {
        type = "domain-keyword";
        value = entry + 8;
    } else if (strchr(entry, ':')) {
        return -1; /* regexp:, ext:, dotless:, geoip: in a site list */
    } else {
        /* a bare name is a substring match in xray, and the profile was written
           for xray */
        type = "domain-keyword";
    }
    int w = snprintf(line, cap, "%s %s %s", rule_action_name(action), type, value);
    return w > 0 && (size_t)w < cap ? 0 : -1;
}

routing_status_t routing_compile(const char *json, size_t len, uint8_t source,
                                 ruleset_t *rules, routing_info_t *info) {
    cJSON *root;
    routing_info_t local;
    if (!rules || source == 0) return ROUTING_ERR_ARG;
    if (!info) info = &local;
    routing_status_t s = parse_root(json, len, &root);
    if (s != ROUTING_OK) return s;
    read_info(root, info);
    for (size_t i = 0; i < LIST_COUNT; ++i) {
        /* proxy first, block last: a name listed under two actions ends with the
           strictest, because a repeated rule replaces the earlier action */
        size_t k = LIST_COUNT - 1 - i;
        const cJSON *list = cJSON_GetObjectItem(root, kLists[k].key);
        int index = 0;
        const cJSON *item;
        if (!cJSON_IsArray(list)) continue;
        cJSON_ArrayForEach(item, list) {
            char line[RULE_VALUE_MAX + 40];
            rule_t rule;
            if (++index > ROUTING_LIST_MAX) {
                ++info->dropped;
                continue;
            }
            const char *entry = string_of(item);
            if (!entry || entry_line(kLists[k].is_ip, kLists[k].action, entry,
                                     line, sizeof line) != 0 ||
                rules_parse(line, strlen(line), &rule) != RULES_OK) {
                ++info->unsupported;
                continue;
            }
            rule.source = source;
            rules_status_t added = ruleset_add(rules, &rule, NULL);
            if (added == RULES_ERR_FULL) {
                ++info->dropped;
            } else if (added == RULES_OK) {
                ++info->rules;
                if (rule.type == RULE_TYPE_GEOSITE || rule.type == RULE_TYPE_GEOIP)
                    ++info->geo_tags;
            } else {
                ++info->unsupported;
            }
        }
    }
    cJSON_Delete(root);
    return ROUTING_OK;
}

size_t routing_geo_tags(const char *json, size_t len, geo_kind_t kind,
                        char (*out)[GEO_TAG_MAX], size_t max) {
    cJSON *root;
    size_t total = 0;
    if (parse_root(json, len, &root) != ROUTING_OK) return 0;
    for (size_t k = 0; k < LIST_COUNT; ++k) {
        const cJSON *list = cJSON_GetObjectItem(root, kLists[k].key);
        const cJSON *item;
        int index = 0;
        if (!cJSON_IsArray(list)) continue;
        cJSON_ArrayForEach(item, list) {
            char line[RULE_VALUE_MAX + 40];
            rule_t rule;
            const char *entry = string_of(item);
            if (++index > ROUTING_LIST_MAX) break;
            if (!entry || entry_line(kLists[k].is_ip, kLists[k].action, entry,
                                     line, sizeof line) != 0 ||
                rules_parse(line, strlen(line), &rule) != RULES_OK)
                continue;
            if (rule.type != (kind == GEO_KIND_SITE ? RULE_TYPE_GEOSITE : RULE_TYPE_GEOIP))
                continue;
            /* geoip:private needs no dat */
            if (rule.type == RULE_TYPE_GEOIP && strcmp(rule.value, "private") == 0)
                continue;
            size_t seen = 0;
            for (; seen < total && seen < max; ++seen)
                if (strcmp(out[seen], rule.value) == 0) break;
            if (seen < total && seen < max) continue;
            if (total < max)
                snprintf(out[total], GEO_TAG_MAX, "%.*s", GEO_TAG_MAX - 1, rule.value);
            ++total;
        }
    }
    cJSON_Delete(root);
    return total;
}

size_t routing_entries(const char *json, size_t len, routing_entry_t *out, size_t max) {
    cJSON *root;
    size_t total = 0;
    if (parse_root(json, len, &root) != ROUTING_OK) return 0;
    for (size_t k = 0; k < LIST_COUNT; ++k) {
        const cJSON *list = cJSON_GetObjectItem(root, kLists[k].key);
        const cJSON *item;
        int index = 0;
        if (!cJSON_IsArray(list)) continue;
        cJSON_ArrayForEach(item, list) {
            char line[RULE_VALUE_MAX + 40];
            rule_t rule;
            const char *entry = string_of(item);
            if (++index > ROUTING_LIST_MAX) break;
            if (!entry) continue;
            if (total < max) {
                snprintf(out[total].entry, sizeof out[total].entry, "%s", entry);
                out[total].action = kLists[k].action;
                out[total].is_ip = kLists[k].is_ip;
                out[total].supported =
                    entry_line(kLists[k].is_ip, kLists[k].action, entry, line, sizeof line) == 0 &&
                    rules_parse(line, strlen(line), &rule) == RULES_OK;
            }
            ++total;
        }
    }
    cJSON_Delete(root);
    return total;
}

routing_status_t routing_count(const char *json, size_t len, routing_counts_t *out) {
    cJSON *root;
    if (!out) return ROUTING_ERR_ARG;
    memset(out, 0, sizeof *out);
    routing_status_t s = parse_root(json, len, &root);
    if (s != ROUTING_OK) return s;
    for (size_t k = 0; k < LIST_COUNT; ++k) {
        const cJSON *list = cJSON_GetObjectItem(root, kLists[k].key);
        const cJSON *item;
        int index = 0;
        if (!cJSON_IsArray(list)) continue;
        cJSON_ArrayForEach(item, list) {
            char line[RULE_VALUE_MAX + 40];
            rule_t rule;
            const char *entry = string_of(item);
            if (++index > ROUTING_LIST_MAX) break;
            if (!entry || entry_line(kLists[k].is_ip, kLists[k].action, entry,
                                     line, sizeof line) != 0 ||
                rules_parse(line, strlen(line), &rule) != RULES_OK) {
                ++out->unsupported;
                continue;
            }
            ++out->supported;
            if (rule.type == RULE_TYPE_GEOSITE || rule.type == RULE_TYPE_GEOIP) ++out->geo_tags;
        }
    }
    cJSON_Delete(root);
    return ROUTING_OK;
}
