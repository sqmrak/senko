#ifndef SENKO_ROUTING_PROFILE_H
#define SENKO_ROUTING_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#include "geo.h"
#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/* a happ routing profile: a json object a subscription delivers in its
   "routing" response header or as a happ://routing/ line in its body.

   the profile json is kept as it arrived and compiled into rules each time the
   tunnel starts, so a profile and the rules built from it cannot drift apart */

#define ROUTING_JSON_MAX (12 * 1024)
#define ROUTING_LINK_MAX (20 * 1024)
#define ROUTING_NAME_MAX 64
#define ROUTING_URL_MAX 512
#define ROUTING_LIST_MAX 1024

typedef enum {
    ROUTING_OK = 0,
    ROUTING_ERR_ARG = -1,
    ROUTING_ERR_NONE = -2,    /* the subscription carried no routing */
    ROUTING_ERR_FORMAT = -3,  /* not a happ routing link or not json */
    ROUTING_ERR_TOOBIG = -4,
    ROUTING_ERR_PROFILE = -5  /* json, but not a routing profile */
} routing_status_t;

typedef enum {
    ROUTING_CMD_ADD = 0,   /* happ://routing/add: the profile joins the list */
    ROUTING_CMD_ONADD,     /* happ://routing/onadd: and becomes the active one */
    ROUTING_CMD_OFF        /* happ://routing/off: routing is switched off */
} routing_cmd_t;

typedef struct {
    char name[ROUTING_NAME_MAX];
    uint64_t last_updated;
    /* 1 when a flow no rule names goes through the tunnel (GlobalProxy true or
       missing), 0 when it goes straight out */
    int global_proxy;
    char geoip_url[ROUTING_URL_MAX];
    char geosite_url[ROUTING_URL_MAX];
    size_t rules;        /* rules the profile added */
    size_t unsupported;  /* entries senko cannot match: regexp, ext, odd syntax */
    size_t dropped;      /* entries the ruleset had no room for */
    size_t geo_tags;     /* geosite and geoip rules, which need dat data */
} routing_info_t;

/* turns one link, a bare base64 profile or a bare json profile into json text.
   a link carries its command, a bare profile counts as onadd */
routing_status_t routing_decode(const char *text, size_t len, routing_cmd_t *cmd,
                                char *json, size_t json_cap);

/* looks for the profile a subscription delivered: the routing header first,
   then a happ://routing/ line of the body. ROUTING_ERR_NONE when neither has one */
routing_status_t routing_find(const char *header, const char *body, size_t body_len,
                              routing_cmd_t *cmd, char *json, size_t json_cap);

/* reads the profile without adding rules, for validation and for the screen.
   a json object with none of the profile's fields is not a profile */
routing_status_t routing_inspect(const char *json, size_t len, routing_info_t *info);

/* adds the profile's rules to the ruleset under source, which marks them as the
   profile's. info may be NULL */
routing_status_t routing_compile(const char *json, size_t len, uint8_t source,
                                 ruleset_t *rules, routing_info_t *info);

/* the geo tags the profile names, one entry per tag; out holds up to max of
   them and the return value is how many the profile names in all */
size_t routing_geo_tags(const char *json, size_t len, geo_kind_t kind,
                        char (*out)[GEO_TAG_MAX], size_t max);

/* one entry of a profile as written, for the screen that lists it. supported is
   0 for an entry senko cannot match (regexp, ext and the like) */
typedef struct {
    rule_action_t action;
    int is_ip;
    char entry[RULE_VALUE_MAX + 16];
    int supported;
} routing_entry_t;

/* what the profile compiles to, without compiling it: entries senko can match,
   entries it cannot, and how many of the matchable ones are geo tags. the
   ruleset is a megabyte, so a screen listing every profile counts instead */
typedef struct {
    size_t supported;
    size_t unsupported;
    size_t geo_tags;
} routing_counts_t;

routing_status_t routing_count(const char *json, size_t len, routing_counts_t *out);

/* the entries of the profile in file order, at most max; returns the total */
size_t routing_entries(const char *json, size_t len, routing_entry_t *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif
