#include "net_diag.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char text[320];
    net_diag_t d = {0};
    d.stage = NET_DIAG_TRANSPORT;
    d.port = 443;
    d.proto = VL_PROTO_VMESS;
    struct { int error; net_fail_kind_t kind; const char *word; } cases[] = {
        {ECONNREFUSED, NET_FAIL_REFUSED, "port 443"},
        {ENETUNREACH, NET_FAIL_NO_ROUTE, "internet connection"},
        {ETIMEDOUT, NET_FAIL_TIMEOUT, "does not answer"},
        {ECONNRESET, NET_FAIL_RESET, "reset"},
        {EMFILE, NET_FAIL_SYSTEM, "this device"}
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        d.transport.kind = net_fail_from_errno(cases[i].error);
        assert(d.transport.kind == cases[i].kind);
        net_diag_text(&d, text, sizeof text);
        assert(strstr(text, cases[i].word));
    }
    d.transport.kind = NET_FAIL_CERT_NAME;
    net_diag_text(&d, text, sizeof text); assert(strstr(text, "SNI"));
    d.transport.kind = NET_FAIL_CERT_EXPIRED;
    assert(net_diag_wants_clock(&d));
    d.clock_known = 1; d.clock_offset_s = 3600;
    net_diag_text(&d, text, sizeof text); assert(strstr(text, "appears 60 minutes ahead"));
    d.clock_offset_s = -3600;
    net_diag_text(&d, text, sizeof text); assert(strstr(text, "certificate has expired"));
    d.transport.kind = NET_FAIL_CERT_NOT_YET_VALID;
    net_diag_text(&d, text, sizeof text); assert(strstr(text, "behind"));
    d.stage = NET_DIAG_CLOSED; d.clock_known = 0;
    assert(net_diag_wants_clock(&d));
    net_diag_text(&d, text, sizeof text); assert(strstr(text, "UUID and the device clock"));
    d.clock_known = 1; d.clock_offset_s = 120;
    net_diag_text(&d, text, sizeof text); assert(!strstr(text, "appears"));
    d.clock_offset_s = 121;
    net_diag_text(&d, text, sizeof text); assert(strstr(text, "appears"));
    d.stage = NET_DIAG_NOT_PROXIED; d.http_status = 503;
    net_diag_text(&d, text, sizeof text); assert(strstr(text, "HTTP 503") && !strstr(text, "website instead"));
    d.stage = NET_DIAG_ANSWER; d.answer = SESS_FAIL_DECRYPT; d.proto = VL_PROTO_SHADOWSOCKS;
    net_diag_text(&d, text, sizeof text); assert(strstr(text, "password and the cipher"));
    d.answer = SESS_FAIL_LOGIN;
    net_diag_text(&d, text, sizeof text); assert(strstr(text, "refused the user name"));
    net_fail_t f = {0};
    net_fail_note(&f, NET_FAIL_CERT_NAME, 0); net_fail_note(&f, NET_FAIL_CLOSED, 0);
    assert(f.kind == NET_FAIL_CERT_NAME);
    char tiny[3] = {'x', 'x', 'x'};
    net_diag_text(&d, tiny, sizeof tiny); assert(tiny[2] == '\0');
    net_diag_text(&d, NULL, 0);
    net_diag_clock_text(INT64_MIN, text, sizeof text); assert(strstr(text, "days behind"));
    puts("network diagnosis checks passed");
    return 0;
}
