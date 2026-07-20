#include "vless_conn.h"

#include <string.h>
#include <stdio.h>

vc_status_t vless_conn_init(vless_conn_t *c,
                            const uint8_t uuid[VLESS_UUID_LEN],
                            const vless_dest_t *dest,
                            const char *flow) {
    if (!c || !uuid || !dest) return VC_ERR_ARG;
    memset(c, 0, sizeof *c);
    memcpy(c->uuid, uuid, VLESS_UUID_LEN);
    c->dest  = *dest;
    c->flow  = (flow && flow[0]) ? flow : NULL;
    c->state = VC_ST_INIT;
    return VC_OK;
}

vc_status_t vless_conn_make_request(vless_conn_t *c,
                                    const uint8_t *payload, size_t payload_len,
                                    uint8_t *out, size_t cap, size_t *out_len) {
    if (!c || !out || !out_len) return VC_ERR_ARG;
    if (payload_len && !payload)  return VC_ERR_ARG;
    if (c->state != VC_ST_INIT)   return VC_ERR_STATE;

    vless_request_t req;
    memset(&req, 0, sizeof req);
    memcpy(req.uuid, c->uuid, VLESS_UUID_LEN);
    req.cmd  = VLESS_CMD_TCP; /* tun2socks only ever connects tcp here */
    req.dest = c->dest;
    req.flow = c->flow; /* advertise flow so vision servers accept us */

    size_t hdr_len = 0;
    vless_status_t s = vless_build_request(&req, out, cap, &hdr_len);
    if (s == VLESS_ERR_BUF_TOO_SMALL) return VC_ERR_ARG;
    if (s != VLESS_OK)                return VC_ERR_PROTO;

/* keep the first payload with the header for vision bootstrap safety */
    if (payload_len) {
        if (hdr_len + payload_len > cap) return VC_ERR_ARG;
        memcpy(out + hdr_len, payload, payload_len);
    }

    *out_len = hdr_len + payload_len;
    c->state = VC_ST_REQ_SENT;
    return VC_OK;
}

vc_status_t vless_conn_feed_response(vless_conn_t *c,
                                     const uint8_t *in, size_t in_len,
                                     size_t *app_off) {
    if (!c || !app_off) return VC_ERR_ARG;
    if (in_len && !in)  return VC_ERR_ARG;
    if (c->state != VC_ST_REQ_SENT) return VC_ERR_STATE;

    size_t prev = c->rbuf_len; /* header bytes staged so far */

/* bound partial headers so malformed input cannot grow the staging buffer */
    size_t room = sizeof c->rbuf - c->rbuf_len;
    size_t take = in_len < room ? in_len : room;
    memcpy(c->rbuf + c->rbuf_len, in, take);
    c->rbuf_len += take;

    size_t hdr_len = 0;
    vless_status_t s = vless_parse_response(c->rbuf, c->rbuf_len, &hdr_len);

    if (s == VLESS_ERR_SHORT) {
        if (room == 0) { /* buffer full and still no header */
            c->state = VC_ST_ERROR;
            return VC_ERR_PROTO;
        }
        return VC_NEED_MORE;
    }
    if (s != VLESS_OK) {
        c->state = VC_ST_ERROR;
        return VC_ERR_PROTO;
    }

/* compute the app offset across staged and current header bytes */
    *app_off = hdr_len - prev;
    c->state = VC_ST_OPEN;
    return VC_OK;
}
