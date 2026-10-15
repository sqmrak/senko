#ifndef STORE_H
#define STORE_H

#include <stddef.h>

#include "config.h"
#include "routing_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORE_MAX_SERVERS 256
#define STORE_MAX_SUBS    32

#define STORE_GROUP_MANUAL (-1)

typedef struct {
    char name[64];
    char url[512];
    char header[512]; /* optional HTTP header for subscription fetches */
    uint64_t expire;
    uint64_t upload;
    uint64_t download;
    uint64_t total;
    char description[256];
    char support_url[512];
    /* wall clock second of the last successful refresh, 0 when never pulled.
       the scheduled refresh compares against it, so it has to survive a
       restart the same way the node list does */
    uint64_t last_refresh;
    /* the happ routing profile this subscription delivered, as json, or empty */
    char routing[ROUTING_JSON_MAX];
    int  used;
} store_sub_t;

typedef struct {
    vl_server_t servers[STORE_MAX_SERVERS];
    int         group[STORE_MAX_SERVERS]; /* keep server ownership */
    size_t      n;

    store_sub_t subs[STORE_MAX_SUBS];
    int         section_order[STORE_MAX_SUBS + 1];
    size_t      section_n;

    int         selected;
    ruleset_t   rules;
    /* the subscription whose routing profile is switched on, -1 for none. a
       profile never applies by itself: the owner picks one, or a link that says
       onadd does */
    int         routing_active;
} store_t;

typedef enum {
    STORE_OK        =  0,
    STORE_ERR_ARG   = -1,
    STORE_ERR_FULL  = -2, /* cap writes */
    STORE_ERR_PARSE = -3, /* reject bad links */
    STORE_ERR_RANGE = -4, /* reject bad indexes */
    STORE_ERR_EXISTS  = -5, /* skip duplicate servers */
    STORE_ERR_TOO_LONG = -6,/* cap stored text */
    STORE_ERR_UNSUPPORTED = -7
} store_status_t;

void store_init(store_t *st);

/* repair old group references */
void store_normalize(store_t *st);

size_t store_section_count(const store_t *st);
int store_section_at(const store_t *st, size_t pos);
store_status_t store_move_section(store_t *st, int section_id, size_t to_pos);

store_status_t store_add_manual(store_t *st, const char *link, size_t *out_index);

/* validate the new link before replacing a manual entry, so an invalid edit
   leaves the saved profile untouched */
store_status_t store_replace_manual(store_t *st, size_t index, const char *link);

/* the already parsed variant, used by bulk imports of foreign client profiles */
store_status_t store_add_manual_server(store_t *st, const vl_server_t *server,
                                       size_t *out_index);

/* drop every manual server in one step; out_removed counts what went */
store_status_t store_clear_manual(store_t *st, size_t *out_removed);

store_status_t store_add_sub(store_t *st, const char *name, const char *url,
                             size_t *out_sub);

/* keep the existing nodes while changing a subscription's fetch details */
store_status_t store_replace_sub(store_t *st, size_t sub_index, const char *name,
                                 const char *url, const char *header);

/* replace one subscription's servers */
store_status_t store_refresh_sub(store_t *st, size_t sub_index,
                                 const char *blob, size_t blob_len,
                                 size_t *out_added);

/* the same, with what the feed held that senko cannot run counted in stats and
   kept as VL_PROTO_UNSUPPORTED rows. moved (STORE_MAX_SERVERS entries, may be
   NULL) receives each old row's new index, -1 for a row that is gone, so
   state keyed by index survives the renumbering */
store_status_t store_refresh_sub_ex(store_t *st, size_t sub_index,
                                    const char *blob, size_t blob_len,
                                    size_t *out_added, cfg_import_stats_t *stats,
                                    int *moved);

store_status_t store_remove(store_t *st, size_t index);
store_status_t store_move_manual(store_t *st, size_t index, size_t to_pos);

store_status_t store_remove_sub(store_t *st, size_t sub_index);

void store_set_sub_expire(store_t *st, size_t sub_index, uint64_t expire);

void store_set_sub_refresh(store_t *st, size_t sub_index, uint64_t when);

void store_set_sub_meta(store_t *st, size_t sub_index, uint64_t upload,
                        uint64_t download, uint64_t total,
                        const char *description, const char *support_url);

/* adopt the panel's suggested title as the subscription's name, but only while
   the stored name still matches url_host: once it differs, a person chose that
   name on purpose and a refresh must not overwrite their choice */
store_status_t store_set_sub_title(store_t *st, size_t sub_index,
                                   const char *title, const char *url_host);

/* set a single optional HTTP request header for subscription refreshes */
store_status_t store_set_sub_header(store_t *st, size_t sub_index,
                                    const char *header);

store_status_t store_select(store_t *st, int index);

const vl_server_t *store_selected(const store_t *st);
int store_link_at(const store_t *st, size_t index, char *buf, size_t cap);

/* keeps the profile json a subscription delivered. a blank profile clears it.
   the json is checked here, so what is stored always compiles. the active
   profile of a cleared subscription goes off */
store_status_t store_set_sub_routing(store_t *st, size_t sub_index,
                                     const char *json, size_t len);
const char *store_sub_routing(const store_t *st, size_t sub_index);

/* -1 switches routing off; otherwise the subscription must hold a profile */
store_status_t store_set_routing_active(store_t *st, int sub_index);

/* rebuilds the profile's share of the ruleset: drops every rule that came from
   a profile, then compiles the active one and sets the fallback action.
   called with the tunnel down, because the running tunnel reads the ruleset.
   info may be NULL; it stays zeroed when no profile is active */
store_status_t store_apply_routing(store_t *st, routing_info_t *info);

store_status_t store_add_rule(store_t *st, const char *text, size_t len,
                              size_t *out_index);
store_status_t store_remove_rule(store_t *st, size_t index);

/* save the store in a simple restart format */
store_status_t store_serialize(const store_t *st, char *buf, size_t cap, size_t *out_len);

/* load the store and skip bad lines */
store_status_t store_deserialize(store_t *st, const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* store_h */
