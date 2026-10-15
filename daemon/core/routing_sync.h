#ifndef SENKO_ROUTING_SYNC_H
#define SENKO_ROUTING_SYNC_H

#include <stddef.h>

#include "store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROUTING_SYNC_NONE = 0,   /* the panel sent no routing; the stored profile stays */
    ROUTING_SYNC_UNCHANGED,  /* the panel sent the profile the store already has */
    ROUTING_SYNC_SET,        /* a new or changed profile was stored */
    ROUTING_SYNC_OFF,        /* the panel withdrew its profile */
    ROUTING_SYNC_REFUSED     /* the panel sent routing senko cannot use */
} routing_sync_kind_t;

typedef struct {
    routing_sync_kind_t kind;
    int activated;       /* the profile is the active one after this refresh */
    char message[160];   /* for the user, empty when there is nothing to say */
} routing_sync_t;

/* what a refreshed subscription says about routing, applied to the store. the
   header is the routing response header (empty or NULL when there was none).
   a profile the panel sent with onadd becomes active; one sent with add becomes
   active only when no profile is. a panel can only withdraw its own profile.
   the compiled ruleset is not touched: it is rebuilt at the next connect */
void routing_sync_apply(store_t *st, size_t sub_index, const char *header,
                        int header_too_long, const char *body, size_t body_len,
                        routing_sync_t *out);

#ifdef __cplusplus
}
#endif

#endif
