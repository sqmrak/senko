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
    int  used; /* keep deleted slots distinguishable from empty data */
} store_sub_t;

typedef struct {
    vl_server_t servers[STORE_MAX_SERVERS];
    int         group[STORE_MAX_SERVERS]; /* preserve subscription ownership */
    size_t      n;

    store_sub_t subs[STORE_MAX_SUBS];

    int         selected; /* preserve the selected server across refreshes */
} store_t;

typedef enum {
    STORE_OK        =  0,
    STORE_ERR_ARG   = -1,
    STORE_ERR_FULL  = -2, /* reject writes beyond fixed capacity */
    STORE_ERR_PARSE = -3, /* reject links that cannot be parsed */
    STORE_ERR_RANGE = -4, /* reject an index outside the store */
    STORE_ERR_EXISTS  = -5, /* avoid duplicate server entries */
    STORE_ERR_TOO_LONG = -6,/* reject data beyond fixed storage */
    STORE_ERR_UNSUPPORTED = -7
} store_status_t;

void store_init(store_t *st);

/* repair group references after subscription slots move */
void store_normalize(store_t *st);

store_status_t store_add_manual(store_t *st, const char *link, size_t *out_index);

store_status_t store_add_sub(store_t *st, const char *name, const char *url,
                             size_t *out_sub);

/* replace one subscription's servers while preserving selection */
store_status_t store_refresh_sub(store_t *st, size_t sub_index,
                                 const char *blob, size_t blob_len,
                                 size_t *out_added);

store_status_t store_remove(store_t *st, size_t index);

/* remove all servers owned by a subscription before freeing its slot */
store_status_t store_remove_sub(store_t *st, size_t sub_index);

store_status_t store_select(store_t *st, int index);

const vl_server_t *store_selected(const store_t *st);
int store_link_at(const store_t *st, size_t index, char *buf, size_t cap);

/* serialize the store in a dependency-free restart format */
store_status_t store_serialize(const store_t *st, char *buf, size_t cap, size_t *out_len);

/* load a store while skipping malformed lines for forward compatibility */
store_status_t store_deserialize(store_t *st, const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* store_h */
