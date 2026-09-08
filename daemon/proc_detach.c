#define _DEFAULT_SOURCE

#include "proc_detach.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* a daemon started through senko-kick is a descendant of the app, so it shares
   the app's session and inherits the app's jetsam band. once the app is swiped
   away that band is the first thing the kernel reclaims, and the tunnel dies
   with the ui that started it. the daemon has to declare for itself that it is
   a daemon; nothing about the way it was launched can do that for it */

#define MEMORYSTATUS_CMD_SET_PRIORITY_PROPERTIES 1
/* the band the system keeps its own long-lived daemons in */
#define SENKO_JETSAM_PRIORITY 19

typedef struct {
    int32_t priority;
    uint64_t user_data;
} jetsam_props_t;

typedef int (*memorystatus_control_fn)(uint32_t command, int32_t pid,
                                       uint32_t flags, void *buffer,
                                       size_t size);

void senko_proc_detach(void) {
    /* launchd already starts its jobs in their own session, and a session
       leader cannot call setsid again, so both are non-errors here */
    if (getsid(0) != getpid()) (void)setsid();

    /* memorystatus_control is missing before ios 6 */
    memorystatus_control_fn set_band =
        (memorystatus_control_fn)dlsym(RTLD_DEFAULT, "memorystatus_control");
    if (!set_band) return;

    jetsam_props_t props;
    props.priority = SENKO_JETSAM_PRIORITY;
    props.user_data = 0;
    (void)set_band(MEMORYSTATUS_CMD_SET_PRIORITY_PROPERTIES, (int32_t)getpid(),
                   0, &props, sizeof props);
}
