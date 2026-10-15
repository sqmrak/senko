#include "route_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const char *route_socket_status_name(route_socket_status_t status) {
    switch (status) {
    case ROUTE_SOCKET_OK:           return "ok";
    case ROUTE_SOCKET_ALREADY_GONE: return "already-gone";
    case ROUTE_SOCKET_ERR_ARG:      return "arg";
    case ROUTE_SOCKET_ERR_BUILD:    return "build";
    case ROUTE_SOCKET_ERR_WRITE:    return "write";
    case ROUTE_SOCKET_ERR_READ:     return "read";
    case ROUTE_SOCKET_ERR_TIMEOUT:  return "timeout";
    case ROUTE_SOCKET_ERR_REPLY:    return "reply";
    case ROUTE_SOCKET_ERR_EXISTS:   return "exists";
    case ROUTE_SOCKET_ERR_KERNEL:   return "kernel";
    }
    return "unknown";
}

const char *route_errno_name(int value) {
    /* compared by name so the armv7, arm64 and host builds each get their own
       platform's numbers right */
    switch (value) {
    case 0:             return "0";
    case EPERM:         return "EPERM";
    case ESRCH:         return "ESRCH";
    case EINTR:         return "EINTR";
    case EIO:           return "EIO";
    case EBADF:         return "EBADF";
    case EAGAIN:        return "EAGAIN";
    case EACCES:        return "EACCES";
    case EFAULT:        return "EFAULT";
    case EBUSY:         return "EBUSY";
    case EEXIST:        return "EEXIST";
    case EINVAL:        return "EINVAL";
    case EAFNOSUPPORT:  return "EAFNOSUPPORT";
    case EADDRINUSE:    return "EADDRINUSE";
    case ENETDOWN:      return "ENETDOWN";
    case ENETUNREACH:   return "ENETUNREACH";
    case ENOBUFS:       return "ENOBUFS";
    case EHOSTUNREACH:  return "EHOSTUNREACH";
    case ENOTSUP:       return "ENOTSUP";
    case ENOENT:        return "ENOENT";
    case ENXIO:         return "ENXIO";
    case EMFILE:        return "EMFILE";
    case ENFILE:        return "ENFILE";
    case ECONNREFUSED:  return "ECONNREFUSED";
    case ECONNRESET:    return "ECONNRESET";
    case ETIMEDOUT:     return "ETIMEDOUT";
    case EHOSTDOWN:     return "EHOSTDOWN";
    case EADDRNOTAVAIL: return "EADDRNOTAVAIL";
    default:            break;
    }
    return "";
}

void route_errno_append(char *out, size_t cap, int value) {
    const char *name = route_errno_name(value);
    size_t used = strlen(out);
    if (used >= cap) return;
    if (name[0])
        snprintf(out + used, cap - used, " (errno %d %s)", value, name);
    else
        snprintf(out + used, cap - used, " (errno %d)", value);
}

static const char *verb_for(route_message_type_t type) {
    switch (type) {
    case ROUTE_MESSAGE_ADD:    return "add";
    case ROUTE_MESSAGE_DELETE: return "delete";
    case ROUTE_MESSAGE_GET:    return "look up";
    }
    return "send";
}

void route_socket_describe(const route_message_spec_t *spec, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!spec) return;

    char destination[INET6_ADDRSTRLEN] = "?";
    int family = spec->address_len == 4 ? AF_INET : AF_INET6;
    inet_ntop(family, spec->destination, destination, sizeof destination);

    if (spec->type == ROUTE_MESSAGE_GET) {
        snprintf(out, cap, "%s", destination);
        return;
    }
    if (spec->target == ROUTE_TARGET_INTERFACE) {
        snprintf(out, cap, "%s/%u via interface %u", destination, spec->prefix,
                 spec->interface_index);
        return;
    }
    char gateway[INET6_ADDRSTRLEN] = "?";
    inet_ntop(family, spec->gateway, gateway, sizeof gateway);
    snprintf(out, cap, "%s/%u via %s", destination, spec->prefix, gateway);
}

/* why the kernel refused, in the words a person would use. the exact code is
   appended separately so the sentence keeps both */
static const char *kernel_reason(int value, route_message_type_t type) {
    if (value == EEXIST)
        return type == ROUTE_MESSAGE_ADD
            ? "a route for this destination already exists and senkod will not"
              " replace a route it did not create"
            : "the kernel reported the route already exists";
    if (value == ESRCH) return "the kernel has no such route";
    if (value == ENETUNREACH)
        return "the gateway is not on any network this device can reach right now";
    if (value == EHOSTUNREACH) return "the gateway did not answer and looks unreachable";
    if (value == ENETDOWN) return "the interface for this route is down";
    if (value == EPERM || value == EACCES)
        return "changing routes is not permitted, senkod has to run as root";
    if (value == EINVAL)
        return "the kernel rejected the message as malformed, which points at the"
               " address or prefix in it";
    if (value == EAFNOSUPPORT)
        return "this kernel does not support that address family on a route";
    if (value == EBUSY) return "the route is in use and cannot be changed right now";
    if (value == ENOBUFS) return "the kernel ran out of memory for the routing table";
    return "the kernel refused it";
}

static const char *build_reason(route_message_status_t status) {
    switch (status) {
    case ROUTE_MESSAGE_ERR_ARG:
        return "the address, prefix or interface in it is not valid";
    case ROUTE_MESSAGE_ERR_SPACE:
        return "the message does not fit the send buffer";
    default:
        break;
    }
    return "it could not be built";
}

static const char *reply_reason(route_message_status_t status) {
    switch (status) {
    case ROUTE_MESSAGE_ERR_TRUNCATED:
        return "the kernel reply was cut short";
    case ROUTE_MESSAGE_ERR_VERSION:
        return "the kernel reply uses a routing message version senkod does not read";
    default:
        break;
    }
    return "the kernel reply could not be read";
}

int32_t route_socket_next_seq(void) {
    /* one owner thread drives the routing socket, so a plain counter is
       enough. it wraps before the increment rather than after, because
       overflowing a signed int is undefined, and it never returns zero, which
       keeps a zeroed reply from looking like an answer to a real request */
    static int32_t seq;
    if (seq >= INT32_MAX) seq = 0;
    return ++seq;
}

static void set_error(route_error_t *error, route_socket_status_t status,
                      int kernel_errno, const char *text) {
    if (!error) return;
    error->status = status;
    error->kernel_errno = kernel_errno;
    snprintf(error->message, sizeof error->message, "%s", text ? text : "");
}

route_socket_status_t route_socket_transact(const route_io_t *io,
                                            const route_message_spec_t *spec,
                                            int64_t timeout_ms,
                                            route_message_reply_t *out_reply,
                                            route_error_t *out_error) {
    if (out_reply) memset(out_reply, 0, sizeof *out_reply);
    if (out_error) {
        memset(out_error, 0, sizeof *out_error);
        out_error->status = ROUTE_SOCKET_ERR_ARG;
    }
    if (!io || !io->write_message || !io->read_message || !io->now_ms || !spec) {
        set_error(out_error, ROUTE_SOCKET_ERR_ARG,
                  0, "route request was made without a usable routing socket");
        return ROUTE_SOCKET_ERR_ARG;
    }
    if (timeout_ms <= 0) timeout_ms = ROUTE_SOCKET_DEFAULT_TIMEOUT_MS;

    route_message_spec_t request = *spec;
    request.pid = io->pid;
    request.seq = route_socket_next_seq();

    char what[112]; /* two ipv6 addresses, a prefix and the word between them */
    route_socket_describe(&request, what, sizeof what);
    const char *verb = verb_for(request.type);
    char text[ROUTE_ERROR_MAX];

    uint8_t message[ROUTE_MESSAGE_MAX];
    size_t message_len = 0;
    route_message_status_t built = route_message_build(&request, message,
                                                       sizeof message, &message_len);
    if (built != ROUTE_MESSAGE_OK) {
        snprintf(text, sizeof text, "cannot %s route %s: %s", verb, what,
                 build_reason(built));
        set_error(out_error, ROUTE_SOCKET_ERR_BUILD, 0, text);
        return ROUTE_SOCKET_ERR_BUILD;
    }

    long wrote;
    int io_errno = 0;
    do {
        io_errno = 0;
        wrote = io->write_message(io->ctx, message, message_len, &io_errno);
    } while (wrote < 0 && io_errno == EINTR);

    /* darwin reports the kernel's verdict on the write itself, before any
       reply is read, so a delete of a missing route ends here */
    if (wrote < 0 && io_errno == ESRCH && request.type == ROUTE_MESSAGE_DELETE) {
        snprintf(text, sizeof text, "route %s was already gone", what);
        route_errno_append(text, sizeof text, io_errno);
        set_error(out_error, ROUTE_SOCKET_ALREADY_GONE, io_errno, text);
        return ROUTE_SOCKET_ALREADY_GONE;
    }
    if (wrote < 0) {
        /* darwin answers a lookup with no matching route by failing the write
           itself, so this is a missing route rather than a socket fault */
        int missing = request.type == ROUTE_MESSAGE_GET &&
                      (io_errno == ESRCH || io_errno == ENETUNREACH ||
                       io_errno == EHOSTUNREACH);
        snprintf(text, sizeof text, "cannot %s route %s: %s", verb, what,
                 missing ? "the device has no route to it, wifi or cellular is"
                           " down or another VPN took the default route"
                         : "writing to the routing socket failed");
        route_errno_append(text, sizeof text, io_errno);
        set_error(out_error, ROUTE_SOCKET_ERR_WRITE, io_errno, text);
        return ROUTE_SOCKET_ERR_WRITE;
    }
    if ((size_t)wrote != message_len) {
        /* a half written routing message is not a request the kernel can act
           on, and resending would duplicate the part that did land */
        snprintf(text, sizeof text, "cannot %s route %s: the routing socket took"
                 " only %ld of %zu bytes", verb, what, wrote, message_len);
        set_error(out_error, ROUTE_SOCKET_ERR_WRITE, 0, text);
        return ROUTE_SOCKET_ERR_WRITE;
    }

    int64_t deadline = io->now_ms(io->ctx) + timeout_ms;
    for (;;) {
        int64_t now = io->now_ms(io->ctx);
        if (now >= deadline) {
            snprintf(text, sizeof text, "cannot %s route %s: the kernel sent no"
                     " reply within %lld ms", verb, what, (long long)timeout_ms);
            set_error(out_error, ROUTE_SOCKET_ERR_TIMEOUT, 0, text);
            return ROUTE_SOCKET_ERR_TIMEOUT;
        }

        uint8_t reply[ROUTE_REPLY_MAX];
        io_errno = 0;
        long got = io->read_message(io->ctx, reply, sizeof reply, deadline, &io_errno);
        if (got < 0) {
            /* a signal is not a failure of the request, the deadline decides
               how long this keeps going */
            if (io_errno == EINTR) continue;
            snprintf(text, sizeof text, "cannot %s route %s: reading the reply"
                     " from the routing socket failed", verb, what);
            route_errno_append(text, sizeof text, io_errno);
            set_error(out_error, ROUTE_SOCKET_ERR_READ, io_errno, text);
            return ROUTE_SOCKET_ERR_READ;
        }
        if (got == 0) continue; /* nothing yet, the deadline check owns the wait */

        route_message_reply_t parsed;
        route_message_status_t status = route_message_parse(reply, (size_t)got,
            request.pid, request.seq, &parsed);

        /* every writer on the routing socket sees every other writer's
           traffic, so someone else's message is skipped, not an error */
        if (status == ROUTE_MESSAGE_ERR_FOREIGN) continue;

        if (status == ROUTE_MESSAGE_ERR_TRUNCATED ||
            status == ROUTE_MESSAGE_ERR_VERSION) {
            snprintf(text, sizeof text, "cannot %s route %s: %s", verb, what,
                     reply_reason(status));
            set_error(out_error, ROUTE_SOCKET_ERR_REPLY, 0, text);
            return ROUTE_SOCKET_ERR_REPLY;
        }

        if (out_reply) *out_reply = parsed;

        if (status == ROUTE_MESSAGE_ERR_KERNEL) {
            int value = parsed.kernel_errno;
            /* deleting a route that is already gone leaves the table in the
               state the caller asked for */
            if (value == ESRCH && request.type == ROUTE_MESSAGE_DELETE) {
                snprintf(text, sizeof text, "route %s was already gone", what);
                route_errno_append(text, sizeof text, value);
                set_error(out_error, ROUTE_SOCKET_ALREADY_GONE, value, text);
                return ROUTE_SOCKET_ALREADY_GONE;
            }
            route_socket_status_t mapped = value == EEXIST &&
                request.type == ROUTE_MESSAGE_ADD
                ? ROUTE_SOCKET_ERR_EXISTS : ROUTE_SOCKET_ERR_KERNEL;
            snprintf(text, sizeof text, "cannot %s route %s: %s", verb, what,
                     kernel_reason(value, request.type));
            route_errno_append(text, sizeof text, value);
            set_error(out_error, mapped, value, text);
            return mapped;
        }

        if (status != ROUTE_MESSAGE_OK) {
            snprintf(text, sizeof text, "cannot %s route %s: %s", verb, what,
                     reply_reason(status));
            set_error(out_error, ROUTE_SOCKET_ERR_REPLY, 0, text);
            return ROUTE_SOCKET_ERR_REPLY;
        }

        if (out_error) {
            out_error->status = ROUTE_SOCKET_OK;
            out_error->kernel_errno = 0;
            out_error->message[0] = '\0';
        }
        return ROUTE_SOCKET_OK;
    }
}
