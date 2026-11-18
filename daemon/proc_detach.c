#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include "proc_detach.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

typedef int (*memorystatus_control_fn)(uint32_t command, int32_t pid,
                                       uint32_t flags, void *buffer,
                                       size_t size);

void senko_proc_detach(void) {
    /* launchd already starts its jobs in their own session, and a session
       leader cannot call setsid again, so both are non-errors here */
    if (getsid(0) != getpid()) (void)setsid();
}

/* ios 16 starts a launchd daemon from /var/jb under an 18 MB fatal footprint
   limit and ignores JetsamProperties in its plist; senkod at idle sits near
   6 MB, but every open connection adds its ~150 KB slot, so a busy tunnel
   was killed with EXC_RESOURCE. the limit is only ever raised: where the
   kernel reports none (-1, and ios before 9) the daemon stays unlimited */
#define MEMORYSTATUS_CMD_SET_MEMLIMIT_PROPERTIES 7
#define MEMORYSTATUS_CMD_GET_MEMLIMIT_PROPERTIES 8
#define MEMORYSTATUS_MEMLIMIT_ATTR_FATAL 0x1
#define SENKO_MEMLIMIT_MB 64

typedef struct {
    int32_t  active;
    uint32_t active_attr;
    int32_t  inactive;
    uint32_t inactive_attr;
} memlimit_props_t;

void senko_raise_memory_limit(void) {
    memorystatus_control_fn control =
        (memorystatus_control_fn)dlsym(RTLD_DEFAULT, "memorystatus_control");
    if (!control) return;

    memlimit_props_t cur;
    memset(&cur, 0, sizeof cur);
    if (control(MEMORYSTATUS_CMD_GET_MEMLIMIT_PROPERTIES, (int32_t)getpid(), 0,
                &cur, sizeof cur) != 0) {
        fprintf(stderr, "senkod: memory limit unreadable: %s\n", strerror(errno));
        return;
    }
    if ((cur.active <= 0 || cur.active >= SENKO_MEMLIMIT_MB) &&
        (cur.inactive <= 0 || cur.inactive >= SENKO_MEMLIMIT_MB))
        return;

    memlimit_props_t want = cur;
    if (cur.active > 0 && cur.active < SENKO_MEMLIMIT_MB) {
        want.active = SENKO_MEMLIMIT_MB;
        want.active_attr = MEMORYSTATUS_MEMLIMIT_ATTR_FATAL;
    }
    if (cur.inactive > 0 && cur.inactive < SENKO_MEMLIMIT_MB) {
        want.inactive = SENKO_MEMLIMIT_MB;
        want.inactive_attr = MEMORYSTATUS_MEMLIMIT_ATTR_FATAL;
    }
    if (control(MEMORYSTATUS_CMD_SET_MEMLIMIT_PROPERTIES, (int32_t)getpid(), 0,
                &want, sizeof want) != 0) {
        fprintf(stderr, "senkod: memory limit stays %d MB: %s\n",
                (int)cur.active, strerror(errno));
        return;
    }
    fprintf(stderr, "senkod: memory limit active %d -> %d MB, inactive %d -> %d MB\n",
            (int)cur.active, (int)want.active, (int)cur.inactive, (int)want.inactive);
}
