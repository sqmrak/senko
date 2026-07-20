#ifndef CTL_ENGINE_H
#define CTL_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "control.h"
#include "store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* separate command decisions from loop side effects */
typedef enum {
    CTL_ACT_NONE = 0, /* keep the data path unchanged */
    CTL_ACT_START, /* ask the daemon to start the selected tunnel */
    CTL_ACT_STOP, /* ask the daemon to stop the active tunnel */
    CTL_ACT_PING, /* ask the daemon to measure the selected server */
    CTL_ACT_REFRESH /* ask the daemon to refresh a subscription */
} ctl_action_kind_t;

typedef struct {
    ctl_action_kind_t kind;
    int               server_index; /* target retained for async work */
    vl_server_t       server; /* copy retained after store lookup */
} ctl_action_t;

typedef struct {
    store_t      store;
    ctl_state_t  state; /* state shared with the control protocol */
} ctl_engine_t;

void ctl_engine_init(ctl_engine_t *e);

/* emit the immediate protocol event and defer network work to the daemon */
ctl_status_t ctl_engine_handle(ctl_engine_t *e, const ctl_cmd_t *cmd,
                               char *out, size_t cap, size_t *out_len,
                               ctl_action_t *action);

/* publish the real asynchronous result after the data path settles */
ctl_status_t ctl_engine_notify(ctl_engine_t *e, ctl_state_t new_state,
                               char *out, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ctl_engine_h */
