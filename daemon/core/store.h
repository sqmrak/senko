#ifndef STORE_H
#define STORE_H

#include <stddef.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORE_MAX_SERVERS 256
#define STORE_MAX_SUBS    32

#define STORE_GROUP_MANUAL (-1)

typedef struct {
    char name[64];
    char url[512];
    uint64_t expire;
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

store_status_t store_add_sub(store_t *st, const char *name, const char *url,
                             size_t *out_sub);

/* replace one subscription's servers */
store_status_t store_refresh_sub(store_t *st, size_t sub_index,
                                 const char *blob, size_t blob_len,
                                 size_t *out_added);

store_status_t store_remove(store_t *st, size_t index);
store_status_t store_move_manual(store_t *st, size_t index, size_t to_pos);

/* remove one subscription and its servers */
store_status_t store_remove_sub(store_t *st, size_t sub_index);

void store_set_sub_expire(store_t *st, size_t sub_index, uint64_t expire);

store_status_t store_select(store_t *st, int index);

const vl_server_t *store_selected(const store_t *st);
int store_link_at(const store_t *st, size_t index, char *buf, size_t cap);

/* save the store in a simple restart format */
store_status_t store_serialize(const store_t *st, char *buf, size_t cap, size_t *out_len);

/* load the store and skip bad lines */
store_status_t store_deserialize(store_t *st, const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* store_h */
