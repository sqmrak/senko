#include "tun_udp.h"

#include "core/senko_time.h"
#include "core/senko_trace.h"
#include "core/session.h"
#include "core/vless_udp.h"
#include "route_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TUN_UDP_IDLE_MS 30000
#define TUN_UDP_OPEN_MS 10000
#define TUN_UDP_DNS_PENDING 32
/* an unanswered query frees its slot after this; resolvers retry sooner */
#define TUN_UDP_DNS_WAIT_MS 5000
/* a lookup the upstream leaves this long gets a stale cached answer, as the
   pf forwarder gave one when its tunnel query failed */
#define TUN_UDP_DNS_STALE_MS 2000

typedef enum { UDP_FREE = 0, UDP_OPENING, UDP_RUNNING } udp_state_t;

typedef enum {
    ASSOC_PROXY = 0, /* one captured tuple, vless udp through the server */
    ASSOC_DNS,       /* every captured lookup, to the dns upstream through the server */
    ASSOC_DIRECT     /* one captured tuple, out of the physical interface */
} assoc_kind_t;

typedef struct {
    uint8_t bytes[VLESS_UDP_PAYLOAD_MAX];
    size_t len;
} datagram_t;

typedef struct {
    tun_flow_key_t key; /* the app's query, answered on its own tuple */
    uint8_t id[2];
    dns_question_t question;
    rule_action_t action;
    int64_t sent_ms;
    int used;
} dns_pending_t;

typedef struct {
    udp_state_t state;
    assoc_kind_t kind;
    uint64_t tag;
    tun_flow_key_t key;
    vless_dest_t remote;
    vless_udp_t codec;
    session_t *session;
    void *th;
    int fd;
    int64_t touched_ms;
    int64_t opened_ms;
    datagram_t queued[TUN_UDP_QUEUE_MAX];
    size_t queue_count;
    uint8_t tx[VLESS_UDP_FRAME_MAX];
    size_t tx_len;
    size_t tx_off;
} association_t;

struct tun_udp {
    tun_udp_config_t config;
    tun_loop_t *loop;
    tun_stack_t *stack;
    uint64_t generation;
    uint64_t next_tag;
    tun_opener_t *opener;
    int opener_running;
    int running;  /* the relay is started and not yet stopped */
    transport_tls_shared_t *shared_tls;
    tun_loop_hooks_t previous_hooks;
    tun_loop_command_fn previous_command;
    void *previous_command_ctx;
    tun_udp_stats_t stats;
    association_t associations[TUN_UDP_ASSOC_MAX];
    dns_pending_t dns[TUN_UDP_DNS_PENDING];
};

static size_t udp_part(void) {
    return (sizeof(struct tun_udp) + 15u) & ~(size_t)15u;
}

size_t tun_udp_size(void) {
    return udp_part() + tun_opener_size();
}

const tun_udp_stats_t *tun_udp_stats(const tun_udp_t *udp) {
    return udp ? &udp->stats : NULL;
}

static void set_error(tun_udp_t *udp, const char *why) {
    snprintf(udp->stats.last_error, sizeof udp->stats.last_error, "%s", why);
}

static uint64_t now_seconds(void) {
    return (uint64_t)(senko_now_ms() / 1000);
}

static void destination_text(const tun_flow_key_t *key, char *out, size_t cap) {
    char ip[INET6_ADDRSTRLEN] = "?";
    (void)inet_ntop(key->address_len == 4 ? AF_INET : AF_INET6, key->destination,
                    ip, sizeof ip);
    snprintf(out, cap, key->address_len == 4 ? "%s:%u" : "[%s]:%u", ip,
             (unsigned)key->destination_port);
}

static int key_equal(const tun_flow_key_t *a, const tun_flow_key_t *b) {
    return a->protocol == b->protocol && a->address_len == b->address_len &&
        a->source_port == b->source_port &&
        a->destination_port == b->destination_port &&
        memcmp(a->source, b->source, a->address_len) == 0 &&
        memcmp(a->destination, b->destination, a->address_len) == 0;
}

static void close_association(tun_udp_t *udp, association_t *assoc) {
    if (assoc->state == UDP_OPENING && assoc->kind != ASSOC_DIRECT &&
        udp->opener_running)
        tun_opener_cancel(udp->opener, assoc->tag);
    if (assoc->th) udp->config.vt->close(assoc->th);
    if (assoc->fd >= 0) close(assoc->fd);
    free(assoc->session);
    /* no answer can reach the queries it carried any more */
    if (assoc->kind == ASSOC_DNS) memset(udp->dns, 0, sizeof udp->dns);
    memset(assoc, 0, sizeof *assoc);
    assoc->fd = -1;
}

static int queue_datagram(association_t *assoc, const uint8_t *payload,
                          size_t payload_len) {
    if (!payload || payload_len == 0 || payload_len > VLESS_UDP_PAYLOAD_MAX ||
        assoc->queue_count == TUN_UDP_QUEUE_MAX) return -1;
    datagram_t *item = &assoc->queued[assoc->queue_count++];
    memcpy(item->bytes, payload, payload_len);
    item->len = payload_len;
    return 0;
}

static void pop_datagram(association_t *assoc) {
    if (!assoc->queue_count) return;
    --assoc->queue_count;
    if (assoc->queue_count)
        memmove(assoc->queued, assoc->queued + 1,
                assoc->queue_count * sizeof assoc->queued[0]);
}

static int reply_to(tun_udp_t *udp, const tun_flow_key_t *key,
                    const uint8_t *payload, size_t len) {
    tun_stack_status_t result = tun_stack_udp_reply(udp->stack, key, payload, len);
    if (result == TUN_STACK_OK || result == TUN_STACK_QUEUED) {
        ++udp->stats.sent;
        return 0;
    }
    ++udp->stats.refused;
    set_error(udp, "the utun output queue refused a UDP response");
    return -1;
}

static int remember_dns(tun_udp_t *udp, const tun_flow_key_t *key,
                        const uint8_t *payload, size_t payload_len,
                        const dns_question_t *question, rule_action_t action) {
    if (payload_len < 2) return -1;
    int64_t now = senko_now_ms();
    for (size_t i = 0; i < TUN_UDP_DNS_PENDING; ++i) {
        dns_pending_t *pending = &udp->dns[i];
        if (pending->used && now - pending->sent_ms < TUN_UDP_DNS_WAIT_MS) continue;
        pending->used = 1;
        pending->key = *key;
        pending->id[0] = payload[0];
        pending->id[1] = payload[1];
        pending->question = *question;
        pending->action = action;
        pending->sent_ms = now;
        return 0;
    }
    return -1;
}

/* the answer goes back only to the query with the same id and question */
static dns_pending_t *take_dns_reply(tun_udp_t *udp, const uint8_t *payload,
                                     size_t payload_len) {
    dns_question_t question;
    dns_response_info_t info;
    if (payload_len < 2 ||
        dns_msg_parse_response_question(payload, payload_len, &question) != DNS_MSG_OK ||
        dns_msg_response_info(payload, payload_len, &info) != DNS_MSG_OK)
        return NULL;
    for (size_t i = 0; i < TUN_UDP_DNS_PENDING; ++i) {
        dns_pending_t *pending = &udp->dns[i];
        if (!pending->used || pending->id[0] != payload[0] ||
            pending->id[1] != payload[1] ||
            pending->question.type != question.type ||
            pending->question.class_code != question.class_code ||
            strcmp(pending->question.name, question.name) != 0)
            continue;
        pending->used = 0;
        return pending;
    }
    return NULL;
}

static void deliver(tun_udp_t *udp, association_t *assoc,
                    const uint8_t *payload, size_t len,
                    const vless_dest_t *source) {
    if (assoc->kind == ASSOC_DNS) {
        dns_pending_t *pending = take_dns_reply(udp, payload, len);
        if (!pending) {
            ++udp->stats.invalid_responses;
            set_error(udp, "the proxy returned DNS with no matching outstanding query");
            return;
        }
        uint8_t answer[VLESS_UDP_PAYLOAD_MAX];
        memcpy(answer, payload, len);
        tun_policy_answer(udp->config.policy, &pending->question, pending->action,
                          answer, len, now_seconds());
        (void)reply_to(udp, &pending->key, answer, len);
        return;
    }
    tun_flow_key_t reply = assoc->key;
    size_t addr_len = source->atyp == VLESS_ADDR_IPV4 ? 4u :
                      source->atyp == VLESS_ADDR_IPV6 ? 16u : 0u;
    if (addr_len != reply.address_len || !source->port) {
        ++udp->stats.invalid_responses;
        set_error(udp, "the proxy returned UDP from an incompatible address family");
        return;
    }
    memcpy(reply.destination, source->host_addr, addr_len);
    reply.destination_port = source->port;
    (void)reply_to(udp, &reply, payload, len);
}

static int receive_session(tun_udp_t *udp, association_t *assoc) {
    uint8_t bytes[4096];
    for (;;) {
        size_t size = session_take_client(assoc->session, bytes, sizeof bytes);
        if (size == 0) return 0;
        size_t offset = 0;
        while (offset < size) {
            size_t taken = 0, payload_len = 0;
            const uint8_t *payload = NULL;
            vless_dest_t source;
            vless_udp_status_t status = vless_udp_feed(&assoc->codec,
                bytes + offset, size - offset, &taken, &payload, &payload_len, &source);
            offset += taken;
            if (status == VLESS_UDP_NEED_MORE) break;
            if (status == VLESS_UDP_SKIPPED) continue;
            if (status == VLESS_UDP_END) {
                set_error(udp, "the proxy ended the UDP association");
                return -1;
            }
            if (status != VLESS_UDP_OK) {
                set_error(udp, "the proxy returned a malformed UDP frame");
                return -1;
            }
            deliver(udp, assoc, payload, payload_len, &source);
            assoc->touched_ms = senko_now_ms();
        }
    }
}

static int send_session(tun_udp_t *udp, association_t *assoc) {
    while (assoc->tx_off < assoc->tx_len || assoc->queue_count) {
        if (assoc->tx_off == assoc->tx_len) {
            datagram_t *item = &assoc->queued[0];
            if (vless_udp_encode(&assoc->codec, item->bytes, item->len,
                                 assoc->tx, sizeof assoc->tx, &assoc->tx_len) != VLESS_UDP_OK) {
                set_error(udp, "a captured UDP datagram did not fit its protocol frame");
                return -1;
            }
            assoc->tx_off = 0;
            pop_datagram(assoc);
        }
        size_t used = 0;
        if (session_feed_client(assoc->session, assoc->tx + assoc->tx_off,
                                assoc->tx_len - assoc->tx_off, &used) != SESS_OK) {
            set_error(udp, "the proxy session refused a UDP datagram");
            return -1;
        }
        assoc->tx_off += used;
        if (used == 0) return 0;
    }
    assoc->tx_len = assoc->tx_off = 0;
    return 0;
}

/* ---- direct associations ------------------------------------------------ */

static int send_direct(tun_udp_t *udp, association_t *assoc) {
    while (assoc->queue_count) {
        datagram_t *item = &assoc->queued[0];
        ssize_t sent;
        do {
            sent = send(assoc->fd, item->bytes, item->len, 0);
        } while (sent < 0 && errno == EINTR);
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS))
            return 0;
        if (sent < 0) {
            char why[128];
            snprintf(why, sizeof why, "a direct UDP send failed");
            route_errno_append(why, sizeof why, errno);
            set_error(udp, why);
            return -1;
        }
        ++udp->stats.direct;
        pop_datagram(assoc);
    }
    return 0;
}

static int receive_direct(tun_udp_t *udp, association_t *assoc) {
    uint8_t bytes[VLESS_UDP_PAYLOAD_MAX];
    for (int i = 0; i < 16; ++i) {
        ssize_t got = recv(assoc->fd, bytes, sizeof bytes, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        /* an icmp unreachable for a connected udp socket, the app just waits */
        if (got < 0 && errno == ECONNREFUSED) return 0;
        if (got < 0) {
            char why[128];
            snprintf(why, sizeof why, "a direct UDP receive failed");
            route_errno_append(why, sizeof why, errno);
            set_error(udp, why);
            return -1;
        }
        (void)reply_to(udp, &assoc->key, bytes, (size_t)got);
        assoc->touched_ms = senko_now_ms();
    }
    return 0;
}

static int open_direct(tun_udp_t *udp, association_t *assoc) {
    char error[160];
    int family = assoc->key.address_len == 4 ? AF_INET : AF_INET6;
    int fd = udp->config.direct_socket
        ? udp->config.direct_socket(udp->config.direct_ctx, family, SOCK_DGRAM,
                                    error, sizeof error)
        : -1;
    if (fd < 0) {
        set_error(udp, udp->config.direct_socket ? error
                       : "direct UDP has no physical interface to leave by");
        return -1;
    }
    if (fd >= FD_SETSIZE) {
        close(fd);
        set_error(udp, "the direct UDP descriptor cannot be watched by select");
        return -1;
    }
    struct sockaddr_storage to;
    socklen_t to_len;
    memset(&to, 0, sizeof to);
    if (family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)&to;
        in->sin_family = AF_INET;
        in->sin_port = htons(assoc->key.destination_port);
        memcpy(&in->sin_addr, assoc->key.destination, 4);
        to_len = sizeof *in;
    } else {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&to;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(assoc->key.destination_port);
        memcpy(&in6->sin6_addr, assoc->key.destination, 16);
        to_len = sizeof *in6;
    }
    if (connect(fd, (struct sockaddr *)&to, to_len) != 0) {
        char why[128];
        snprintf(why, sizeof why, "a direct UDP destination is unreachable");
        route_errno_append(why, sizeof why, errno);
        set_error(udp, why);
        close(fd);
        return -1;
    }
    assoc->fd = fd;
    assoc->state = UDP_RUNNING;
    return 0;
}

static void service(tun_udp_t *udp, association_t *assoc) {
    int failed;
    if (assoc->kind == ASSOC_DIRECT) {
        failed = receive_direct(udp, assoc) != 0 || send_direct(udp, assoc) != 0;
    } else {
        failed = session_pump_remote(assoc->session) != SESS_OK ||
            assoc->session->state == SESS_ERROR ||
            receive_session(udp, assoc) != 0 ||
            assoc->session->state == SESS_CLOSED ||
            send_session(udp, assoc) != 0;
    }
    if (failed) {
        if (!udp->stats.last_error[0])
            set_error(udp, "the UDP association lost its server session");
        close_association(udp, assoc);
    }
}

/* ---- server associations from the opener pool ---------------------------- */

static void notify_loop(void *ctx) {
    tun_udp_t *udp = ctx;
    tun_loop_command_t command;
    command.type = TUN_UDP_CMD_OPENED;
    command.generation = udp->generation;
    command.argument = 0;
    (void)tun_loop_post(udp->loop, &command);
}

/* every protocol reaches the dns upstream over a tcp stream: vless with its
   udp command, the others with a plain connect to port 53, whose dns-over-tcp
   framing is the same two byte length the udp codec writes. a vmess proxy
   association uses the udp command too, one chunk per datagram */
static int start_session(tun_udp_t *udp, association_t *assoc, session_t *session) {
    const tun_udp_config_t *c = &udp->config;
    int vless = c->proto == VL_PROTO_VLESS;
    int udp_command = vless || (c->proto == VL_PROTO_VMESS && assoc->kind == ASSOC_PROXY);
    if (session_init(session, c->vt, assoc->th, c->proto, c->uuid,
                     vless ? c->flow : NULL, c->user, c->pass) != SESS_OK)
        return -1;
    vless_udp_mode_t mode = vless && session->vision_on ? VLESS_UDP_XUDP : VLESS_UDP_LENGTH;
    if (vless_udp_init(&assoc->codec, mode, &assoc->remote) != VLESS_UDP_OK ||
        vless_udp_encode(&assoc->codec, assoc->queued[0].bytes, assoc->queued[0].len,
                         assoc->tx, sizeof assoc->tx, &assoc->tx_len) != VLESS_UDP_OK)
        return -1;
    size_t used = 0;
    sess_status_t started = udp_command
        ? session_start_from_transparent_udp(session, &assoc->remote,
                                             assoc->tx, assoc->tx_len, &used)
        : session_start_from_transparent_dest(session, &assoc->remote,
                                              assoc->tx, assoc->tx_len, &used);
    if (started != SESS_OK) return -1;
    assoc->tx_off = used;
    pop_datagram(assoc);
    return 0;
}

static void collect_opened(tun_udp_t *udp) {
    tun_opener_result_t result;
    while (tun_opener_take(udp->opener, &result)) {
        association_t *assoc = NULL;
        for (size_t i = 0; i < TUN_UDP_ASSOC_MAX; ++i)
            if (udp->associations[i].state == UDP_OPENING &&
                udp->associations[i].kind != ASSOC_DIRECT &&
                udp->associations[i].tag == result.tag)
                assoc = &udp->associations[i];
        if (!assoc) {
            tun_opener_discard(udp->opener, &result);
            continue;
        }
        if (!result.ok || result.fd >= FD_SETSIZE) {
            set_error(udp, result.ok ? "the server descriptor cannot be watched by select"
                                      : result.message);
            tun_opener_discard(udp->opener, &result);
            close_association(udp, assoc);
            continue;
        }
        session_t *session = calloc(1, sizeof *session);
        if (!session) {
            set_error(udp, "there is not enough memory for a UDP proxy session");
            tun_opener_discard(udp->opener, &result);
            close_association(udp, assoc);
            continue;
        }
        assoc->session = session;
        assoc->th = result.th;
        assoc->fd = result.fd;
        if (start_session(udp, assoc, session) != 0) {
            set_error(udp, "the proxy session could not start its first UDP request");
            close_association(udp, assoc);
            continue;
        }
        assoc->state = UDP_RUNNING;
        service(udp, assoc);
    }
}

/* ---- captured datagrams -------------------------------------------------- */

static int refuse(tun_udp_t *udp, const char *why) {
    ++udp->stats.refused;
    set_error(udp, why);
    return -1;
}

static void trace_verdict(const tun_flow_key_t *key, const char *verdict,
                          const char *why) {
    if (!senko_trace_enabled()) return;
    char where[64];
    destination_text(key, where, sizeof where);
    fprintf(stderr, "senkod: udp %s %s%s%s%s\n", where, verdict,
            why && why[0] ? " (" : "", why ? why : "", why && why[0] ? ")" : "");
}

/* a lookup the rules or the cache can answer never leaves the device */
static int answer_locally(tun_udp_t *udp, const tun_flow_key_t *key,
                          const uint8_t *payload, size_t len,
                          const dns_question_t *question, rule_action_t action) {
    uint8_t answer[TUN_STACK_PACKET_MAX];
    size_t answer_len = 0;
    tun_policy_t *policy = udp->config.policy;
    if (action == RULE_ACTION_BLOCK) {
        if (dns_msg_build_block(payload, len, udp->config.block_response,
                                answer, sizeof answer, &answer_len) != DNS_MSG_OK)
            return -1;
        if (policy) ++policy->stats.dns_blocked;
        return reply_to(udp, key, answer, answer_len) == 0 ? 1 : -1;
    }
    if (tun_policy_cached(policy, payload, len, question, action, 0, now_seconds(),
                          answer, sizeof answer, &answer_len))
        return reply_to(udp, key, answer, answer_len) == 0 ? 1 : -1;
    return 0;
}

static association_t *find_association(tun_udp_t *udp, assoc_kind_t kind,
                                       const tun_flow_key_t *key) {
    for (size_t i = 0; i < TUN_UDP_ASSOC_MAX; ++i) {
        association_t *candidate = &udp->associations[i];
        if (candidate->state == UDP_FREE) continue;
        /* resolvers send each query from a fresh port; one association per
           port would spend a server connection on every lookup */
        if (kind == ASSOC_DNS && candidate->kind == ASSOC_DNS) return candidate;
        if (kind != ASSOC_DNS && candidate->kind == kind &&
            key_equal(&candidate->key, key))
            return candidate;
    }
    return NULL;
}

static int on_udp(void *ctx, const tun_flow_key_t *key,
                  const uint8_t *payload, size_t len) {
    tun_udp_t *udp = ctx;
    if (!udp->running || !key ||
        (key->address_len != 4 && key->address_len != 16) ||
        !payload || len == 0 || len > VLESS_UDP_PAYLOAD_MAX)
        return refuse(udp, "a UDP datagram is empty, too large, or the relay is stopped");

    tun_policy_t *policy = udp->config.policy;
    vless_dest_t remote;
    memset(&remote, 0, sizeof remote);
    remote.atyp = key->address_len == 4 ? VLESS_ADDR_IPV4 : VLESS_ADDR_IPV6;
    memcpy(remote.host_addr, key->destination, key->address_len);
    remote.port = key->destination_port;

    assoc_kind_t kind;
    dns_question_t question;
    rule_action_t dns_action = RULE_ACTION_PROXY;
    if (key->destination_port == 53) {
        kind = ASSOC_DNS;
        if (dns_msg_parse_question(payload, len, &question) != DNS_MSG_OK)
            return refuse(udp, "a captured DNS query is malformed");
        if (policy) ++policy->stats.dns_queries;
        dns_action = tun_policy_domain(policy, question.name);
        int local = answer_locally(udp, key, payload, len, &question, dns_action);
        if (local != 0) {
            if (local < 0) return refuse(udp, "the utun output queue refused a local DNS answer");
            ++udp->stats.received;
            return 0;
        }
        /* a direct domain is still resolved through the tunnel, as pf did; the
           answer is what sends its addresses direct */
        remote.atyp = udp->config.dns_upstream_len == 4 ? VLESS_ADDR_IPV4 : VLESS_ADDR_IPV6;
        memset(remote.host_addr, 0, sizeof remote.host_addr);
        memcpy(remote.host_addr, udp->config.dns_upstream, udp->config.dns_upstream_len);
    } else {
        char why[96];
        rule_action_t action = tun_policy_flow_from(policy, key, now_seconds(), why, sizeof why);
        if (action == RULE_ACTION_BLOCK) {
            ++udp->stats.blocked;
            trace_verdict(key, "blocked", why);
            return refuse(udp, "a rule blocked a UDP destination");
        }
        if (action == RULE_ACTION_DIRECT) {
            kind = ASSOC_DIRECT;
        } else if (udp->config.proto != VL_PROTO_VLESS &&
                   udp->config.proto != VL_PROTO_VMESS) {
            return refuse(udp, "only a VLESS or VMess server carries UDP; this server"
                               " carries TCP and DNS");
        } else {
            kind = ASSOC_PROXY;
        }
        if (!find_association(udp, kind, key))
            trace_verdict(key, kind == ASSOC_DIRECT ? "direct" : "proxy", why);
    }

    if (kind != ASSOC_DIRECT && !udp->opener_running)
        return refuse(udp, "this tunnel carries no datagram through a server");

    association_t *assoc = find_association(udp, kind, key);
    int fresh = assoc == NULL;
    if (fresh) {
        for (size_t i = 0; i < TUN_UDP_ASSOC_MAX; ++i)
            if (udp->associations[i].state == UDP_FREE) {
                assoc = &udp->associations[i];
                break;
            }
    }
    if (!assoc || assoc->queue_count == TUN_UDP_QUEUE_MAX)
        return refuse(udp, "the bounded UDP association table or its send queue is full");
    if (fresh) {
        memset(assoc, 0, sizeof *assoc);
        assoc->fd = -1;
        assoc->kind = kind;
        assoc->key = *key;
        assoc->remote = remote;
        assoc->tag = ++udp->next_tag;
        if (assoc->tag == 0) assoc->tag = ++udp->next_tag;
        assoc->state = UDP_OPENING;
        assoc->opened_ms = senko_now_ms();
    }
    if (kind == ASSOC_DNS && remember_dns(udp, key, payload, len, &question, dns_action) != 0) {
        if (fresh) close_association(udp, assoc);
        return refuse(udp, "the bounded DNS query table is full");
    }
    if (queue_datagram(assoc, payload, len) != 0) {
        if (fresh) close_association(udp, assoc);
        return refuse(udp, "the UDP send queue refused a datagram");
    }
    if (fresh) {
        int opened = kind == ASSOC_DIRECT
            ? open_direct(udp, assoc)
            : (tun_opener_submit(udp->opener, assoc->tag) == TUN_OPENER_OK ? 0 : -1);
        if (opened != 0) {
            if (kind != ASSOC_DIRECT)
                set_error(udp, "the server opener refused a UDP association");
            close_association(udp, assoc);
            ++udp->stats.refused;
            return -1;
        }
    }
    assoc->touched_ms = senko_now_ms();
    ++udp->stats.received;
    /* a running association hands the datagram on now, so the small queue
       only has to cover the time the server connection opens */
    if (assoc->state == UDP_RUNNING) service(udp, assoc);
    return 0;
}

/* lookups the upstream is slow to answer: a stale entry first, then a timeout */
static void age_lookups(tun_udp_t *udp, int64_t now) {
    for (size_t i = 0; i < TUN_UDP_DNS_PENDING; ++i) {
        dns_pending_t *pending = &udp->dns[i];
        if (!pending->used) continue;
        int64_t waited = now - pending->sent_ms;
        if (waited >= TUN_UDP_DNS_STALE_MS) {
            uint8_t answer[TUN_STACK_PACKET_MAX];
            size_t answer_len = 0;
            if (tun_policy_cached(udp->config.policy, pending->id, 2,
                                  &pending->question, pending->action, 1,
                                  now_seconds(), answer, sizeof answer, &answer_len)) {
                (void)reply_to(udp, &pending->key, answer, answer_len);
                pending->used = 0;
                continue;
            }
        }
        if (waited >= TUN_UDP_DNS_WAIT_MS) {
            pending->used = 0;
            ++udp->stats.dns_timeouts;
            if (udp->config.policy) ++udp->config.policy->stats.dns_failed;
            if (senko_trace_enabled())
                fprintf(stderr, "senkod: dns %s got no answer in %d ms\n",
                        pending->question.name, TUN_UDP_DNS_WAIT_MS);
        }
    }
}

/* ---- the loop's callbacks ------------------------------------------------ */

static int on_start(void *ctx, tun_stack_t *stack, char *error, size_t error_cap) {
    tun_udp_t *udp = ctx;
    udp->stack = stack;
    if (udp->previous_hooks.start && udp->previous_hooks.start(
        udp->previous_hooks.ctx, stack, error, error_cap) != 0) return -1;
    if (!udp->loop) {
        snprintf(error, error_cap, "the UDP relay has no tunnel loop to wake");
        return -1;
    }
    /* no transport: only the traffic the rules send direct can be served */
    if (!udp->config.vt) {
        udp->running = 1;
        return 0;
    }
    if (udp->config.use_shared_tls) {
        udp->shared_tls = transport_tls_shared_create(&udp->config.tls);
        if (!udp->shared_tls) {
            snprintf(error, error_cap, "the UDP relay could not prepare TLS trust");
            return -1;
        }
        udp->config.tls.shared_ctx = udp->shared_tls;
    }
    tun_opener_config_t config;
    memset(&config, 0, sizeof config);
    config.vt = udp->config.vt;
    config.tls = udp->config.tls;
    config.dial = udp->config.dial;
    config.dial_ctx = udp->config.dial_ctx;
    config.notify = notify_loop;
    config.notify_ctx = udp;
    config.threads = udp->config.opener_threads;
    if (tun_opener_start(udp->opener, &config) != TUN_OPENER_OK) {
        snprintf(error, error_cap, "the UDP server opener could not start");
        return -1;
    }
    udp->opener_running = 1;
    udp->running = 1;
    return 0;
}

static void lower_wait(uint32_t *wait_ms, int64_t now, int64_t due) {
    int64_t left = due - now;
    if (left < 0) left = 0;
    if ((uint64_t)left < *wait_ms) *wait_ms = (uint32_t)left;
}

/* the loop sleeps until the next lookup or association runs out of time;
   everything else here arrives as a readable or writable descriptor */
static void on_prepare(void *ctx, fd_set *readable, fd_set *writable,
                       int *highest, uint32_t *wait_ms) {
    tun_udp_t *udp = ctx;
    if (udp->previous_hooks.prepare)
        udp->previous_hooks.prepare(udp->previous_hooks.ctx, readable,
                                    writable, highest, wait_ms);
    int64_t now = senko_now_ms();
    for (size_t i = 0; i < TUN_UDP_DNS_PENDING; ++i) {
        const dns_pending_t *pending = &udp->dns[i];
        if (!pending->used) continue;
        /* past the stale mark only the timeout is left to wait for */
        int64_t due = now - pending->sent_ms < TUN_UDP_DNS_STALE_MS
            ? pending->sent_ms + TUN_UDP_DNS_STALE_MS
            : pending->sent_ms + TUN_UDP_DNS_WAIT_MS;
        lower_wait(wait_ms, now, due);
    }
    for (size_t i = 0; i < TUN_UDP_ASSOC_MAX; ++i) {
        association_t *assoc = &udp->associations[i];
        if (assoc->state == UDP_FREE) continue;
        lower_wait(wait_ms, now, assoc->touched_ms + TUN_UDP_IDLE_MS);
        if (assoc->state == UDP_OPENING)
            lower_wait(wait_ms, now, assoc->opened_ms + TUN_UDP_OPEN_MS);
        if (assoc->state != UDP_RUNNING) continue;
        if (assoc->kind == ASSOC_DIRECT) {
            FD_SET(assoc->fd, readable);
            if (assoc->queue_count) FD_SET(assoc->fd, writable);
        } else {
            session_t *s = assoc->session;
            if (s->to_client_len < sizeof s->to_client) FD_SET(assoc->fd, readable);
            if ((s->to_remote_len && !s->to_remote_wait_read) ||
                (udp->config.vt->want_write && udp->config.vt->want_write(assoc->th)))
                FD_SET(assoc->fd, writable);
        }
        if (assoc->fd > *highest) *highest = assoc->fd;
    }
}

static void on_dispatch(void *ctx, const fd_set *readable,
                        const fd_set *writable) {
    tun_udp_t *udp = ctx;
    if (udp->previous_hooks.dispatch)
        udp->previous_hooks.dispatch(udp->previous_hooks.ctx, readable, writable);
    collect_opened(udp);
    int64_t now = senko_now_ms();
    age_lookups(udp, now);
    for (size_t i = 0; i < TUN_UDP_ASSOC_MAX; ++i) {
        association_t *assoc = &udp->associations[i];
        if (assoc->state == UDP_FREE) continue;
        if (now - assoc->touched_ms >= TUN_UDP_IDLE_MS ||
            (assoc->state == UDP_OPENING && now - assoc->opened_ms >= TUN_UDP_OPEN_MS)) {
            ++udp->stats.expired;
            close_association(udp, assoc);
            continue;
        }
        if (assoc->state == UDP_RUNNING &&
            (FD_ISSET(assoc->fd, readable) || FD_ISSET(assoc->fd, writable) ||
             assoc->tx_off < assoc->tx_len || assoc->queue_count))
            service(udp, assoc);
    }
}

static void on_command(void *ctx, tun_stack_t *stack,
                       const tun_loop_command_t *command) {
    tun_udp_t *udp = ctx;
    if (udp->previous_command)
        udp->previous_command(udp->previous_command_ctx, stack, command);
    if (command->type == TUN_UDP_CMD_OPENED) collect_opened(udp);
}

static void on_stop(void *ctx) {
    tun_udp_t *udp = ctx;
    udp->running = 0;
    if (udp->opener_running) {
        tun_opener_stop(udp->opener);
        udp->opener_running = 0;
    }
    for (size_t i = 0; i < TUN_UDP_ASSOC_MAX; ++i)
        if (udp->associations[i].state != UDP_FREE)
            close_association(udp, &udp->associations[i]);
    transport_tls_shared_destroy(udp->shared_tls);
    udp->shared_tls = NULL;
    udp->config.tls.shared_ctx = NULL;
    if (udp->previous_hooks.stop)
        udp->previous_hooks.stop(udp->previous_hooks.ctx);
}

int tun_udp_init(tun_udp_t *udp, const tun_udp_config_t *config,
                 tun_loop_config_t *loop_config) {
    if (!udp || !config || !loop_config || !config->vt != !config->dial ||
        (config->vt && (config->opener_threads == 0 ||
                        config->opener_threads > TUN_OPENER_THREADS_MAX)) ||
        (config->dns_upstream_len != 4 && config->dns_upstream_len != 16) ||
        loop_config->stack.udp_packet) return -1;
    memset(udp, 0, udp_part());
    udp->config = *config;
    udp->opener = (tun_opener_t *)((uint8_t *)udp + udp_part());
    udp->previous_hooks = loop_config->hooks;
    udp->previous_command = loop_config->on_command;
    udp->previous_command_ctx = loop_config->command_ctx;
    for (size_t i = 0; i < TUN_UDP_ASSOC_MAX; ++i)
        udp->associations[i].fd = -1;
    loop_config->stack.udp_packet = on_udp;
    loop_config->stack.udp_ctx = udp;
    loop_config->on_command = on_command;
    loop_config->command_ctx = udp;
    loop_config->hooks.start = on_start;
    loop_config->hooks.prepare = on_prepare;
    loop_config->hooks.dispatch = on_dispatch;
    loop_config->hooks.stop = on_stop;
    loop_config->hooks.ctx = udp;
    return 0;
}

void tun_udp_bind(tun_udp_t *udp, tun_loop_t *loop, uint64_t generation) {
    if (!udp) return;
    udp->loop = loop;
    udp->generation = generation;
}
