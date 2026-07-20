#include "session.h"
#include "senko_trace.h"
#include "senko_replay.h"
#include "senko_upload.h"
#include "socks5.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define VISION_FIRST_WAIT_MS 500

static size_t q_append(uint8_t *q, size_t *qlen, size_t cap,
                       const uint8_t *src, size_t len) {
    size_t room = cap - *qlen;
    size_t take = len < room ? len : room;
    memcpy(q + *qlen, src, take);
    *qlen += take;
    return take;
}

/* stage new remote bytes behind queued data so transport order stays intact */
static int transport_write_mode(session_t *s, const uint8_t *buf, size_t len, int raw) {
    int w;
    if (!raw)
        w = s->vt->write(s->th, buf, len);
    else if (!s->vt->raw_write)
        w = TRANSPORT_ERR;
    else
        w = s->vt->raw_write(s->th, buf, len);
    if (w > 0)
        senko_upload_wire_feed(s, buf, (size_t)w);
    return w;
}

static void activate_pending_direct_if_drained(session_t *s) {
    if (s->vision_upstream_direct_pending && s->to_remote_len == 0) {
        s->vision_upstream_direct_pending = 0;
        if (s->vision_upstream_direct_mark_transport && s->vt->raw_write) {
            (void)s->vt->raw_write(s->th, NULL, 0);
            s->vision_upstream_direct_mark_transport = 0;
        }
        s->vision_upstream_direct = 1;
        fprintf(stderr, "senkod: vision upstream direct active\n");
        senko_trace_sess(s, "upstream_direct_active", "pending drained");
    }
}

static void request_upstream_direct(session_t *s, const char *why, int mark_transport) {
    if (s->vision_upstream_direct) return;
    if (s->to_remote_len == 0) {
        if (mark_transport && s->vt->raw_write)
            (void)s->vt->raw_write(s->th, NULL, 0);
        s->vision_upstream_direct = 1;
        fprintf(stderr, "senkod: vision upstream direct active: %s\n",
                why ? why : "direct");
        senko_trace_sess(s, "upstream_direct_active", why);
    } else {
        if (mark_transport)
            s->vision_upstream_direct_mark_transport = 1;
        s->vision_upstream_direct_pending = 1;
        fprintf(stderr, "senkod: vision upstream direct pending: %s\n",
                why ? why : "direct");
        senko_trace_sess(s, "upstream_direct_pending", why);
    }
}

static size_t push_remote_mode(session_t *s, const uint8_t *buf, size_t len, int raw) {
    if (s->to_remote_len > 0) {
        if (s->to_remote_raw != raw) return 0;
        return q_append(s->to_remote, &s->to_remote_len, sizeof s->to_remote, buf, len);
    }

    size_t off = 0;
    while (off < len) {
        int w = transport_write_mode(s, buf + off, len - off, raw);
        if (w > 0) {
            off += (size_t)w;
            continue;
        }
        if (w == TRANSPORT_WANT_WRITE || w == TRANSPORT_WANT_READ) {
            s->to_remote_raw = raw;
            off += q_append(s->to_remote, &s->to_remote_len, sizeof s->to_remote,
                            buf + off, len - off);
            break;
        }
        senko_trace_sess(s, "SESS_ERROR", raw ? "push_remote_raw" : "push_remote");
        fprintf(stderr, "senkod: session remote write failed (%s)\n",
                raw ? "raw" : "framed");
        s->state = SESS_ERROR;
        break;
    }
#ifndef SENKO_RELEASE
    s->trace_wire_tx += (uint64_t)off;
#endif
    return off;
}

static size_t push_remote(session_t *s, const uint8_t *buf, size_t len) {
    return push_remote_mode(s, buf, len, 0);
}

static size_t push_remote_raw(session_t *s, const uint8_t *buf, size_t len) {
    return push_remote_mode(s, buf, len, 1);
}

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

static void flush_remote(session_t *s) {
    size_t off = 0;
    while (off < s->to_remote_len) {
        int w = transport_write_mode(s, s->to_remote + off,
                                     s->to_remote_len - off,
                                     s->to_remote_raw);
        if (w > 0) { off += (size_t)w; continue; }
        if (w == TRANSPORT_WANT_WRITE || w == TRANSPORT_WANT_READ) break;
        s->state = SESS_ERROR;
        return;
    }
    if (off > 0) {
        memmove(s->to_remote, s->to_remote + off, s->to_remote_len - off);
        s->to_remote_len -= off;
    }
    if (s->to_remote_len == 0) s->to_remote_raw = 0;
    activate_pending_direct_if_drained(s);
}

#define VISION_MAX_OVERHEAD (16 + 5 + 1400)

static sess_status_t send_vision_first(session_t *s,
                                       const uint8_t *payload, size_t payload_len,
                                       size_t *taken) {
    uint8_t req[VLESS_UUID_LEN + 512];
    size_t reqlen = 0;
    size_t content = payload_len;
    if (content > 8192) content = 8192;
    if (content > 0xffff) content = 0xffff;

    uint8_t blk[8192 + VISION_MAX_OVERHEAD];
    size_t blk_len = 0;
    int sent_direct = 0;
    if (content > 0) {
        senko_upload_in_feed(s, payload, content);
        int was_direct = s->vwrap.direct_sent;
        if (vision_wrap(&s->vwrap, payload, content,
                        blk, sizeof blk, &blk_len) != 0) {
            s->state = SESS_ERROR;
            return SESS_ERR;
        }
        sent_direct = !was_direct && s->vwrap.direct_sent;
    } else {
        if (vision_wrap_bootstrap(&s->vwrap, blk, sizeof blk, &blk_len) != 0) {
            s->state = SESS_ERROR;
            return SESS_ERR;
        }
    }

    vc_status_t vs = vless_conn_make_request(&s->u.vc, NULL, 0,
                                             req, sizeof req, &reqlen);
    if (vs != VC_OK) {
        s->state = SESS_ERROR;
        return SESS_ERR;
    }

    uint8_t first[VLESS_UUID_LEN + 512 + 8192 + VISION_MAX_OVERHEAD];
    if (reqlen + blk_len > sizeof first) {
        s->state = SESS_ERROR;
        return SESS_ERR;
    }
    memcpy(first, req, reqlen);
    memcpy(first + reqlen, blk, blk_len);
    push_remote(s, first, reqlen + blk_len);
    if (s->state == SESS_ERROR) return SESS_ERR;

    s->state = SESS_VLESS_RESP;
/* enable bulk aead when vision requests direct mode immediately */
    if (sent_direct)
        request_upstream_direct(s, "vision first DIRECT", 1);
    if (taken) *taken = content;
    senko_trace_sess(s, content ? "vision_first_payload" : "vision_first_bootstrap",
                     content ? "header+payload" : "header+bootstrap");
    return SESS_OK;
}

/* wrap client bytes atomically so backpressure cannot split a vision block */
static size_t push_app(session_t *s, const uint8_t *buf, size_t len) {
    if (!s->vision_on) {
        size_t n = push_remote(s, buf, len);
#ifndef SENKO_RELEASE
        s->trace_app_tx += (uint64_t)n;
#endif
        return n;
    }
    if (len == 0) return 0;

    activate_pending_direct_if_drained(s);
    if (s->vision_upstream_direct) return push_remote_raw(s, buf, len);
    if (s->vision_upstream_direct_pending) return 0;

    size_t free_rm = sizeof s->to_remote - s->to_remote_len;
    if (free_rm <= VISION_MAX_OVERHEAD) return 0;

    size_t maxc = free_rm - VISION_MAX_OVERHEAD;
    if (maxc > 8192)  maxc = 8192; /* keep the temp block small */
    if (maxc > 0xffff) maxc = 0xffff;
    size_t content = len < maxc ? len : maxc;

    senko_upload_in_feed(s, buf, content);

    uint8_t blk[8192 + VISION_MAX_OVERHEAD];
    size_t bn = 0;
    int was_direct = s->vwrap.direct_sent;
    if (vision_wrap(&s->vwrap, buf, content, blk, sizeof blk, &bn) != 0) return 0;
    int sent_direct = !was_direct && s->vwrap.direct_sent;

    push_remote(s, blk, bn);
    if (s->state == SESS_ERROR) return 0;
    if (sent_direct)
        request_upstream_direct(s, "local DIRECT", 1);
#ifndef SENKO_RELEASE
    s->trace_app_tx += (uint64_t)content;
#endif
    return content;
}

static void deliver_client(session_t *s, const uint8_t *buf, size_t len) {
/* reject overflow so package downloads cannot become partial archives */
    if (!s->vision_on || s->vision_downstream_direct) {
        size_t took = q_append(s->to_client, &s->to_client_len,
                               sizeof s->to_client, buf, len);
        if (took < len) {
            fprintf(stderr, "senkod: to_client overflow, dropped %zu bytes "
                    "(would corrupt downloads)\n", len - took);
            s->state = SESS_ERROR;
            return;
        }
#ifndef SENKO_RELEASE
        s->trace_app_rx += (uint64_t)len;
#endif
        return;
    }
    senko_replay_feed(s, buf, len);
    uint8_t app[SESS_BUF]; size_t an = 0; int dir = 0;
    if (vision_unpad(&s->vunpad, buf, len, app, sizeof app, &an, &dir) != 0) {
        char detail[128];
        snprintf(detail, sizeof detail,
                 "in=%zu stash=%zu rem_cmd=%d rem_cnt=%d rem_pad=%d direct=%d",
                 len, s->vunpad.stash_len, s->vunpad.remaining_command,
                 (int)s->vunpad.remaining_content, (int)s->vunpad.remaining_padding,
                 s->vunpad.direct);
        senko_trace_sess(s, "vision_unpad_err", detail);
        senko_trace_sess(s, "SESS_ERROR", "vision_unpad");
        s->state = SESS_ERROR;
        return;
    }
    if (dir) {
        s->vision_downstream_direct = 1;
        request_upstream_direct(s, "downstream DIRECT", 1);
        fprintf(stderr, "senkod: vision downstream direct\n");
        senko_trace_sess(s, "downstream_direct", "vision CMD_DIRECT");
    }
    size_t took = q_append(s->to_client, &s->to_client_len,
                           sizeof s->to_client, app, an);
    if (took < an) {
        fprintf(stderr, "senkod: to_client overflow after vision, dropped %zu\n",
                an - took);
        s->state = SESS_ERROR;
        return;
    }
#ifndef SENKO_RELEASE
    s->trace_app_rx += (uint64_t)an;
#endif
}

static void push_staged_client(session_t *s) {
    if ((s->state != SESS_VLESS_RESP && s->state != SESS_RELAY) ||
        s->client_stage_len == 0)
        return;

    size_t pushed = push_app(s, s->client_stage, s->client_stage_len);
    memmove(s->client_stage, s->client_stage + pushed,
            s->client_stage_len - pushed);
    s->client_stage_len -= pushed;
}

sess_status_t session_init(session_t *s,
                           const transport_vt_t *vt, void *th,
                           vl_proto_t proto,
                           const uint8_t uuid[VLESS_UUID_LEN],
                           const char *flow,
                           const char *user,
                           const char *pass) {
    if (!s || !vt) return SESS_ERR_ARG;
    memset(s, 0, sizeof *s);
    s->vt = vt;
    s->th = th;
    s->proto = proto;
    s->state = SESS_GREETING;

    if (proto == VL_PROTO_VLESS) {
        if (uuid) memcpy(s->u.vc.uuid, uuid, VLESS_UUID_LEN);
        s->u.vc.flow = (flow && flow[0]) ? flow : NULL;
/* enable vision only for its explicit flow so both directions agree */
        if (flow && strcmp(flow, "xtls-rprx-vision") == 0 && uuid) {
            s->vision_on = 1;
            vision_wrap_init(&s->vwrap, uuid);
            vision_unpad_init(&s->vunpad, uuid);
            senko_trace_sess(s, "sess_init", "vision");
        }
    } else if (proto == VL_PROTO_SOCKS5) {
        s5c_init(&s->u.s5c, user, pass, &(vless_dest_t){0});
    } else if (proto == VL_PROTO_HTTP || proto == VL_PROTO_HTTPS) {
        hc_init(&s->u.hc, user, pass, &(vless_dest_t){0});
    }
    return SESS_OK;
}

sess_status_t session_init_after_greet(session_t *s,
                                       const transport_vt_t *vt, void *th,
                                       vl_proto_t proto,
                                       const uint8_t uuid[VLESS_UUID_LEN],
                                       const char *flow,
                                       const char *user,
                                       const char *pass) {
    sess_status_t r = session_init(s, vt, th, proto, uuid, flow, user, pass);
    if (r != SESS_OK) return r;
    s->state = SESS_REQUEST;
    return SESS_OK;
}

static void queue_socks_reply(session_t *s, uint8_t code) {
    uint8_t rep[10];
    size_t rn = 0;
    socks5_build_reply(code, rep, sizeof rep, &rn);
    q_append(s->to_client, &s->to_client_len, sizeof s->to_client, rep, rn);
}

static void flush_pending_socks_ok(session_t *s) {
    if (!s->pending_socks_ok) return;
    s->pending_socks_ok = 0;
    queue_socks_reply(s, SOCKS5_REP_OK);
}

static void fail_pending_socks(session_t *s) {
    if (!s->pending_socks_ok) return;
    s->pending_socks_ok = 0;
    queue_socks_reply(s, SOCKS5_REP_GENERAL_FAIL);
}

static sess_status_t begin_vless_dest(session_t *s, const vless_dest_t *dest,
                                     const uint8_t *payload, size_t payload_len,
                                     size_t *payload_used_out, int send_socks_ok) {
    session_set_trace_host(s, dest);
    s->u.vc.dest = *dest;
    s->u.vc.state = VC_ST_INIT;

/* acknowledge socks early because vision peers may reset an empty bootstrap */
    if (send_socks_ok)
        queue_socks_reply(s, SOCKS5_REP_OK);
    s->pending_socks_ok = 0;

    size_t payload_used = 0;
    if (s->vision_on) {
        if (payload_len) {
            size_t took = 0;
            if (send_vision_first(s, payload, payload_len, &took) != SESS_OK)
                return SESS_ERR;
            payload_used = took;
        } else {
/* wait for client bytes before sending an empty vision bootstrap */
            s->state = SESS_VISION_FIRST;
            s->vision_first_deadline_ms = now_ms() + VISION_FIRST_WAIT_MS;
            senko_trace_sess(s, "vision_first_wait",
                             send_socks_ok ? "await client after socks ok"
                                           : "empty tproxy payload");
        }
    } else {
        uint8_t req[VLESS_UUID_LEN + 512];
        size_t cap = sizeof req;
        size_t first = payload_len;
        size_t header_budget = 64;
        if (first + header_budget > cap) first = cap - header_budget;

        size_t reqlen = 0;
        vc_status_t vs = vless_conn_make_request(&s->u.vc, payload, first,
                                                 req, cap, &reqlen);
        if (vs != VC_OK) { s->state = SESS_ERROR; return SESS_ERR; }
        push_remote(s, req, reqlen);
        if (s->state == SESS_ERROR) return SESS_ERR;
        payload_used = first;
    }

    if (payload_used_out) *payload_used_out = payload_used;
    if (s->state != SESS_VISION_FIRST)
        s->state = SESS_VLESS_RESP;
    return SESS_OK;
}

sess_status_t session_start_from_socks_dest(session_t *s,
                                            const vless_dest_t *dest,
                                            const uint8_t *payload,
                                            size_t payload_len) {
    if (!s || !dest) return SESS_ERR_ARG;
    if (s->proto != VL_PROTO_VLESS) return SESS_ERR_ARG;
    return begin_vless_dest(s, dest, payload, payload_len, NULL, 1);
}

sess_status_t session_start_from_transparent_dest(session_t *s,
                                                  const vless_dest_t *dest,
                                                  const uint8_t *payload,
                                                  size_t payload_len,
                                                  size_t *payload_used_out) {
    if (!s || !dest) return SESS_ERR_ARG;
    if (s->proto != VL_PROTO_VLESS) return SESS_ERR_ARG;
    return begin_vless_dest(s, dest, payload, payload_len, payload_used_out, 0);
}

static sess_status_t do_greeting(session_t *s, size_t *consumed) {
    size_t used = 0;
    s5_status_t r = socks5_parse_greeting(s->client_stage, s->client_stage_len, &used);
    if (r == S5_NEED_MORE) { *consumed = 0; return SESS_OK; }
    if (r != S5_OK) { s->state = SESS_ERROR; return SESS_ERR; }

    uint8_t reply[2]; size_t rn = 0;
    socks5_build_method_reply(reply, sizeof reply, &rn);
    q_append(s->to_client, &s->to_client_len, sizeof s->to_client, reply, rn);

    memmove(s->client_stage, s->client_stage + used, s->client_stage_len - used);
    s->client_stage_len -= used;
    s->state = SESS_REQUEST;
    *consumed = 0; /* staged greeting bytes were already consumed */
    return SESS_OK;
}

static sess_status_t do_request(session_t *s) {
    vless_dest_t dest;
    size_t used = 0;
    s5_status_t r = socks5_parse_request(s->client_stage, s->client_stage_len,
                                         &dest, &used);
    if (r == S5_NEED_MORE) return SESS_OK;
    if (r != S5_OK) {
        uint8_t rep[10]; size_t rn = 0;
        uint8_t code = (r == S5_ERR_CMD) ? SOCKS5_REP_CMD_NOT_SUP
                                         : SOCKS5_REP_GENERAL_FAIL;
        socks5_build_reply(code, rep, sizeof rep, &rn);
        q_append(s->to_client, &s->to_client_len, sizeof s->to_client, rep, rn);
        s->state = SESS_ERROR;
        return SESS_ERR;
    }

    if (s->proto == VL_PROTO_VLESS) {
        const uint8_t *payload = s->client_stage + used;
        size_t payload_len = s->client_stage_len - used;
        size_t payload_used = 0;
        if (begin_vless_dest(s, &dest, payload, payload_len, &payload_used, 1) != SESS_OK)
            return SESS_ERR;
        size_t eaten = used + payload_used;
        memmove(s->client_stage, s->client_stage + eaten, s->client_stage_len - eaten);
        s->client_stage_len -= eaten;
    } else if (s->proto == VL_PROTO_SOCKS5) {
        session_set_trace_host(s, &dest);
        s->u.s5c.dest = dest;
        s->u.s5c.state = S5C_ST_INIT;

        uint8_t req[512];
        size_t reqlen = 0;
        int rs = s5c_make_request(&s->u.s5c, req, sizeof req, &reqlen);
        if (rs != S5C_OK) { s->state = SESS_ERROR; return SESS_ERR; }

        push_remote(s, req, reqlen);
        if (s->state == SESS_ERROR) return SESS_ERR;

        memmove(s->client_stage, s->client_stage + used, s->client_stage_len - used);
        s->client_stage_len -= used;

        s->state = SESS_VLESS_RESP;
    } else if (s->proto == VL_PROTO_HTTP || s->proto == VL_PROTO_HTTPS) {
        s->u.hc.dest = dest;
        s->u.hc.state = HC_ST_INIT;

        uint8_t req[1024];
        size_t reqlen = 0;
        int rs = hc_make_request(&s->u.hc, req, sizeof req, &reqlen);
        if (rs != HC_OK) { s->state = SESS_ERROR; return SESS_ERR; }

        push_remote(s, req, reqlen);
        if (s->state == SESS_ERROR) return SESS_ERR;

        memmove(s->client_stage, s->client_stage + used, s->client_stage_len - used);
        s->client_stage_len -= used;

        s->state = SESS_VLESS_RESP;
    }

    return SESS_OK;
}

sess_status_t session_feed_client(session_t *s,
                                  const uint8_t *in, size_t len,
                                  size_t *consumed) {
    if (!s || (!in && len) || !consumed) return SESS_ERR_ARG;
    *consumed = 0;
    if (s->state == SESS_ERROR || s->state == SESS_CLOSED) return SESS_ERR;

    if (s->state == SESS_GREETING || s->state == SESS_REQUEST) {
        size_t took = q_append(s->client_stage, &s->client_stage_len,
                               sizeof s->client_stage, in, len);
        *consumed = took;

        for (;;) {
            sess_state_t before = s->state;
            if (s->state == SESS_GREETING) {
                size_t c;
                if (do_greeting(s, &c) != SESS_OK) return SESS_ERR;
            } else if (s->state == SESS_REQUEST) {
                if (do_request(s) != SESS_OK) return SESS_ERR;
            } else {
                break;
            }
            if (s->state == before) break; /* wait when this pass made no progress */
        }

/* flush staged bytes after the request so early client data is not stranded */
        if ((s->state == SESS_VLESS_RESP || s->state == SESS_RELAY) &&
            s->client_stage_len > 0) {
            push_staged_client(s);
            if (s->state == SESS_ERROR) return SESS_ERR;
        }
        return SESS_OK;
    }

    if (s->state == SESS_VISION_FIRST) {
        size_t took = 0;
        if (send_vision_first(s, in, len, &took) != SESS_OK) return SESS_ERR;
        *consumed = took;
        return SESS_OK;
    }

/* relay client data while the remote side parses its response header */
    *consumed = push_app(s, in, len);
    if (s->state == SESS_ERROR) return SESS_ERR;
    return SESS_OK;
}

sess_status_t session_pump_remote(session_t *s) {
    if (!s) return SESS_ERR_ARG;
    if (s->state == SESS_ERROR) return SESS_ERR;

    if (s->state == SESS_VISION_FIRST) {
        if (now_ms() < s->vision_first_deadline_ms) return SESS_OK;
        fprintf(stderr, "senkod: vision first bootstrap timeout fired\n");
        if (send_vision_first(s, NULL, 0, NULL) != SESS_OK) return SESS_ERR;
    }

    flush_remote(s);
    if (s->state == SESS_ERROR) return SESS_ERR;
/* nudge reality so accepted plaintext can flush pending ciphertext */
    if (s->to_remote_len == 0 && s->vt && s->vt->write && s->th &&
        (s->state == SESS_RELAY || s->state == SESS_VLESS_RESP)) {
        int nw = s->vt->write(s->th, (const uint8_t *)"", 0);
        (void)nw; /* empty writes only trigger the internal flush */
    }
    push_staged_client(s);
    if (s->state == SESS_ERROR) return SESS_ERR;

/* drain all readable records in one pump (avoid empty/truncated body) */
    for (;;) {
        size_t room = sizeof s->to_client - s->to_client_len;
        if (room == 0) return SESS_OK;

        uint8_t tmp[SESS_BUF];
        size_t want = room < sizeof tmp ? room : sizeof tmp;
        int n = s->vt->read(s->th, tmp, want);

        if (n == TRANSPORT_WANT_READ || n == TRANSPORT_WANT_WRITE) return SESS_OK;
        if (n == TRANSPORT_EOF) {
            fprintf(stderr, "senkod: transport eof in session state=%d queued=%zu\n",
                    (int)s->state, s->to_client_len);
            fail_pending_socks(s);
            s->state = SESS_CLOSED;
            return SESS_OK;
        }
        if (n < 0) {
            fprintf(stderr, "senkod: transport read error in session state=%d rc=%d\n",
                    (int)s->state, n);
            fail_pending_socks(s);
            s->state = SESS_ERROR;
            return SESS_ERR;
        }
        if (n == 0) return SESS_OK;

#ifndef SENKO_RELEASE
        s->trace_wire_rx += (uint64_t)n;
#endif

        if (s->state == SESS_VLESS_RESP) {
            if (s->proto == VL_PROTO_VLESS) {
                size_t app_off = 0;
                vc_status_t vs = vless_conn_feed_response(&s->u.vc, tmp, (size_t)n, &app_off);
                if (vs == VC_NEED_MORE) return SESS_OK; /* retain a partial header */
                if (vs != VC_OK) {
                    fprintf(stderr, "senkod: vless response parse failed rc=%d bytes=%d\n",
                            (int)vs, n);
                    fail_pending_socks(s);
                    s->state = SESS_ERROR;
                    return SESS_ERR;
                }
/* deliver the response tail as application data after the header */
                s->state = SESS_RELAY;
                flush_pending_socks_ok(s);
                fprintf(stderr, "senkod: vless response ok app_tail=%zu\n",
                        (size_t)n - app_off);
                senko_trace_sess(s, "relay_established", "vless");
                deliver_client(s, tmp + app_off, (size_t)n - app_off);
                if (s->state == SESS_ERROR) return SESS_ERR;
            } else if (s->proto == VL_PROTO_SOCKS5) {
                size_t consumed = 0;
                uint8_t out[1024];
                size_t out_len = 0;
                int rs = s5c_feed(&s->u.s5c, tmp, (size_t)n, &consumed, out, sizeof out, &out_len);
                if (rs == S5C_NEED_MORE) return SESS_OK;
                if (rs != S5C_OK) {
                    uint8_t rep[10]; size_t rn = 0;
                    socks5_build_reply(SOCKS5_REP_GENERAL_FAIL, rep, sizeof rep, &rn);
                    q_append(s->to_client, &s->to_client_len, sizeof s->to_client, rep, rn);
                    s->state = SESS_ERROR;
                    return SESS_ERR;
                }

                if (out_len > 0) {
                    push_remote(s, out, out_len);
                    if (s->state == SESS_ERROR) return SESS_ERR;
                }

                if (s->u.s5c.state == S5C_ST_OPEN) {
                    uint8_t rep[10]; size_t rn = 0;
                    socks5_build_reply(SOCKS5_REP_OK, rep, sizeof rep, &rn);
                    q_append(s->to_client, &s->to_client_len, sizeof s->to_client, rep, rn);
                    s->state = SESS_RELAY;

                    if (consumed < (size_t)n) {
                        q_append(s->to_client, &s->to_client_len, sizeof s->to_client,
                                 tmp + consumed, (size_t)n - consumed);
                    }
                }
            } else if (s->proto == VL_PROTO_HTTP || s->proto == VL_PROTO_HTTPS) {
                size_t consumed = 0;
                int rs = hc_feed(&s->u.hc, tmp, (size_t)n, &consumed);
                if (rs == HC_NEED_MORE) return SESS_OK;
                if (rs != HC_OK) {
                    uint8_t rep[10]; size_t rn = 0;
                    socks5_build_reply(SOCKS5_REP_GENERAL_FAIL, rep, sizeof rep, &rn);
                    q_append(s->to_client, &s->to_client_len, sizeof s->to_client, rep, rn);
                    s->state = SESS_ERROR;
                    return SESS_ERR;
                }

                if (s->u.hc.state == HC_ST_OPEN) {
                    uint8_t rep[10]; size_t rn = 0;
                    socks5_build_reply(SOCKS5_REP_OK, rep, sizeof rep, &rn);
                    q_append(s->to_client, &s->to_client_len, sizeof s->to_client, rep, rn);
                    s->state = SESS_RELAY;

                    if (consumed < (size_t)n) {
                        q_append(s->to_client, &s->to_client_len, sizeof s->to_client,
                                 tmp + consumed, (size_t)n - consumed);
                    }
                }
            }

/* flush bytes parked during response parsing when relay mode starts */
            if (s->state == SESS_RELAY && s->client_stage_len > 0) {
                push_staged_client(s);
                if (s->state == SESS_ERROR) return SESS_ERR;
            }
            continue;
        }

        deliver_client(s, tmp, (size_t)n);
        if (s->state == SESS_ERROR) return SESS_ERR;
    }
}

size_t session_take_client(session_t *s, uint8_t *out, size_t cap) {
    if (!s || !out) return 0;
    size_t take = s->to_client_len < cap ? s->to_client_len : cap;
    memcpy(out, s->to_client, take);
    if (take < s->to_client_len) {
        memmove(s->to_client, s->to_client + take, s->to_client_len - take);
    }
    s->to_client_len -= take;
    return take;
}

int session_is_done(const session_t *s) {
    if (!s) return 1;
    if (s->state == SESS_ERROR) return 1;
    if (s->state == SESS_CLOSED && s->to_client_len == 0) return 1;
    return 0;
}
