#include "tun_tcp.h"
#include "core/senko_time.h"
#include "core/senko_trace.h"
#include "route_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef enum {
    RELAY_FREE = 0,
    RELAY_WAITING,   /* over the flow limit: the app's syn stays held */
    RELAY_OPENING,   /* the server connection is being opened */
    RELAY_CONNECTING_DIRECT, /* a direct flow's own connect is in progress */
    RELAY_VERIFYING, /* wait for the proxy CONNECT answer before app SYN-ACK */
    RELAY_ACCEPTED,  /* server ready, the app's handshake is completing */
    RELAY_RUNNING
} relay_state_t;

typedef struct {
    relay_state_t  state;
    uint64_t       flow_id;
    tun_flow_key_t key;
    void          *th;
    int            fd;
    int            app_eof;          /* the app sent its fin */
    int            server_write_shut; /* and it was passed on to the server */
    int            app_fin_sent;     /* the server finished and the app was told */
    int64_t        verify_deadline_ms;
    uint64_t       waiting_since;    /* admission order while RELAY_WAITING */
    int            direct;           /* a rule sent it past the server */
    int            direct_blocked;   /* the direct socket took no more bytes */
    int64_t        started_ms;
    uint64_t       up;
    uint64_t       down;
    uint8_t        hold[TUN_TCP_HOLD];
    size_t         hold_off;
    size_t         hold_len;
} relay_t;

struct tun_tcp {
    tun_tcp_config_t config;
    tun_loop_t      *loop;
    uint64_t         generation;
    tun_stack_t     *stack;
    tun_opener_t    *opener;          /* lives in the same allocation */
    int              opener_running;
    transport_tls_shared_t *shared_tls;
    tun_tcp_stats_t  stats;
    uint64_t         waiting_seq;
    relay_t          relays[TUN_NAT_TCP_MAX];
    session_t       *sessions[TUN_NAT_TCP_MAX];
};

static size_t tcp_part(void) {
    return (sizeof(struct tun_tcp) + 15u) & ~(size_t)15u;
}

size_t tun_tcp_size(void) {
    return tcp_part() + tun_opener_size();
}

const tun_tcp_stats_t *tun_tcp_stats(const tun_tcp_t *tcp) {
    return tcp ? &tcp->stats : NULL;
}

static void set_error(tun_tcp_t *tcp, const char *text) {
    snprintf(tcp->stats.last_error, sizeof tcp->stats.last_error, "%s", text);
}

static void destination_text(const tun_flow_key_t *key, char *out, size_t cap) {
    char ip[INET6_ADDRSTRLEN] = "?";
    (void)inet_ntop(key->address_len == 4 ? AF_INET : AF_INET6, key->destination,
                    ip, sizeof ip);
    snprintf(out, cap, key->address_len == 4 ? "%s:%u" : "[%s]:%u", ip,
             (unsigned)key->destination_port);
}

/* one line per flow when tracing, the way the pf path's listener logged each
   redirected connection */
static void trace_end(const relay_t *relay, const char *how, const char *why) {
    if (!senko_trace_enabled() || relay->state == RELAY_FREE) return;
    char where[64];
    destination_text(&relay->key, where, sizeof where);
    long long lived = relay->started_ms ? (long long)(senko_now_ms() - relay->started_ms) : 0;
    fprintf(stderr, "senkod: tcp %s %s %s after %lld ms, up %llu down %llu%s%s\n",
            where, relay->direct ? "direct" : "proxy", how, lived,
            (unsigned long long)relay->up, (unsigned long long)relay->down,
            why ? ": " : "", why ? why : "");
}

/* the capture slot is the low byte of a flow id, so a flow maps to its relay
   without a search; the full id is compared before anything is trusted */
static relay_t *relay_for(tun_tcp_t *tcp, uint64_t flow_id) {
    size_t slot = (size_t)(flow_id & 0xffu);
    if (slot >= TUN_NAT_TCP_MAX) return NULL;
    return &tcp->relays[slot];
}

static session_t *session_of(tun_tcp_t *tcp, const relay_t *relay) {
    return tcp->sessions[relay - tcp->relays];
}

static void close_transport(tun_tcp_t *tcp, relay_t *relay) {
    if (relay->th) tcp->config.vt->close(relay->th);
    if (relay->fd >= 0) close(relay->fd);
    relay->th = NULL;
    relay->fd = -1;
}

static void release(tun_tcp_t *tcp, relay_t *relay) {
    if (relay->state == RELAY_OPENING && tcp->opener_running)
        tun_opener_cancel(tcp->opener, relay->flow_id);
    close_transport(tcp, relay);
    size_t slot = (size_t)(relay - tcp->relays);
    free(tcp->sessions[slot]);
    tcp->sessions[slot] = NULL;
    memset(relay, 0, sizeof *relay);
    relay->fd = -1;
}

/* the flow failed after the app connected: a reset, never a clean fin, so the
   app does not take a truncated transfer for a complete one */
static void abort_flow(tun_tcp_t *tcp, relay_t *relay, const char *why) {
    uint64_t id = relay->flow_id;
    ++tcp->stats.aborted;
    set_error(tcp, why);
    trace_end(relay, "reset", why);
    release(tcp, relay);
    (void)tun_stack_flow_abort(tcp->stack, id);
}

static void refuse_flow(tun_tcp_t *tcp, relay_t *relay, const char *why) {
    uint64_t id = relay->flow_id;
    ++tcp->stats.refused;
    set_error(tcp, why);
    trace_end(relay, "refused", why);
    release(tcp, relay);
    (void)tun_stack_flow_reject(tcp->stack, id);
}

static void finish_flow(tun_tcp_t *tcp, relay_t *relay) {
    uint64_t id = relay->flow_id;
    ++tcp->stats.finished;
    trace_end(relay, "closed", NULL);
    release(tcp, relay);
    (void)tun_stack_flow_close(tcp->stack, id);
}

/* ---- the app to the server ---------------------------------------------- */

/* returns 0 while the flow lives, -1 once it was torn down */
static int app_to_server(tun_tcp_t *tcp, relay_t *relay) {
    session_t *s = session_of(tcp, relay);
    for (;;) {
        const uint8_t *data;
        size_t len;
        int eof;
        if (tun_stack_flow_peek(tcp->stack, relay->flow_id, &data, &len, &eof) != TUN_STACK_OK)
            return 0;
        if (len == 0) {
            if (eof) relay->app_eof = 1;
            break;
        }
        size_t consumed = 0;
        if (session_feed_client(s, data, len, &consumed) != SESS_OK) {
            abort_flow(tcp, relay, s->state == SESS_CLOSED
                ? "the app kept sending after the server had closed the connection"
                : "the protocol session with the server failed");
            return -1;
        }
        if (consumed == 0) break; /* the session is full: the app's window closes */
        tun_stack_flow_consume(tcp->stack, relay->flow_id, consumed);
        tcp->stats.bytes_to_server += consumed;
        relay->up += consumed;
        if (consumed < len) break;
    }
    return 0;
}

/* the app finished sending: once everything it sent reached the server, the
   server gets the same half close */
static void pass_app_fin(tun_tcp_t *tcp, relay_t *relay) {
    session_t *s = session_of(tcp, relay);
    if (!relay->app_eof || relay->server_write_shut || s->to_remote_len > 0) return;
    if (s->state != SESS_RELAY && s->state != SESS_VLESS_RESP) return;
    if (tcp->config.vt->shutdown) tcp->config.vt->shutdown(relay->th);
    else shutdown(relay->fd, SHUT_WR);
    relay->server_write_shut = 1;
}

/* ---- the server to the app ---------------------------------------------- */

static int flush_hold(tun_tcp_t *tcp, relay_t *relay) {
    while (relay->hold_off < relay->hold_len) {
        size_t accepted = 0;
        if (tun_stack_flow_write(tcp->stack, relay->flow_id, relay->hold + relay->hold_off,
                                 relay->hold_len - relay->hold_off, &accepted) != TUN_STACK_OK)
            return -1;
        if (accepted == 0) return 1; /* the app's window is full */
        relay->hold_off += accepted;
        tcp->stats.bytes_to_app += accepted;
        relay->down += accepted;
    }
    relay->hold_off = relay->hold_len = 0;
    return 0;
}

/* bytes leave the session only as far as the app's window reaches, and a
   short write keeps its rest in hold, so nothing taken is ever lost */
static int server_to_app(tun_tcp_t *tcp, relay_t *relay) {
    session_t *s = session_of(tcp, relay);
    for (;;) {
        int held = flush_hold(tcp, relay);
        if (held < 0) {
            abort_flow(tcp, relay, "the ip stack refused bytes for the app");
            return -1;
        }
        if (held > 0) return 0;
        size_t room = tun_stack_flow_writable(tcp->stack, relay->flow_id);
        if (room == 0) return 0;
        if (room > sizeof relay->hold) room = sizeof relay->hold;
        size_t taken = session_take_client(s, relay->hold, room);
        if (taken == 0) break;
        relay->hold_len = taken;
        relay->hold_off = 0;
    }

    if (!session_is_done(s)) return 0;
    if (s->state == SESS_ERROR) {
        abort_flow(tcp, relay, "the connection to the server failed in the middle of the flow");
        return -1;
    }
    /* the server finished and the app has every byte it sent */
    if (!relay->app_fin_sent) {
        relay->app_fin_sent = 1;
        (void)tun_stack_flow_shutdown(tcp->stack, relay->flow_id);
    }
    if (relay->app_eof) {
        finish_flow(tcp, relay);
        return -1;
    }
    return 0;
}

/* one pass over a running flow in both directions */
/* in this bridge SESS_CLOSED only ever means the server ended its stream;
   pumping past it would just read the same eof again on every event */
static int pump(tun_tcp_t *tcp, relay_t *relay) {
    session_t *s = session_of(tcp, relay);
    if (s->state == SESS_CLOSED) return 0;
    if (session_pump_remote(s) == SESS_OK) return 0;
    abort_flow(tcp, relay, "the connection to the server failed in the middle of the flow");
    return -1;
}

/* ---- flows the rules send direct ---------------------------------------- */

static void abort_errno(tun_tcp_t *tcp, relay_t *relay, const char *what, int value) {
    char why[128];
    snprintf(why, sizeof why, "%s", what);
    route_errno_append(why, sizeof why, value);
    abort_flow(tcp, relay, why);
}

static int direct_app_to_server(tun_tcp_t *tcp, relay_t *relay) {
    relay->direct_blocked = 0;
    for (;;) {
        const uint8_t *data;
        size_t len;
        int eof;
        if (tun_stack_flow_peek(tcp->stack, relay->flow_id, &data, &len, &eof) != TUN_STACK_OK)
            return 0;
        if (len == 0) {
            if (eof && !relay->server_write_shut) {
                relay->app_eof = 1;
                shutdown(relay->fd, SHUT_WR);
                relay->server_write_shut = 1;
            }
            return 0;
        }
        ssize_t sent = send(relay->fd, data, len, 0);
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            relay->direct_blocked = 1;
            return 0;
        }
        if (sent <= 0) {
            abort_errno(tcp, relay, "the direct connection refused the app's bytes", errno);
            return -1;
        }
        tun_stack_flow_consume(tcp->stack, relay->flow_id, (size_t)sent);
        tcp->stats.bytes_to_server += (uint64_t)sent;
        relay->up += (uint64_t)sent;
    }
}

static void direct_server_to_app(tun_tcp_t *tcp, relay_t *relay) {
    for (;;) {
        int held = flush_hold(tcp, relay);
        if (held < 0) {
            abort_flow(tcp, relay, "the ip stack refused bytes for the app");
            return;
        }
        if (held > 0 || relay->app_fin_sent) return;
        size_t room = tun_stack_flow_writable(tcp->stack, relay->flow_id);
        if (room == 0) return;
        if (room > sizeof relay->hold) room = sizeof relay->hold;
        ssize_t got = recv(relay->fd, relay->hold, room, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (got < 0) {
            abort_errno(tcp, relay, "the direct connection failed", errno);
            return;
        }
        if (got == 0) {
            relay->app_fin_sent = 1;
            (void)tun_stack_flow_shutdown(tcp->stack, relay->flow_id);
            if (relay->app_eof) finish_flow(tcp, relay);
            return;
        }
        relay->hold_len = (size_t)got;
        relay->hold_off = 0;
    }
}

static void direct_service(tun_tcp_t *tcp, relay_t *relay) {
    if (direct_app_to_server(tcp, relay) != 0) return;
    /* the destination may have finished first; the app's fin ends it then */
    if (relay->app_eof && relay->app_fin_sent && relay->hold_len == 0) {
        finish_flow(tcp, relay);
        return;
    }
    direct_server_to_app(tcp, relay);
}

static void start_direct(tun_tcp_t *tcp, relay_t *relay) {
    char error[160];
    int family = relay->key.address_len == 4 ? AF_INET : AF_INET6;
    int fd = tcp->config.direct_socket
        ? tcp->config.direct_socket(tcp->config.direct_ctx, family, SOCK_STREAM,
                                    error, sizeof error)
        : -1;
    if (fd < 0) {
        refuse_flow(tcp, relay, tcp->config.direct_socket ? error
                                 : "direct TCP has no physical interface to leave by");
        return;
    }
    if (fd >= FD_SETSIZE) {
        close(fd);
        refuse_flow(tcp, relay, "the direct descriptor cannot be watched by select");
        return;
    }
    struct sockaddr_storage to;
    socklen_t to_len;
    memset(&to, 0, sizeof to);
    if (family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)&to;
        in->sin_family = AF_INET;
        in->sin_port = htons(relay->key.destination_port);
        memcpy(&in->sin_addr, relay->key.destination, 4);
        to_len = sizeof *in;
    } else {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&to;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(relay->key.destination_port);
        memcpy(&in6->sin6_addr, relay->key.destination, 16);
        to_len = sizeof *in6;
    }
    relay->fd = fd;
    relay->state = RELAY_CONNECTING_DIRECT;
    relay->verify_deadline_ms = senko_now_ms() + TUN_TCP_VERIFY_MS;
    if (connect(fd, (struct sockaddr *)&to, to_len) != 0 && errno != EINPROGRESS) {
        char why[128];
        snprintf(why, sizeof why, "the direct destination refused the connection");
        route_errno_append(why, sizeof why, errno);
        refuse_flow(tcp, relay, why);
    }
}

/* the app's handshake completes only once the destination accepted, so a dead
   direct site answers the app with a reset, as a dead server does */
static void finish_direct_connect(tun_tcp_t *tcp, relay_t *relay) {
    int socket_error = 0;
    socklen_t len = sizeof socket_error;
    if (getsockopt(relay->fd, SOL_SOCKET, SO_ERROR, &socket_error, &len) != 0)
        socket_error = errno;
    if (socket_error != 0) {
        char why[128];
        snprintf(why, sizeof why, "the direct destination refused the connection");
        route_errno_append(why, sizeof why, socket_error);
        refuse_flow(tcp, relay, why);
        return;
    }
    relay->state = RELAY_ACCEPTED;
    ++tcp->stats.opened;
    if (tun_stack_flow_accept(tcp->stack, relay->flow_id) != TUN_STACK_OK)
        release(tcp, relay);
}

static void service(tun_tcp_t *tcp, relay_t *relay) {
    if (relay->direct) {
        direct_service(tcp, relay);
        return;
    }
    if (pump(tcp, relay) != 0) return;
    if (app_to_server(tcp, relay) != 0) return;
    /* feeding may have queued bytes for the server; pumping again sends them */
    if (pump(tcp, relay) != 0) return;
    pass_app_fin(tcp, relay);
    (void)server_to_app(tcp, relay);
}

/* ---- server connections from the opener pool --------------------------- */

static void notify_loop(void *ctx) {
    tun_tcp_t *tcp = ctx;
    tun_loop_command_t command;
    command.type = TUN_TCP_CMD_OPENED;
    command.generation = tcp->generation;
    command.argument = 0;
    /* a full queue already holds a wakeup, and take() collects every finished
       job, so a refused post loses nothing */
    (void)tun_loop_post(tcp->loop, &command);
}

static void start_session(tun_tcp_t *tcp, relay_t *relay, tun_opener_result_t *result) {
    if (result->fd >= FD_SETSIZE) {
        tun_opener_discard(tcp->opener, result);
        refuse_flow(tcp, relay, "the server connection got a descriptor the tunnel"
                                " loop cannot watch");
        return;
    }
    size_t slot = (size_t)(relay - tcp->relays);
    session_t *s = calloc(1, sizeof *s);
    if (!s) {
        tun_opener_discard(tcp->opener, result);
        refuse_flow(tcp, relay, "there is not enough memory for a protocol session");
        return;
    }
    tcp->sessions[slot] = s;
    const tun_tcp_config_t *c = &tcp->config;
    if (session_init(s, c->vt, result->th, c->proto, c->uuid, c->flow,
                     c->user, c->pass) != SESS_OK) {
        tun_opener_discard(tcp->opener, result);
        refuse_flow(tcp, relay, "the protocol session could not start");
        return;
    }

    vless_dest_t dest;
    memset(&dest, 0, sizeof dest);
    dest.atyp = relay->key.address_len == 4 ? VLESS_ADDR_IPV4 : VLESS_ADDR_IPV6;
    memcpy(dest.host_addr, relay->key.destination, relay->key.address_len);
    dest.port = relay->key.destination_port;
    if (dest.port == 53 && (c->dns_upstream_len == 4 || c->dns_upstream_len == 16)) {
        dest.atyp = c->dns_upstream_len == 4 ? VLESS_ADDR_IPV4 : VLESS_ADDR_IPV6;
        memset(dest.host_addr, 0, sizeof dest.host_addr);
        memcpy(dest.host_addr, c->dns_upstream, c->dns_upstream_len);
    }
    size_t used = 0;
    if (session_start_from_transparent_dest(s, &dest, NULL, 0, &used) != SESS_OK) {
        tun_opener_discard(tcp->opener, result);
        refuse_flow(tcp, relay, "the server protocol could not encode the captured destination");
        return;
    }

    relay->th = result->th;
    relay->fd = result->fd;
    /* only a proxy that answers CONNECT by itself can confirm the flow first.
       xray holds the vless response header until the target sends a byte,
       and an app that speaks first cannot send before its handshake */
    if (c->proto == VL_PROTO_SOCKS5 || c->proto == VL_PROTO_HTTP ||
        c->proto == VL_PROTO_HTTPS) {
        relay->state = RELAY_VERIFYING;
        relay->verify_deadline_ms = senko_now_ms() + TUN_TCP_VERIFY_MS;
        return;
    }
    relay->state = RELAY_ACCEPTED;
    ++tcp->stats.opened;
    if (tun_stack_flow_accept(tcp->stack, relay->flow_id) != TUN_STACK_OK)
        release(tcp, relay);
}

static void collect_opened(tun_tcp_t *tcp) {
    tun_opener_result_t result;
    while (tun_opener_take(tcp->opener, &result)) {
        relay_t *relay = relay_for(tcp, result.tag);
        if (!relay || relay->state != RELAY_OPENING || relay->flow_id != result.tag) {
            tun_opener_discard(tcp->opener, &result); /* its flow is gone */
            continue;
        }
        if (!result.ok) {
            refuse_flow(tcp, relay, result.message);
            continue;
        }
        start_session(tcp, relay, &result);
    }
}

/* ---- admission under the flow limit ------------------------------------ */

static size_t active_relays(const tun_tcp_t *tcp) {
    size_t active = 0;
    for (size_t i = 0; i < TUN_NAT_TCP_MAX; ++i)
        if (tcp->relays[i].state != RELAY_FREE &&
            tcp->relays[i].state != RELAY_WAITING) ++active;
    return active;
}

static void start_opening(tun_tcp_t *tcp, relay_t *relay) {
    if (relay->direct) {
        start_direct(tcp, relay);
        return;
    }
    if (!tcp->opener_running) {
        refuse_flow(tcp, relay, "this tunnel carries no flow through a server");
        return;
    }
    relay->state = RELAY_OPENING;
    if (tun_opener_submit(tcp->opener, relay->flow_id) != TUN_OPENER_OK)
        refuse_flow(tcp, relay, "too many server connections are opening at once");
}

/* a page load opens more connections than the limit at once; a reset would
   fail them, while a held syn is simply answered later. the held syn still
   expires in the stack, which releases its waiting relay */
static void admit_waiting(tun_tcp_t *tcp) {
    while (active_relays(tcp) < tcp->config.max_flows) {
        relay_t *oldest = NULL;
        for (size_t i = 0; i < TUN_NAT_TCP_MAX; ++i) {
            relay_t *relay = &tcp->relays[i];
            if (relay->state == RELAY_WAITING &&
                (!oldest || relay->waiting_since < oldest->waiting_since))
                oldest = relay;
        }
        if (!oldest) return;
        start_opening(tcp, oldest);
    }
}

/* ---- the loop's callbacks ----------------------------------------------- */

/* the rules pick the path before anything is dialed. dns over tcp always goes
   to the upstream through the server, as the pf redirect sent it. returns -1
   when the flow was answered with a reset */
static int decide(tun_tcp_t *tcp, relay_t *relay) {
    if (relay->key.destination_port == 53 || !tcp->config.policy) return 0;
    char why[96];
    rule_action_t action = tun_policy_flow_from(tcp->config.policy, &relay->key,
                                           (uint64_t)(senko_now_ms() / 1000),
                                           why, sizeof why);
    if (senko_trace_enabled() && action != RULE_ACTION_PROXY) {
        char where[64];
        destination_text(&relay->key, where, sizeof where);
        fprintf(stderr, "senkod: tcp %s %s (%s)\n", where,
                action == RULE_ACTION_BLOCK ? "blocked" : "direct", why);
    }
    if (action == RULE_ACTION_BLOCK) {
        ++tcp->stats.blocked;
        uint64_t id = relay->flow_id;
        ++tcp->stats.refused;
        set_error(tcp, "a rule blocked a TCP destination");
        release(tcp, relay);
        (void)tun_stack_flow_reject(tcp->stack, id);
        return -1;
    }
    if (action == RULE_ACTION_DIRECT) {
        relay->direct = 1;
        ++tcp->stats.direct;
    }
    return 0;
}

static void on_flow(void *ctx, tun_stack_flow_event_t event, uint64_t flow_id,
                    const tun_flow_key_t *key) {
    tun_tcp_t *tcp = ctx;
    relay_t *relay = relay_for(tcp, flow_id);
    if (!relay) return;

    switch (event) {
    case TUN_STACK_FLOW_REQUESTED:
        ++tcp->stats.requested;
        if (relay->state != RELAY_FREE) release(tcp, relay);
        relay->flow_id = flow_id;
        relay->key = *key;
        relay->fd = -1;
        relay->started_ms = senko_now_ms();
        if (decide(tcp, relay) != 0) return;
        if (active_relays(tcp) >= tcp->config.max_flows) {
            relay->state = RELAY_WAITING;
            relay->waiting_since = ++tcp->waiting_seq;
            return;
        }
        start_opening(tcp, relay);
        return;
    case TUN_STACK_FLOW_OPEN:
        if (relay->flow_id != flow_id || relay->state != RELAY_ACCEPTED) return;
        relay->state = RELAY_RUNNING;
        service(tcp, relay);
        return;
    case TUN_STACK_FLOW_READABLE:
    case TUN_STACK_FLOW_WRITABLE:
        if (relay->flow_id != flow_id || relay->state != RELAY_RUNNING) return;
        service(tcp, relay);
        return;
    case TUN_STACK_FLOW_CLOSED:
        if (relay->flow_id == flow_id && relay->state != RELAY_FREE) release(tcp, relay);
        admit_waiting(tcp);
        return;
    }
}

static void on_command(void *ctx, tun_stack_t *stack, const tun_loop_command_t *command) {
    (void)stack;
    tun_tcp_t *tcp = ctx;
    if (command->type == TUN_TCP_CMD_OPENED) collect_opened(tcp);
    admit_waiting(tcp);
}

static int on_start(void *ctx, tun_stack_t *stack, char *error, size_t error_cap) {
    tun_tcp_t *tcp = ctx;
    tcp->stack = stack;
    /* no transport: only the flows the rules send direct can be served */
    if (!tcp->config.vt) return 0;
    if (tcp->config.use_shared_tls) {
        tcp->shared_tls = transport_tls_shared_create(&tcp->config.tls);
        if (!tcp->shared_tls) {
            set_error(tcp, "the TLS trust store could not be prepared");
            snprintf(error, error_cap, "%s", tcp->stats.last_error);
            return -1;
        }
        tcp->config.tls.shared_ctx = tcp->shared_tls;
    }
    tun_opener_config_t config;
    memset(&config, 0, sizeof config);
    config.vt = tcp->config.vt;
    config.tls = tcp->config.tls;
    config.dial = tcp->config.dial;
    config.dial_ctx = tcp->config.dial_ctx;
    config.notify = notify_loop;
    config.notify_ctx = tcp;
    config.threads = tcp->config.opener_threads;
    if (tun_opener_start(tcp->opener, &config) == TUN_OPENER_OK) {
        tcp->opener_running = 1;
    } else {
        set_error(tcp, "the server connection threads could not start, so every"
                       " flow will be refused");
        snprintf(error, error_cap, "%s", tcp->stats.last_error);
        return -1;
    }
    return 0;
}

/* a full queue toward the app stops reading from the server, which is what
   holds a fast server back from a slow app */
static int wants_read(const session_t *s) {
    if (s->state == SESS_CLOSED || s->state == SESS_ERROR) return 0;
    return s->to_client_len < sizeof s->to_client;
}

/* a flow whose server is ready is served before the app's handshake is done
   too: its request goes out and early server bytes wait in the session */
static int served(const relay_t *relay) {
    return (relay->state == RELAY_RUNNING || relay->state == RELAY_ACCEPTED ||
            relay->state == RELAY_VERIFYING) &&
        relay->fd >= 0 && !relay->direct;
}

static void lower_wait(uint32_t *wait_ms, int64_t now, int64_t due) {
    int64_t left = due - now;
    if (left < 0) left = 0;
    if ((uint64_t)left < *wait_ms) *wait_ms = (uint32_t)left;
}

static void prepare_direct(const relay_t *relay, fd_set *readable, fd_set *writable,
                           int *highest, uint32_t *wait_ms, int64_t now) {
    if (relay->fd < 0) return;
    if (relay->state == RELAY_CONNECTING_DIRECT) {
        FD_SET(relay->fd, writable);
        lower_wait(wait_ms, now, relay->verify_deadline_ms);
    } else if (relay->state == RELAY_RUNNING) {
        /* bytes wait in hold for the app's window before more are read */
        if (relay->hold_len == 0 && !relay->app_fin_sent) FD_SET(relay->fd, readable);
        if (relay->direct_blocked) FD_SET(relay->fd, writable);
    } else {
        return;
    }
    if (relay->fd > *highest) *highest = relay->fd;
}

static void on_prepare(void *ctx, fd_set *readable, fd_set *writable, int *highest,
                       uint32_t *wait_ms) {
    tun_tcp_t *tcp = ctx;
    int64_t now = senko_now_ms();
    for (size_t i = 0; i < TUN_NAT_TCP_MAX; ++i) {
        relay_t *relay = &tcp->relays[i];
        if (relay->direct) prepare_direct(relay, readable, writable, highest, wait_ms, now);
        if (!served(relay)) continue;
        session_t *s = tcp->sessions[i];
        if (wants_read(s)) FD_SET(relay->fd, readable);
        if ((s->to_remote_len > 0 && !s->to_remote_wait_read) ||
            (tcp->config.vt->want_write && tcp->config.vt->want_write(relay->th)))
            FD_SET(relay->fd, writable);
        if (relay->fd > *highest) *highest = relay->fd;
/* the server's answer arrives as a readable descriptor; only these two
   waits end on the clock */
        if (s->state == SESS_VISION_FIRST)
            lower_wait(wait_ms, now, s->vision_first_deadline_ms);
        if (relay->state == RELAY_VERIFYING)
            lower_wait(wait_ms, now, relay->verify_deadline_ms);
    }
}

static void on_dispatch(void *ctx, const fd_set *readable, const fd_set *writable) {
    tun_tcp_t *tcp = ctx;
    for (size_t i = 0; i < TUN_NAT_TCP_MAX; ++i) {
        relay_t *relay = &tcp->relays[i];
        if (relay->direct && relay->fd >= 0) {
            if (relay->state == RELAY_CONNECTING_DIRECT) {
                if (FD_ISSET(relay->fd, writable)) finish_direct_connect(tcp, relay);
                else if (senko_now_ms() >= relay->verify_deadline_ms)
                    refuse_flow(tcp, relay, "the direct destination did not answer in time");
            } else if (relay->state == RELAY_RUNNING &&
                       (FD_ISSET(relay->fd, readable) || FD_ISSET(relay->fd, writable))) {
                service(tcp, relay);
            }
            continue;
        }
        if (!served(relay)) continue;
        session_t *s = tcp->sessions[i];
        if (relay->state == RELAY_VERIFYING) {
            if (senko_now_ms() >= relay->verify_deadline_ms) {
                refuse_flow(tcp, relay, "the server protocol handshake timed out");
                continue;
            }
            if (session_pump_remote(s) != SESS_OK || s->state == SESS_CLOSED ||
                s->state == SESS_ERROR) {
                refuse_flow(tcp, relay, "the server protocol handshake failed");
                continue;
            }
            if (s->state == SESS_RELAY) {
                relay->state = RELAY_ACCEPTED;
                ++tcp->stats.opened;
                if (tun_stack_flow_accept(tcp->stack, relay->flow_id) != TUN_STACK_OK)
                    release(tcp, relay);
            }
            continue;
        }
        if (FD_ISSET(relay->fd, readable) || FD_ISSET(relay->fd, writable) ||
            s->state == SESS_VISION_FIRST)
            service(tcp, relay);
    }
    admit_waiting(tcp);
}

static void on_stop(void *ctx) {
    tun_tcp_t *tcp = ctx;
    if (tcp->opener_running) {
        tun_opener_stop(tcp->opener);
        tcp->opener_running = 0;
    }
    for (size_t i = 0; i < TUN_NAT_TCP_MAX; ++i)
        if (tcp->relays[i].state != RELAY_FREE) release(tcp, &tcp->relays[i]);
    transport_tls_shared_destroy(tcp->shared_tls);
    tcp->shared_tls = NULL;
    tcp->config.tls.shared_ctx = NULL;
}

tun_tcp_status_t tun_tcp_init(tun_tcp_t *tcp, const tun_tcp_config_t *config,
                              tun_loop_config_t *loop_config) {
    if (!tcp || !config || !loop_config || !config->vt != !config->dial ||
        (config->dns_upstream_len != 0 && config->dns_upstream_len != 4 &&
         config->dns_upstream_len != 16))
        return TUN_TCP_ERR_ARG;
    if (config->vt && (config->opener_threads == 0 ||
                       config->opener_threads > TUN_OPENER_THREADS_MAX))
        return TUN_TCP_ERR_ARG;
    memset(tcp, 0, tcp_part());
    tcp->config = *config;
    if (tcp->config.max_flows == 0)
        tcp->config.max_flows = TUN_TCP_DEFAULT_MAX_FLOWS;
    if (tcp->config.max_flows > TUN_NAT_TCP_MAX) return TUN_TCP_ERR_ARG;
    tcp->opener = (tun_opener_t *)((uint8_t *)tcp + tcp_part());
    for (size_t i = 0; i < TUN_NAT_TCP_MAX; ++i) tcp->relays[i].fd = -1;

    loop_config->stack.flow_event = on_flow;
    loop_config->stack.flow_ctx = tcp;
    loop_config->on_command = on_command;
    loop_config->command_ctx = tcp;
    loop_config->hooks.start = on_start;
    loop_config->hooks.prepare = on_prepare;
    loop_config->hooks.dispatch = on_dispatch;
    loop_config->hooks.stop = on_stop;
    loop_config->hooks.ctx = tcp;
    return TUN_TCP_OK;
}

void tun_tcp_bind(tun_tcp_t *tcp, tun_loop_t *loop, uint64_t generation) {
    if (!tcp) return;
    tcp->loop = loop;
    tcp->generation = generation;
}
