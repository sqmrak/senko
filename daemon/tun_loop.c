#include "tun_loop.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "core/senko_time.h"

struct tun_loop {
    tun_loop_config_t config;
    utun_device_t    *device;
    tun_stack_t      *stack; /* lives in the same allocation, right after this */

    pthread_mutex_t lock;
    int             lock_ready;
    int             wake[2]; /* written by posters, read by the loop */

    /* guarded by lock */
    int                accepting;
    int                stopping;
    tun_loop_command_t queue[TUN_LOOP_COMMAND_MAX];
    size_t             queue_head;
    size_t             queue_count;
    size_t             capacity;

    /* loop thread only */
    int      ran;
    int      device_closed;
    int      device_failed;
    int      device_errno;
    uint64_t commands_run;
    uint8_t  frame[TUN_STACK_FRAME_MAX + 1]; /* a spare byte shows an oversize read */
};

static size_t loop_part(void) {
    return (sizeof(struct tun_loop) + 15u) & ~(size_t)15u;
}

size_t tun_loop_size(void) {
    return loop_part() + tun_stack_size();
}

tun_stack_t *tun_loop_stack(tun_loop_t *loop) {
    return loop ? loop->stack : NULL;
}

void tun_loop_byte_counts(tun_loop_t *loop, uint64_t *from_device,
                          uint64_t *to_device) {
    if (from_device) *from_device = 0;
    if (to_device) *to_device = 0;
    if (!loop || !loop->lock_ready) return;
    pthread_mutex_lock(&loop->lock);
    if (from_device) *from_device = loop->device->bytes_in;
    if (to_device) *to_device = loop->device->bytes_out;
    pthread_mutex_unlock(&loop->lock);
}

const char *tun_loop_status_name(tun_loop_status_t status) {
    switch (status) {
    case TUN_LOOP_OK:          return "ok";
    case TUN_LOOP_ERR_ARG:     return "arg";
    case TUN_LOOP_ERR_FULL:    return "full";
    case TUN_LOOP_ERR_STALE:   return "stale";
    case TUN_LOOP_ERR_STOPPED: return "stopped";
    case TUN_LOOP_ERR_SYSTEM:  return "system";
    case TUN_LOOP_ERR_STACK:   return "stack";
    case TUN_LOOP_ERR_OWNER:   return "owner";
    }
    return "unknown";
}

static int set_flags(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return -1;
    int fd_flags = fcntl(fd, F_GETFD, 0);
    if (fd_flags < 0 || fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) != 0) return -1;
    return 0;
}

/* ---- the device, as the stack sees it ---------------------------------- */

utun_device_status_t tun_loop_write_frame(tun_loop_t *loop, const uint8_t *frame, size_t len) {
    pthread_mutex_lock(&loop->lock);
    utun_device_status_t status = utun_device_write_frame(loop->device, frame, len);
    int saved_errno = loop->device->last_errno;
    pthread_mutex_unlock(&loop->lock);
    if (status != UTUN_DEVICE_OK && status != UTUN_DEVICE_AGAIN &&
        status != UTUN_DEVICE_ERR_SHORT) {
        /* a descriptor that fails a write will fail the next one too; the
           loop ends instead of dropping every packet one at a time */
        loop->device_failed = 1;
        loop->device_errno = saved_errno;
    }
    return status;
}

static tun_stack_write_t loop_write(void *ctx, const uint8_t *frame, size_t len,
                                    int *out_errno) {
    tun_loop_t *loop = ctx;
    *out_errno = 0;
    switch (tun_loop_write_frame(loop, frame, len)) {
    case UTUN_DEVICE_OK:
        return TUN_STACK_WRITE_DONE;
    case UTUN_DEVICE_AGAIN:
        return TUN_STACK_WRITE_BLOCKED;
    case UTUN_DEVICE_ERR_SHORT:
        return TUN_STACK_WRITE_SHORT;
    default:
        *out_errno = loop->device_errno;
        return TUN_STACK_WRITE_FAILED;
    }
}

static int64_t loop_now(void *ctx) {
    (void)ctx;
    return senko_now_ms();
}

static void loop_close_device(void *ctx) {
    tun_loop_t *loop = ctx;
    if (loop->device_closed) return;
    loop->device_closed = 1;
    utun_device_close(loop->device);
}

/* ---- lifecycle --------------------------------------------------------- */

tun_loop_status_t tun_loop_init(tun_loop_t *loop, const tun_loop_config_t *config,
                                utun_device_t *device) {
    if (!loop || !config || !device || device->fd < 0) return TUN_LOOP_ERR_ARG;
    memset(loop, 0, loop_part());
    loop->config = *config;
    loop->device = device;
    loop->stack = (tun_stack_t *)((uint8_t *)loop + loop_part());
    loop->wake[0] = loop->wake[1] = -1;
    loop->capacity = config->command_capacity ? config->command_capacity
                                              : TUN_LOOP_COMMAND_MAX;
    if (loop->capacity > TUN_LOOP_COMMAND_MAX) loop->capacity = TUN_LOOP_COMMAND_MAX;

    /* select() cannot watch a descriptor past FD_SETSIZE, and FD_SET on one
       would write outside the set */
    if (device->fd >= FD_SETSIZE) {
        loop_close_device(loop);
        return TUN_LOOP_ERR_ARG;
    }
    if (pthread_mutex_init(&loop->lock, NULL) != 0) {
        loop_close_device(loop);
        return TUN_LOOP_ERR_SYSTEM;
    }
    loop->lock_ready = 1;
    if (pipe(loop->wake) != 0 || set_flags(loop->wake[0]) != 0 ||
        set_flags(loop->wake[1]) != 0 || loop->wake[0] >= FD_SETSIZE) {
        tun_loop_destroy(loop);
        return TUN_LOOP_ERR_SYSTEM;
    }
    loop->accepting = 1;
    return TUN_LOOP_OK;
}

tun_loop_status_t tun_loop_post(tun_loop_t *loop, const tun_loop_command_t *command) {
    if (!loop || !command || !loop->lock_ready) return TUN_LOOP_ERR_ARG;
    pthread_mutex_lock(&loop->lock);
    tun_loop_status_t status = TUN_LOOP_OK;
    if (!loop->accepting) {
        status = TUN_LOOP_ERR_STOPPED;
    } else if (command->generation != loop->config.generation) {
        status = TUN_LOOP_ERR_STALE;
    } else if (loop->queue_count >= loop->capacity) {
        status = TUN_LOOP_ERR_FULL;
    } else {
        size_t index = (loop->queue_head + loop->queue_count) % TUN_LOOP_COMMAND_MAX;
        loop->queue[index] = *command;
        ++loop->queue_count;
    }
    pthread_mutex_unlock(&loop->lock);

    /* a full pipe already means the loop has a wakeup waiting, so a refused
       byte here loses nothing */
    if (status == TUN_LOOP_OK) {
        ssize_t wrote;
        do {
            wrote = write(loop->wake[1], "c", 1);
        } while (wrote < 0 && errno == EINTR);
    }
    return status;
}

void tun_loop_request_stop(tun_loop_t *loop) {
    if (!loop || !loop->lock_ready) return;
    pthread_mutex_lock(&loop->lock);
    loop->stopping = 1;
    loop->accepting = 0;
    pthread_mutex_unlock(&loop->lock);
    ssize_t wrote;
    do {
        wrote = write(loop->wake[1], "s", 1);
    } while (wrote < 0 && errno == EINTR);
}

static void drain_wake(tun_loop_t *loop) {
    char sink[64];
    ssize_t got;
    do {
        got = read(loop->wake[0], sink, sizeof sink);
    } while (got > 0 || (got < 0 && errno == EINTR));
}

/* the commands are taken out under the lock and run without it, so an owner
   callback that posts again cannot deadlock the loop */
static int run_commands(tun_loop_t *loop) {
    tun_loop_command_t taken[TUN_LOOP_COMMAND_MAX];
    size_t count = 0;
    pthread_mutex_lock(&loop->lock);
    int stopping = loop->stopping;
    if (stopping) {
        pthread_mutex_unlock(&loop->lock);
        return 1;
    }
    while (loop->queue_count > 0) {
        taken[count++] = loop->queue[loop->queue_head];
        loop->queue_head = (loop->queue_head + 1u) % TUN_LOOP_COMMAND_MAX;
        --loop->queue_count;
    }
    pthread_mutex_unlock(&loop->lock);

    for (size_t i = 0; i < count; ++i) {
        const tun_loop_command_t *command = &taken[i];
        if (command->generation != loop->config.generation) continue;
        ++loop->commands_run;
        if (command->type == TUN_LOOP_CMD_STOP) return 1;
        if (command->type >= TUN_LOOP_CMD_OWNER && loop->config.on_command)
            loop->config.on_command(loop->config.command_ctx, loop->stack, command);
    }
    return 0;
}

static void finish(tun_loop_t *loop, tun_loop_result_t *result, tun_loop_end_t end,
                   const char *message, int stack_started) {
    /* commands first, then packets: nothing new may arrive while the stack is
       being taken apart */
    pthread_mutex_lock(&loop->lock);
    loop->accepting = 0;
    uint64_t dropped = loop->queue_count;
    loop->queue_count = 0;
    pthread_mutex_unlock(&loop->lock);

    /* the owner lets go of its servers before the stack aborts the flows they
       serve, so no transport outlives the flow it belonged to */
    if (stack_started && loop->config.hooks.stop)
        loop->config.hooks.stop(loop->config.hooks.ctx);
    tun_stack_shutdown(loop->stack);
    if (!loop->config.retain_device_until_destroy) loop_close_device(loop);

    if (result) {
        result->end = end;
        result->device_errno = loop->device_errno;
        result->commands_run = loop->commands_run;
        result->commands_dropped = dropped;
        snprintf(result->message, sizeof result->message, "%s", message);
    }
}

tun_loop_status_t tun_loop_run(tun_loop_t *loop, tun_loop_result_t *out_result) {
    if (out_result) memset(out_result, 0, sizeof *out_result);
    if (!loop || !loop->lock_ready || loop->ran) return TUN_LOOP_ERR_ARG;
    loop->ran = 1;

    tun_stack_io_t io;
    memset(&io, 0, sizeof io);
    io.write_frame = loop_write;
    io.now_ms = loop_now;
    io.close_device = loop->config.retain_device_until_destroy ? NULL : loop_close_device;
    io.ctx = loop;

    char message[256];
    if (tun_stack_init(loop->stack, &loop->config.stack, &io) != TUN_STACK_OK) {
        snprintf(message, sizeof message, "the ip stack did not start: %s",
                 tun_stack_last_error(loop->stack));
        finish(loop, out_result, TUN_LOOP_ENDED_START_FAILED, message, 0);
        return TUN_LOOP_ERR_STACK;
    }
    const tun_loop_hooks_t *hooks = &loop->config.hooks;
    if (hooks->start) {
        char owner_error[192] = "";
        if (hooks->start(hooks->ctx, loop->stack, owner_error,
                         sizeof owner_error) != 0) {
            snprintf(message, sizeof message, "the tunnel flow owner did not start: %s",
                     owner_error[0] ? owner_error : "no reason was given");
            finish(loop, out_result, TUN_LOOP_ENDED_START_FAILED, message, 1);
            return TUN_LOOP_ERR_OWNER;
        }
    }

    if (loop->config.on_ready) loop->config.on_ready(loop->config.ready_ctx);

    int device_fd = loop->device->fd;
    tun_loop_end_t end = TUN_LOOP_ENDED_STOP;

    for (;;) {
        if (run_commands(loop)) {
            end = TUN_LOOP_ENDED_STOP;
            break;
        }

        int pending = tun_stack_has_pending(loop->stack);
        int nobufs = pending && loop->device->last_block_nobufs;
        uint32_t wait_ms = tun_stack_timer_wait_ms(loop->stack);
        if (nobufs && wait_ms > TUN_LOOP_NOBUFS_RETRY_MS) wait_ms = TUN_LOOP_NOBUFS_RETRY_MS;

        fd_set readable, writable;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        FD_SET(device_fd, &readable);
        FD_SET(loop->wake[0], &readable);
        if (pending && !nobufs) FD_SET(device_fd, &writable);
        int highest = device_fd > loop->wake[0] ? device_fd : loop->wake[0];
        if (hooks->prepare) hooks->prepare(hooks->ctx, &readable, &writable, &highest, &wait_ms);

        struct timeval timeout;
        timeout.tv_sec = (time_t)(wait_ms / 1000u);
        timeout.tv_usec = (suseconds_t)((wait_ms % 1000u) * 1000u);
        int ready = select(highest + 1, &readable, &writable, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            loop->device_errno = errno;
            end = TUN_LOOP_ENDED_DEVICE_ERROR;
            break;
        }

        if (ready > 0 && FD_ISSET(loop->wake[0], &readable)) drain_wake(loop);

        if (ready > 0 && FD_ISSET(device_fd, &readable)) {
            for (int i = 0; i < TUN_LOOP_READ_BATCH; ++i) {
                size_t len = 0;
                pthread_mutex_lock(&loop->lock);
                utun_device_status_t status = utun_device_read_frame(loop->device,
                    loop->frame, sizeof loop->frame, &len);
                int saved_errno = loop->device->last_errno;
                pthread_mutex_unlock(&loop->lock);
                if (status == UTUN_DEVICE_AGAIN) break;
                if (status == UTUN_DEVICE_EOF) {
                    end = TUN_LOOP_ENDED_DEVICE_EOF;
                    goto done;
                }
                if (status != UTUN_DEVICE_OK) {
                    loop->device_errno = saved_errno;
                    end = TUN_LOOP_ENDED_DEVICE_ERROR;
                    goto done;
                }
                /* a bad frame is counted and explained by the stack; one bad
                   packet is no reason to stop the tunnel */
                if (loop->config.frame_filter &&
                    loop->config.frame_filter(loop->config.filter_ctx, loop->frame, len))
                    continue;
                (void)tun_stack_input_frame(loop->stack, loop->frame, len);
                if (loop->device_failed) break;
            }
        }

        if (tun_stack_has_pending(loop->stack) &&
            (nobufs || (ready > 0 && FD_ISSET(device_fd, &writable))))
            (void)tun_stack_on_writable(loop->stack);

        if (loop->device_failed) {
            end = TUN_LOOP_ENDED_DEVICE_ERROR;
            break;
        }
        if (hooks->dispatch) {
            /* a timeout leaves the sets as they were; nothing in them is ready */
            if (ready <= 0) {
                FD_ZERO(&readable);
                FD_ZERO(&writable);
            }
            hooks->dispatch(hooks->ctx, &readable, &writable);
        }
        tun_stack_run_timers(loop->stack);
    }

done:
    switch (end) {
    case TUN_LOOP_ENDED_STOP:
        snprintf(message, sizeof message, "the tunnel stopped because it was asked to");
        break;
    case TUN_LOOP_ENDED_DEVICE_EOF:
        snprintf(message, sizeof message, "the tunnel device was closed from the"
                 " other side, so no more packets can arrive");
        break;
    case TUN_LOOP_ENDED_DEVICE_ERROR:
    default:
        snprintf(message, sizeof message, "the tunnel device failed, so the tunnel"
                 " stopped rather than drop every packet (errno %d, %s)",
                 loop->device_errno, strerror(loop->device_errno));
        break;
    }
    finish(loop, out_result, end, message, 1);
    return TUN_LOOP_OK;
}

void tun_loop_destroy(tun_loop_t *loop) {
    if (!loop) return;
    if (loop->lock_ready) {
        pthread_mutex_lock(&loop->lock);
        loop->accepting = 0;
        loop->queue_count = 0;
        pthread_mutex_unlock(&loop->lock);
    }
    if (!loop->ran || loop->config.retain_device_until_destroy)
        loop_close_device(loop);
    for (int i = 0; i < 2; ++i) {
        if (loop->wake[i] >= 0) close(loop->wake[i]);
        loop->wake[i] = -1;
    }
    if (loop->lock_ready) {
        pthread_mutex_destroy(&loop->lock);
        loop->lock_ready = 0;
    }
}
