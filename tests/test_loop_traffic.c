#include "../daemon/loop.c"
#include <assert.h>
#include <stdlib.h>

static int unused_dial(void *ctx) {
    (void)ctx;
    abort();
}

int main(void) {
    loop_t *lp = calloc(1, sizeof *lp);
    assert(lp);
    lp->listen_fd = lp->tproxy_fd = lp->wake_rd = lp->wake_wr = -1;
    int local[2], remote[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, local) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, remote) == 0);
    set_nonblock(local[0]);
    set_nonblock(local[1]);
    set_nonblock(remote[0]);
    set_nonblock(remote[1]);
    loop_conn_t *c = &lp->conns[0];
    c->owner = lp;
    c->local_fd = local[0];
    c->remote_fd = remote[0];
    c->used = 1;
    c->open_vt = &transport_tcp;
    c->th = transport_tcp.open(remote[0], NULL);
    assert(session_init(&c->sess, &transport_tcp, c->th, VL_PROTO_HTTP,
                        NULL, NULL, NULL, NULL) == SESS_OK);
    c->sess.state = SESS_RELAY;
    assert(write(local[1], "early", 5) == 5);
    assert(read_local_into_prebuf(c) == 0);
    assert(lp->bytes_up == 5 && c->prebuf_len == 5);
    assert(write(local[1], "upload", 6) == 6);
    service_conn(lp, c, POLLIN, 0);
    char data[8192];
    assert(read(remote[1], data, sizeof data) == 6);
    assert(memcmp(data, "upload", 6) == 0 && lp->bytes_up == 11);
    assert(write(remote[1], "download", 8) == 8);
    service_conn(lp, c, 0, POLLIN);
    assert(read(local[1], data, sizeof data) == 8);
    assert(memcmp(data, "download", 8) == 0 && lp->bytes_down == 8);

    int small = 1024;
    assert(setsockopt(local[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0);
    memset(c->pend, 'x', sizeof c->pend);
    c->pend_len = sizeof c->pend;
    assert(flush_pend_local(c) == 0);
    assert(c->pend_off > 0 && c->pend_off < c->pend_len);
    assert(lp->bytes_down == 8 + c->pend_off);
    uint64_t before = lp->bytes_down;
    assert(flush_to_local(c) == 0 && lp->bytes_down == before);
    for (int i = 0; i < 100 && c->pend_len; ++i) {
        while (read(local[1], data, sizeof data) > 0) {}
        assert(flush_to_local(c) == 0);
    }
    assert(c->pend_len == 0 && lp->bytes_down == 8 + sizeof c->pend);
    drop_conn(lp, c);
    assert(lp->bytes_up == 11 && lp->bytes_down == 8 + sizeof c->pend);
    close(local[1]);
    close(remote[1]);
    assert(loop_set_server(lp, &transport_tcp, unused_dial, NULL, VL_PROTO_HTTP,
                          NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                          NULL, NULL, NULL, NULL, 0) == LOOP_OK);
    assert(lp->bytes_up == 0 && lp->bytes_down == 0);
    lp->bytes_up = UINT64_C(1) << 35;
    lp->bytes_down = 123;
    loop_stop(lp);
    assert(lp->bytes_up == 0 && lp->bytes_down == 0);
    free(lp);
    puts("all loop traffic checks passed");
    return 0;
}
