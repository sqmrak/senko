#include "net_diag.h"

#include <stdio.h>

static uint64_t abs64(int64_t v) {
    return v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
}

static int clock_off(const net_diag_t *d) {
    return d->clock_known && abs64(d->clock_offset_s) > NET_DIAG_CLOCK_LIMIT_S;
}

/* what the user has to check when the server turns the request down */
static const char *credential(vl_proto_t proto) {
    switch (proto) {
    case VL_PROTO_VLESS:
    case VL_PROTO_VMESS: return "UUID";
    case VL_PROTO_SHADOWSOCKS: return "password and the cipher";
    case VL_PROTO_SOCKS5:
    case VL_PROTO_HTTP:
    case VL_PROTO_HTTPS: return "user name and password";
    default: return "password";
    }
}

static const char *proto_name(vl_proto_t proto) {
    switch (proto) {
    case VL_PROTO_VLESS: return "VLESS";
    case VL_PROTO_VMESS: return "VMess";
    case VL_PROTO_TROJAN: return "Trojan";
    case VL_PROTO_SHADOWSOCKS: return "Shadowsocks";
    case VL_PROTO_HYSTERIA2: return "Hysteria2";
    case VL_PROTO_SOCKS5: return "SOCKS5";
    default: return "HTTP proxy";
    }
}

void net_diag_clock_text(int64_t offset_s, char *out, size_t cap) {
    const char *way = offset_s > 0 ? "ahead" : "behind";
    uint64_t a = abs64(offset_s);
    if (!out || !cap) return;
    if (a >= 2 * 86400)
        snprintf(out, cap, "the device clock appears %lld days %s compared with the HTTP check: verify automatic date and time",
                 (long long)(a / 86400), way);
    else if (a >= 2 * 3600)
        snprintf(out, cap, "the device clock appears %lld hours %s compared with the HTTP check: verify automatic date and time",
                 (long long)(a / 3600), way);
    else
        snprintf(out, cap, "the device clock appears %lld minutes %s compared with the HTTP check: verify automatic date and time",
                 (long long)((a + 59) / 60), way);
}

int net_diag_wants_clock(const net_diag_t *d) {
    if (!d || d->clock_known) return 0;
    if (d->stage == NET_DIAG_TRANSPORT)
        return d->transport.kind == NET_FAIL_REALITY ||
               d->transport.kind == NET_FAIL_CERT_EXPIRED ||
               d->transport.kind == NET_FAIL_CERT_NOT_YET_VALID;
    return d->proto == VL_PROTO_VMESS &&
           (d->stage == NET_DIAG_CLOSED || d->stage == NET_DIAG_SILENT);
}

static void transport_text(const net_diag_t *d, char *out, size_t cap) {
    int v = d->transport.value;
    switch (d->transport.kind) {
    case NET_FAIL_SYSTEM:
        snprintf(out, cap, "this device could not open the connection: restart Senko and check System Logs");
        return;
    case NET_FAIL_NO_ROUTE:
        snprintf(out, cap, "this device has no network route to the server: check the internet connection");
        return;
    case NET_FAIL_REFUSED:
        snprintf(out, cap, "the server refused the connection on port %u: "
                 "check the port and whether the server is running", (unsigned)d->port);
        return;
    case NET_FAIL_TIMEOUT:
        snprintf(out, cap, "the server does not answer: it is off, the address is wrong, "
                 "or the network blocks it");
        return;
    case NET_FAIL_RESET:
        snprintf(out, cap, "the connection was reset during the handshake: "
                 "check the network and the server settings");
        return;
    case NET_FAIL_CLOSED:
        snprintf(out, cap, "the server closed the connection during the handshake: "
                 "check the port and the security setting");
        return;
    case NET_FAIL_NOT_TLS:
        snprintf(out, cap, "the server does not speak TLS on port %u: "
                 "check the port and the security setting", (unsigned)d->port);
        return;
    case NET_FAIL_CERT_NAME:
        snprintf(out, cap, "the server certificate is for another name: check the SNI");
        return;
    case NET_FAIL_CERT_UNTRUSTED:
        snprintf(out, cap, "the server certificate is not trusted: "
                 "it is self-signed or from an unknown issuer");
        return;
    case NET_FAIL_CERT_EXPIRED:
        if (d->clock_known && d->clock_offset_s > NET_DIAG_CLOCK_LIMIT_S)
            net_diag_clock_text(d->clock_offset_s, out, cap);
        else
            snprintf(out, cap, "the server certificate has expired");
        return;
    case NET_FAIL_CERT_NOT_YET_VALID:
        if (d->clock_known && d->clock_offset_s < -NET_DIAG_CLOCK_LIMIT_S)
            net_diag_clock_text(d->clock_offset_s, out, cap);
        else
            snprintf(out, cap, "the server certificate is not valid yet: "
                     "check the device date and the certificate dates");
        return;
    case NET_FAIL_REALITY:
        if (clock_off(d))
            net_diag_clock_text(d->clock_offset_s, out, cap);
        else
            snprintf(out, cap, "REALITY did not accept the connection: "
                     "check the public key, the short id and the SNI");
        return;
    case NET_FAIL_HTTP_STATUS:
        if (v == 404)
            snprintf(out, cap, "the server has no such path (HTTP 404): check the path");
        else if (v == 400)
            snprintf(out, cap, "the server rejected the request (HTTP 400): "
                     "check the path and the transport type");
        else if (v == 403)
            snprintf(out, cap, "the server refused access (HTTP 403): check the path and the host");
        else if (v >= 500 && v <= 599)
            snprintf(out, cap, "the server front answered HTTP %d: check the server and its proxy service", v);
        else
            snprintf(out, cap, "the server answered HTTP %d instead of opening the transport: "
                     "check the path and the transport type", v);
        return;
    case NET_FAIL_GRPC_STATUS:
        if (v == 5 || v == 12)
            snprintf(out, cap, "the server has no such gRPC service: check the service name");
        else
            snprintf(out, cap, "the server answered gRPC status %d: check the service name", v);
        return;
    case NET_FAIL_TRANSPORT:
        snprintf(out, cap, "the server did not open the chosen transport: check the path, host and transport type");
        return;
    case NET_FAIL_TLS:
    case NET_FAIL_NONE:
    default:
        snprintf(out, cap, "the TLS handshake with the server failed: "
                 "check the SNI and the security setting");
        return;
    }
}

void net_diag_text(const net_diag_t *d, char *out, size_t cap) {
    if (!out || !cap) return;
    out[0] = '\0';
    if (!d) return;
    const char *what = credential(d->proto);
    int vmess = d->proto == VL_PROTO_VMESS;
    switch (d->stage) {
    case NET_DIAG_OK:
        return;
    case NET_DIAG_DNS:
        snprintf(out, cap, "the server name does not resolve: "
                 "check the address and the internet connection");
        return;
    case NET_DIAG_TRANSPORT:
        transport_text(d, out, cap);
        return;
    case NET_DIAG_CLOSED:
        if (vmess && clock_off(d))
            net_diag_clock_text(d->clock_offset_s, out, cap);
        else if (vmess && !d->clock_known)
            snprintf(out, cap, "the server closed the request without an answer: "
                     "check the UUID and the device clock");
        else
            snprintf(out, cap, "the server closed the request without an answer: check the %s",
                     what);
        return;
    case NET_DIAG_SILENT:
        if (vmess && clock_off(d))
            net_diag_clock_text(d->clock_offset_s, out, cap);
        else
            snprintf(out, cap, "the server did not return an answer: check the %s; "
                     "also check whether the server can reach the internet", what);
        return;
    case NET_DIAG_ANSWER:
        if (d->answer == SESS_FAIL_DECRYPT)
            snprintf(out, cap, "the server answer does not decrypt: check the %s, "
                     "or this is not a %s server", what, proto_name(d->proto));
        else if (d->answer == SESS_FAIL_WEBSITE)
            snprintf(out, cap, "the server answered as a website instead of carrying "
                     "the request: check the %s", what);
        else if (d->answer == SESS_FAIL_LOGIN)
            snprintf(out, cap, "the proxy refused the user name or password");
        else
            snprintf(out, cap, "the server answer is not %s: check the protocol and the port",
                     proto_name(d->proto));
        return;
    case NET_DIAG_NOT_PROXIED:
        snprintf(out, cap, "the connection check received HTTP %d instead of 204: check the %s and "
                 "whether the server can reach the check website", d->http_status, what);
        return;
    }
}
