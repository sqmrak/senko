#include "route_socket.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

/* an error message is only useful if it names the operation, says what went
   wrong in words, and still carries the exact code */
static void ok_message(const char *name, const route_error_t *error,
                       const char *needle, int want_errno) {
    int good = strstr(error->message, needle) != NULL &&
        error->kernel_errno == want_errno;
    if (want_errno != 0) {
        char code[32];
        snprintf(code, sizeof code, "errno %d", want_errno);
        if (!strstr(error->message, code)) good = 0;
    }
    if (!good) {
        ++failures;
        fprintf(stderr, "FAIL %s\n  message: %s\n", name, error->message);
    }
}

#define SCRIPT_MAX 8

typedef struct {
    long    result;   /* bytes, 0 for nothing to read, -1 for an error */
    int     error;    /* errno when result is -1 */
    int     foreign;  /* reply carries another writer's pid and sequence */
    int     kernel_errno;
    int     truncate; /* reply is cut to this many bytes, 0 for full */
    int     version;  /* routing version to claim, 0 for the right one */
} scripted_read_t;

typedef struct {
    uint8_t  sent[ROUTE_MESSAGE_MAX];
    size_t   sent_len;
    long     write_result;  /* -1 to fail, 0 to use the real length, or a short count */
    int      write_errno;
    int      write_calls;
    int      write_eintr_once;

    scripted_read_t reads[SCRIPT_MAX];
    size_t   read_count;
    size_t   read_next;
    int      read_calls;

    int64_t  now;
    int64_t  step;       /* how far the clock moves on each read */
    int32_t  pid;
} fake_socket_t;

static void fake_init(fake_socket_t *fake) {
    memset(fake, 0, sizeof *fake);
    fake->pid = 4242;
    fake->step = 10;
}

static long fake_write(void *ctx, const uint8_t *data, size_t len, int *out_errno) {
    fake_socket_t *fake = ctx;
    ++fake->write_calls;
    *out_errno = 0;

    /* a signal during the write must be retried, not reported */
    if (fake->write_eintr_once) {
        fake->write_eintr_once = 0;
        *out_errno = EINTR;
        return -1;
    }
    if (fake->write_errno) {
        *out_errno = fake->write_errno;
        return -1;
    }
    if (len > sizeof fake->sent) return -1;
    memcpy(fake->sent, data, len);
    fake->sent_len = len;
    return fake->write_result ? fake->write_result : (long)len;
}

/* build the reply the kernel would send for what was just written */
static long fake_reply(fake_socket_t *fake, const scripted_read_t *script,
                       uint8_t *buf, size_t cap) {
    if (fake->sent_len == 0 || fake->sent_len > cap) return -1;
    memcpy(buf, fake->sent, fake->sent_len);
    size_t len = fake->sent_len;

    if (script->foreign) {
        int32_t other = 9999;
        memcpy(buf + 16, &other, sizeof other); /* another writer's pid */
    }
    if (script->kernel_errno) {
        int32_t value = script->kernel_errno;
        memcpy(buf + 24, &value, sizeof value);
    }
    if (script->version) buf[2] = (uint8_t)script->version;
    if (script->truncate > 0 && (size_t)script->truncate < len)
        len = (size_t)script->truncate;
    return (long)len;
}

static long fake_read(void *ctx, uint8_t *buf, size_t cap, int64_t deadline_ms,
                      int *out_errno) {
    fake_socket_t *fake = ctx;
    ++fake->read_calls;
    *out_errno = 0;
    fake->now += fake->step;

    if (fake->read_next >= fake->read_count) {
        /* nothing scripted left: behave like a quiet socket so the deadline
           is what ends the wait */
        if (fake->now < deadline_ms) fake->now = deadline_ms;
        return 0;
    }
    const scripted_read_t *script = &fake->reads[fake->read_next++];
    if (script->result < 0) {
        *out_errno = script->error;
        return -1;
    }
    if (script->result == 0) return 0;
    return fake_reply(fake, script, buf, cap);
}

static int64_t fake_now(void *ctx) {
    fake_socket_t *fake = ctx;
    return fake->now;
}

static route_io_t io_for(fake_socket_t *fake) {
    route_io_t io;
    memset(&io, 0, sizeof io);
    io.write_message = fake_write;
    io.read_message = fake_read;
    io.now_ms = fake_now;
    io.ctx = fake;
    io.pid = fake->pid;
    return io;
}

static void push_read(fake_socket_t *fake, scripted_read_t script) {
    if (fake->read_count < SCRIPT_MAX) fake->reads[fake->read_count++] = script;
}

static route_message_spec_t pin_spec(route_message_type_t type) {
    route_message_spec_t spec;
    memset(&spec, 0, sizeof spec);
    spec.type = type;
    spec.address_len = 4;
    spec.prefix = 32;
    spec.destination[0] = 203; spec.destination[2] = 113; spec.destination[3] = 7;
    spec.gateway[0] = 192; spec.gateway[1] = 168; spec.gateway[3] = 1;
    spec.target = ROUTE_TARGET_GATEWAY;
    return spec;
}

int main(void) {
    fake_socket_t fake;
    route_message_spec_t spec = pin_spec(ROUTE_MESSAGE_ADD);
    route_message_reply_t reply;
    route_error_t error;

    /* the ordinary case */
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1 });
    route_io_t io = io_for(&fake);
    ok("a normal add succeeds",
       route_socket_transact(&io, &spec, 1000, &reply, &error) == ROUTE_SOCKET_OK);
    ok("a success carries no message", error.message[0] == '\0');
    ok("the request was written once", fake.write_calls == 1);

    /* every request gets its own sequence, so a late reply to an earlier one
       cannot be mistaken for this one */
    int32_t first_seq, second_seq;
    memcpy(&first_seq, fake.sent + 20, sizeof first_seq);
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1 });
    io = io_for(&fake);
    route_socket_transact(&io, &spec, 1000, &reply, &error);
    memcpy(&second_seq, fake.sent + 20, sizeof second_seq);
    ok("each request uses a new sequence", second_seq != first_seq);
    ok("the sequence is never zero", first_seq != 0 && second_seq != 0);

    /* another writer's reply arrives first and must be skipped, not treated
       as an answer or as a failure */
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1, .foreign = 1 });
    push_read(&fake, (scripted_read_t){ .result = 1, .foreign = 1 });
    push_read(&fake, (scripted_read_t){ .result = 1 });
    io = io_for(&fake);
    ok("a foreign reply is skipped and ours is still found",
       route_socket_transact(&io, &spec, 1000, &reply, &error) == ROUTE_SOCKET_OK);
    ok("all three reads were consumed", fake.read_calls == 3);

    /* a signal during the read is retried */
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = -1, .error = EINTR });
    push_read(&fake, (scripted_read_t){ .result = 1 });
    io = io_for(&fake);
    ok("EINTR on read is retried",
       route_socket_transact(&io, &spec, 1000, &reply, &error) == ROUTE_SOCKET_OK);

    /* and a signal during the write */
    fake_init(&fake);
    fake.write_eintr_once = 1;
    push_read(&fake, (scripted_read_t){ .result = 1 });
    io = io_for(&fake);
    ok("EINTR on write is retried",
       route_socket_transact(&io, &spec, 1000, &reply, &error) == ROUTE_SOCKET_OK &&
       fake.write_calls == 2);

    /* a quiet kernel ends at the deadline rather than hanging */
    fake_init(&fake);
    io = io_for(&fake);
    ok("a silent kernel times out",
       route_socket_transact(&io, &spec, 100, &reply, &error) ==
       ROUTE_SOCKET_ERR_TIMEOUT);
    ok_message("the timeout says how long it waited", &error, "no reply within 100 ms", 0);
    ok("the timeout message names the route",
       strstr(error.message, "203.0.113.7/32 via 192.168.0.1") != NULL);

    /* a stream of foreign replies must not extend the wait forever */
    fake_init(&fake);
    fake.step = 40;
    for (int i = 0; i < SCRIPT_MAX; ++i)
        push_read(&fake, (scripted_read_t){ .result = 1, .foreign = 1 });
    io = io_for(&fake);
    ok("foreign replies cannot hold the deadline open",
       route_socket_transact(&io, &spec, 100, &reply, &error) ==
       ROUTE_SOCKET_ERR_TIMEOUT);

    /* adding a route that already exists is an error: the existing one is
       not ours to replace, and a rollback must not delete it */
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1, .kernel_errno = EEXIST });
    io = io_for(&fake);
    ok("EEXIST on add is an error",
       route_socket_transact(&io, &spec, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_EXISTS);
    ok_message("the message explains why an existing route is not replaced",
               &error, "already exists", EEXIST);
    ok("the message says senkod will not touch it",
       strstr(error.message, "did not create") != NULL);

    /* deleting a route that is already gone is the state we wanted */
    route_message_spec_t remove = pin_spec(ROUTE_MESSAGE_DELETE);
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1, .kernel_errno = ESRCH });
    io = io_for(&fake);
    ok("ESRCH on delete is not a failure",
       route_socket_transact(&io, &remove, 1000, &reply, &error) ==
       ROUTE_SOCKET_ALREADY_GONE);
    ok_message("the message says it was already gone", &error, "already gone", ESRCH);

    /* but ESRCH on an add is still a failure */
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1, .kernel_errno = ESRCH });
    io = io_for(&fake);
    ok("ESRCH on add is a failure",
       route_socket_transact(&io, &spec, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_KERNEL);

    /* the kernel refusing for other reasons is passed through with the code */
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1, .kernel_errno = ENETUNREACH });
    io = io_for(&fake);
    ok("an unreachable gateway is reported",
       route_socket_transact(&io, &spec, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_KERNEL);
    ok_message("the message explains an unreachable gateway in words",
               &error, "not on any network", ENETUNREACH);

    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1, .kernel_errno = EPERM });
    io = io_for(&fake);
    route_socket_transact(&io, &spec, 1000, &reply, &error);
    ok_message("a permission failure names root", &error, "run as root", EPERM);

    /* a cut short reply is refused rather than half read */
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1, .truncate = 40 });
    io = io_for(&fake);
    ok("a truncated reply is refused",
       route_socket_transact(&io, &spec, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_REPLY);
    ok_message("the message says the reply was cut short", &error, "cut short", 0);

    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1, .version = 3 });
    io = io_for(&fake);
    ok("a reply of another routing version is refused",
       route_socket_transact(&io, &spec, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_REPLY);

    /* darwin fails the write itself with ESRCH for a missing route, as an
       iphone 4s on ios 5.1.1 did for a 224/4 bypass after a crash */
    fake_init(&fake);
    fake.write_errno = ESRCH;
    io = io_for(&fake);
    ok("ESRCH on the delete write is already gone",
       route_socket_transact(&io, &remove, 1000, &reply, &error) ==
       ROUTE_SOCKET_ALREADY_GONE);
    fake_init(&fake);
    fake.write_errno = ESRCH;
    io = io_for(&fake);
    ok("ESRCH on an add write is still a failure",
       route_socket_transact(&io, &spec, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_WRITE);

    /* write failures */
    fake_init(&fake);
    fake.write_errno = EPERM;
    io = io_for(&fake);
    ok("a refused write is reported",
       route_socket_transact(&io, &spec, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_WRITE);
    ok_message("a refused write names the socket and the code", &error,
               "routing socket", EPERM);

    fake_init(&fake);
    fake.write_result = 10; /* short write */
    push_read(&fake, (scripted_read_t){ .result = 1 });
    io = io_for(&fake);
    ok("a short write is an error, never a retry",
       route_socket_transact(&io, &spec, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_WRITE);
    ok("the short write message says how much went",
       strstr(error.message, "only 10 of") != NULL);

    /* a lookup with no route is a missing route, and says what usually causes
       it instead of blaming the socket */
    route_message_spec_t missing = pin_spec(ROUTE_MESSAGE_GET);
    fake_init(&fake);
    fake.write_errno = ESRCH;
    io = io_for(&fake);
    ok("a lookup with no route fails the write",
       route_socket_transact(&io, &missing, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_WRITE);
    ok("the lookup message names a missing route and another VPN",
       strstr(error.message, "no route to it") != NULL &&
       strstr(error.message, "another VPN") != NULL &&
       error.kernel_errno == ESRCH);

    /* a message that cannot be built says so before anything is sent */
    route_message_spec_t bad = pin_spec(ROUTE_MESSAGE_ADD);
    bad.prefix = 33;
    fake_init(&fake);
    io = io_for(&fake);
    ok("an invalid route is refused before it is sent",
       route_socket_transact(&io, &bad, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_BUILD && fake.write_calls == 0);
    ok_message("the build failure points at the address or prefix", &error,
               "prefix", 0);

    /* a missing io interface must not crash */
    ok("a request without a socket is refused",
       route_socket_transact(NULL, &spec, 1000, &reply, &error) ==
       ROUTE_SOCKET_ERR_ARG);
    ok("the message says the socket is missing",
       strstr(error.message, "routing socket") != NULL);

    /* a lookup reads the interface the kernel would use */
    route_message_spec_t lookup = pin_spec(ROUTE_MESSAGE_GET);
    lookup.want_interface = 1;
    fake_init(&fake);
    push_read(&fake, (scripted_read_t){ .result = 1 });
    io = io_for(&fake);
    ok("a lookup completes",
       route_socket_transact(&io, &lookup, 1000, &reply, &error) == ROUTE_SOCKET_OK);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("all route socket checks passed");
    return 0;
}
