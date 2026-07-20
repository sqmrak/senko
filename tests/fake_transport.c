#include "fake_transport.h"

#include <string.h>

void fake_tp_reset(fake_tp_t *f) {
    memset(f, 0, sizeof *f);
}

void fake_tp_queue_server_bytes(fake_tp_t *f, const uint8_t *b, size_t n) {
    if (f->in_len + n > sizeof f->in) n = sizeof f->in - f->in_len;
    memcpy(f->in + f->in_len, b, n);
    f->in_len += n;
}

/* accept the vtable fd but use the test-owned fake handle instead */
static void *fake_open(int fd, const transport_tls_cfg_t *cfg) {
    (void)fd; (void)cfg;
    return (void *)0;   /* unused: tests pass the handle directly */
}

static int fake_read(void *h, uint8_t *buf, size_t len) {
    fake_tp_t *f = (fake_tp_t *)h;
    size_t avail = f->in_len - f->in_off;
    if (avail == 0) {
        return f->eof ? TRANSPORT_EOF : TRANSPORT_WANT_READ;
    }
    size_t take = avail < len ? avail : len;
    if (f->read_chunk && take > f->read_chunk) take = f->read_chunk;
    memcpy(buf, f->in + f->in_off, take);
    f->in_off += take;
    return (int)take;
}

static int fake_write(void *h, const uint8_t *buf, size_t len) {
    fake_tp_t *f = (fake_tp_t *)h;
    if (f->stall_writes > 0) {          /* deterministic backpressure */
        f->stall_writes--;
        return TRANSPORT_WANT_WRITE;
    }
    size_t take = len;
    if (f->write_chunk && take > f->write_chunk) take = f->write_chunk;
    if (f->out_len + take > sizeof f->out) take = sizeof f->out - f->out_len;
    memcpy(f->out + f->out_len, buf, take);
    f->out_len += take;
    return (int)take;
}

static int fake_raw_write(void *h, const uint8_t *buf, size_t len) {
    fake_tp_t *f = (fake_tp_t *)h;
    if (f->stall_writes > 0) {
        f->stall_writes--;
        return TRANSPORT_WANT_WRITE;
    }
    size_t take = len;
    if (f->write_chunk && take > f->write_chunk) take = f->write_chunk;
    if (f->raw_out_len + take > sizeof f->raw_out) take = sizeof f->raw_out - f->raw_out_len;
    memcpy(f->raw_out + f->raw_out_len, buf, take);
    f->raw_out_len += take;
    return (int)take;
}

static void fake_close(void *h) { (void)h; }

const transport_vt_t fake_transport = {
    fake_open, fake_read, fake_write, fake_raw_write, fake_close, NULL
};
