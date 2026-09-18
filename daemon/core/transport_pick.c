#include "transport_pick.h"
#include "reality_handshake.h"

const transport_vt_t *transport_for_server(const vl_server_t *s) {
    if (!s) return NULL;

/* quic/udp only. the go backend dials it with its own bundled hysteria
   client; no senko transport speaks this wire format */
    if (s->proto == VL_PROTO_HYSTERIA2) return NULL;

    if (s->net == VL_NET_WS) {
        if (s->security == VL_SEC_REALITY) return &transport_ws_reality;
        if (s->security == VL_SEC_TLS) return &transport_ws_tls;
        if (s->security == VL_SEC_NONE) return &transport_ws_tcp;
        return NULL;
    }

    if (s->net == VL_NET_XHTTP) {
        if (s->security == VL_SEC_REALITY) return &transport_xhttp_reality;
        if (s->security == VL_SEC_TLS) return &transport_xhttp_tls;
        if (s->security == VL_SEC_NONE) return &transport_xhttp_tcp;
        return NULL;
    }

    /* gRPC uses the same HTTP/2 and TLS layers as xhttp, with its own
       message envelope selected by mode=grpc. */
    if (s->net == VL_NET_GRPC) {
        if (s->security == VL_SEC_REALITY) return &transport_xhttp_reality;
        if (s->security == VL_SEC_TLS) return &transport_xhttp_tls;
        if (s->security == VL_SEC_NONE) return &transport_xhttp_tcp;
        return NULL;
    }

    if (s->proto == VL_PROTO_SHADOWSOCKS)
        return &transport_tcp;

    if (s->proto == VL_PROTO_TROJAN) {
        if (s->net == VL_NET_TCP) return &transport_tls;
        if (s->net == VL_NET_WS) return &transport_ws_tls;
        return NULL;
    }

    if (s->proto == VL_PROTO_HTTPS)
        return &transport_tls;

    switch (s->security) {
        case VL_SEC_NONE:    return &transport_tcp;
        case VL_SEC_TLS:     return &transport_tls;
        case VL_SEC_REALITY: return &transport_reality;
        default:             return NULL;
    }
}
