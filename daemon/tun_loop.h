#ifndef SENKO_TUN_LOOP_H
#define SENKO_TUN_LOOP_H

#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>

#include "tun_stack.h"
#include "utun_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the thread that owns the tunnel. tun_loop_run() is the only code that reads
   or writes the utun descriptor, calls into lwip, runs its timers and drains
   the pending queue, all on the thread that calls it.

   every other thread reaches it through tun_loop_post(): a bounded queue of
   commands, each stamped with the generation of the run it was meant for, so
   a command aimed at a run that already ended can never act on the next one.

   stopping follows one order: stop taking commands and packets, abort the
   flows and the stack's connections, remove the interface, drop the pending
   frames, close the descriptor. stopping twice is harmless */

#define TUN_LOOP_COMMAND_MAX 32
#define TUN_LOOP_READ_BATCH 64
/* how long to wait before retrying a device that refused a write for lack of
   kernel buffers: select() reports such a descriptor writable, so waiting on
   writability alone would spin */
#define TUN_LOOP_NOBUFS_RETRY_MS 10

typedef enum {
    TUN_LOOP_CMD_STOP = 1,
    /* values from here on are handed to the owner's command callback */
    TUN_LOOP_CMD_OWNER = 100
} tun_loop_command_type_t;

typedef struct {
    int      type;
    uint64_t generation;
    uint64_t argument;
} tun_loop_command_t;

typedef enum {
    TUN_LOOP_OK          =  0,
    TUN_LOOP_ERR_ARG     = -1,
    TUN_LOOP_ERR_FULL    = -2, /* the command queue is full, try again later */
    TUN_LOOP_ERR_STALE   = -3, /* the command was meant for another run */
    TUN_LOOP_ERR_STOPPED = -4, /* the loop no longer takes commands */
    TUN_LOOP_ERR_SYSTEM  = -5, /* a pipe or lock could not be created */
    TUN_LOOP_ERR_STACK   = -6, /* the ip stack refused to start */
    TUN_LOOP_ERR_OWNER   = -7  /* the flow owner refused to start */
} tun_loop_status_t;

/* why tun_loop_run() returned */
typedef enum {
    TUN_LOOP_ENDED_STOP = 0,     /* a stop command */
    TUN_LOOP_ENDED_DEVICE_EOF,   /* the other end of the descriptor went away */
    TUN_LOOP_ENDED_DEVICE_ERROR, /* reading or writing the descriptor failed */
    TUN_LOOP_ENDED_START_FAILED  /* the stack or its flow owner could not start */
} tun_loop_end_t;

typedef struct tun_loop tun_loop_t;

/* runs on the loop thread for every command of type TUN_LOOP_CMD_OWNER or
   above, with the stack the command may act on */
typedef void (*tun_loop_command_fn)(void *ctx, tun_stack_t *stack,
                                    const tun_loop_command_t *command);

/* how the owner of the flows plugs into the loop. every hook runs on the loop
   thread, so the owner may call into the stack from any of them */
typedef struct {
    /* the stack is up; the owner keeps the pointer for later calls */
    int (*start)(void *ctx, tun_stack_t *stack, char *error, size_t error_cap);
    /* add the descriptors the owner waits on, and shorten the wait when it
       has a deadline sooner than *wait_ms */
    void (*prepare)(void *ctx, fd_set *readable, fd_set *writable, int *highest,
                    uint32_t *wait_ms);
    /* what select reported for the owner's descriptors, and any work that
       came due */
    void (*dispatch)(void *ctx, const fd_set *readable, const fd_set *writable);
    /* the loop is ending: close what the owner holds before the stack goes */
    void (*stop)(void *ctx);
    void *ctx;
} tun_loop_hooks_t;

/* runs on the loop thread for every frame read from the device, before the
   stack sees it. nonzero means the owner took the frame, so the stack never
   does and may not be handed the same buffer again. the frame keeps its
   family header */
typedef int (*tun_loop_filter_fn)(void *ctx, uint8_t *frame, size_t frame_len);

typedef struct {
    tun_stack_config_t stack;
    tun_loop_hooks_t   hooks;
    tun_loop_filter_fn frame_filter; /* NULL hands every frame to the stack */
    void              *filter_ctx;
    uint64_t generation;          /* the run this loop is; commands must match it */
    size_t   command_capacity;    /* 0 picks TUN_LOOP_COMMAND_MAX */
    tun_loop_command_fn on_command;
    void    *command_ctx;
    void   (*on_ready)(void *ctx);
    void    *ready_ctx;
    int      retain_device_until_destroy;
} tun_loop_config_t;

typedef struct {
    tun_loop_end_t end;
    int            device_errno;     /* for TUN_LOOP_ENDED_DEVICE_ERROR */
    uint64_t       commands_run;
    uint64_t       commands_dropped; /* still queued when the loop stopped */
    char           message[256];     /* why it ended, in plain words */
} tun_loop_result_t;

size_t tun_loop_size(void);

/* may run on any thread. the device must already be open; the loop owns it
   from here and it is closed exactly once, whether or not tun_loop_run()
   ever starts */
tun_loop_status_t tun_loop_init(tun_loop_t *loop, const tun_loop_config_t *config,
                                utun_device_t *device);

/* the owning thread calls this and stays in it until the loop ends */
tun_loop_status_t tun_loop_run(tun_loop_t *loop, tun_loop_result_t *out_result);

/* any thread. never blocks on the loop's work */
tun_loop_status_t tun_loop_post(tun_loop_t *loop, const tun_loop_command_t *command);
void tun_loop_request_stop(tun_loop_t *loop);

/* after tun_loop_run() has returned, or when it never ran: closes the device
   if nothing did, and releases the queue's pipe and lock. safe to call twice */
void tun_loop_destroy(tun_loop_t *loop);

/* one frame the owner writes to the device itself, on the loop thread. it
   takes the same lock and ends the loop on a broken descriptor the way the
   stack's own writes do, but it is never queued: a busy device refuses it */
utun_device_status_t tun_loop_write_frame(tun_loop_t *loop, const uint8_t *frame, size_t len);

/* the stack, for the owner's callbacks on the loop thread */
tun_stack_t *tun_loop_stack(tun_loop_t *loop);
void tun_loop_byte_counts(tun_loop_t *loop, uint64_t *from_device,
                          uint64_t *to_device);

const char *tun_loop_status_name(tun_loop_status_t status);

#ifdef __cplusplus
}
#endif

#endif
