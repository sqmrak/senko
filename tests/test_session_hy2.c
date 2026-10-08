/* the hysteria2 branch of the session layer over a scripted server: the
   request that opens the stream, a response header split at every byte, a
   refusal, and application bytes that follow the header in the same read */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_transport.h"
#include "session.h"

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static void domain_dest(vless_dest_t *d) {
    memset(d, 0, sizeof *d);
    d->atyp = VLESS_ADDR_DOMAIN;
    snprintf(d->domain, sizeof d->domain, "example.com");
    d->port = 443;
}

static void start(session_t *s, fake_tp_t *tp) {
    vless_dest_t dest;
    domain_dest(&dest);
    fake_tp_reset(tp);
    ok("init", session_init(s, &fake_transport, tp, VL_PROTO_HYSTERIA2, NULL, NULL, NULL,
                            "secret") == SESS_OK);
    ok("start", session_start_from_transparent_dest(s, &dest, NULL, 0, NULL) == SESS_OK);
}

static void test_request_and_response(void) {
    static const uint8_t want[] = { 0x44, 0x01, 0x0f, 'e','x','a','m','p','l','e','.','c','o','m',
                                    ':','4','4','3', 0x00 };
    static const uint8_t server[] = { 0x00, 0x02, 'o', 'k', 0x03, 'x', 'y', 'z', 'H', 'E', 'L', 'L', 'O' };
    session_t *s = calloc(1, sizeof *s);
    fake_tp_t *tp = calloc(1, sizeof *tp);

    start(s, tp);
    ok("waits for the response", s->state == SESS_VLESS_RESP);
    session_pump_remote(s);
    ok("request written", tp->out_len == sizeof want && memcmp(tp->out, want, sizeof want) == 0);

    /* the header reaches the session in two reads, then the payload follows */
    fake_tp_queue_server_bytes(tp, server, 3);
    ok("first part", session_pump_remote(s) == SESS_OK && s->state == SESS_VLESS_RESP);
    fake_tp_queue_server_bytes(tp, server + 3, sizeof server - 3);
    ok("second part", session_pump_remote(s) == SESS_OK && s->state == SESS_RELAY);
    uint8_t got[16];
    size_t n = session_take_client(s, got, sizeof got);
    ok("payload after the header", n == 5 && memcmp(got, "HELLO", 5) == 0);

    /* app bytes sent before the response are held until it says yes */
    start(s, tp);
    const uint8_t early[] = "GET /";
    size_t used = 0;
    ok("early bytes taken", session_feed_client(s, early, sizeof early - 1, &used) == SESS_OK);
    session_pump_remote(s);
    fake_tp_queue_server_bytes(tp, (const uint8_t *)"\x00\x00\x00", 3);
    session_pump_remote(s);
    ok("relay after a bare ok", s->state == SESS_RELAY);
    free(s);
    free(tp);
}

static void test_refusal(void) {
    static const uint8_t refused[] = { 0x01, 0x04, 'd', 'e', 'n', 'y', 0x00 };
    session_t *s = calloc(1, sizeof *s);
    fake_tp_t *tp = calloc(1, sizeof *tp);
    start(s, tp);
    session_pump_remote(s);
    fake_tp_queue_server_bytes(tp, refused, sizeof refused);
    ok("a refusal fails the session", session_pump_remote(s) == SESS_ERR && s->state == SESS_ERROR);

    /* a length over the cap is a malformed header, not something to wait for */
    start(s, tp);
    session_pump_remote(s);
    fake_tp_queue_server_bytes(tp, (const uint8_t *)"\x00\x48\x01", 3);
    ok("an oversized message fails", session_pump_remote(s) == SESS_ERR);

    /* the server ends the stream before it answered */
    start(s, tp);
    session_pump_remote(s);
    tp->eof = 1;
    ok("eof before the response fails", session_pump_remote(s) == SESS_ERR);
    free(s);
    free(tp);
}

int main(void) {
    test_request_and_response();
    test_refusal();
    if (failures) return 1;
    puts("hy2 session ok");
    return 0;
}
