#define _DEFAULT_SOURCE

#include "awg_split.h"

#include "core/dns_msg.h"
#include "core/dns_policy.h"
#include "core/flow_split.h"
#include "core/senko_time.h"
#include "core/utun_frame.h"
#include "tun_loop.h"
#include "tun_tcp.h"
#include "tun_udp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#define SPLIT_GENERATION 1
/* the link's timers are checked at least this often, as awg_link_run does */
#define SPLIT_TICK_MS 1000
/* a direct flow costs a socket and a relay buffer, not a server connection, so
   the ceiling follows the stack: its 32 connection slots minus the few that
   time-wait still holds. a browser keeps idle connections open, and with eight
   relays they starved the next page and a speed test, whose streams then waited
   in the stack until it dropped their syn */
#define SPLIT_TCP_FLOWS 24

struct awg_split {
    awg_split_config_t config;
    tun_loop_t *loop;
    int loop_ready; /* tun_loop_init succeeded, so the loop owns the device */
    tun_tcp_t *tcp;
    tun_udp_t *udp;
    tun_policy_t policy;
    int policy_ready;
    flow_split_t table;
    tun_loop_hooks_t previous;
    awg_split_counts_t counts;
    char last_tcp_error[sizeof ((tun_tcp_stats_t *)0)->last_error];

    /* set on the loop thread, read once the loop has returned */
    int stop_asked;
    int failed;
    awg_link_status_t status;
    char reason[192];
};

int awg_split_wanted(const ruleset_t *rules) {
    if (!rules) return 0;
    if (rules->fallback != RULE_ACTION_PROXY) return 1;
    for (size_t i = 0; i < rules->count; ++i)
        if (rules->entries[i].action != RULE_ACTION_PROXY) return 1;
    return 0;
}

size_t awg_split_unenforced(const ruleset_t *rules) {
    size_t count = 0;
    for (size_t i = 0; rules && i < rules->count; ++i) {
        rule_type_t type = rules->entries[i].type;
        if (rules->entries[i].action == RULE_ACTION_BLOCK &&
            type != RULE_TYPE_IP_CIDR && type != RULE_TYPE_GEOIP && type != RULE_TYPE_APP)
            ++count;
    }
    return count;
}

static void fail(awg_split_t *split, awg_link_status_t status, const char *reason) {
    if (split->failed || split->stop_asked) return;
    split->failed = 1;
    split->status = status;
    snprintf(split->reason, sizeof split->reason, "%s", reason);
    tun_loop_request_stop(split->loop);
}

static rule_action_t decide(void *ctx, const tun_flow_key_t *key) {
    awg_split_t *split = ctx;
    char why[96];
    rule_action_t action = tun_policy_flow_from(&split->policy, key,
                                                (uint64_t)(senko_now_ms() / 1000),
                                                why, sizeof why);
    if (action == RULE_ACTION_PROXY) ++split->counts.tunnel_flows;
    else ++split->counts.local_flows;
    return action;
}

/* a packet read from the device: the server's, or the local stack's */
static int on_frame(void *ctx, uint8_t *frame, size_t frame_len) {
    awg_split_t *split = ctx;
    const uint8_t *packet = NULL;
    size_t packet_len = 0;
    uint8_t address_len = 0;
    /* a malformed frame is counted and explained by the stack */
    if (utun_frame_parse(frame, frame_len, &packet, &packet_len, &address_len) != UTUN_FRAME_OK)
        return 0;
    uint64_t evicted = split->table.evicted;
    flow_split_route_t route = flow_split_route(&split->table, packet, packet_len,
                                                (uint64_t)senko_now_ms(), decide, split);
    split->counts.forgotten += (uint32_t)(split->table.evicted - evicted);
    if (route == FLOW_SPLIT_LOCAL) return 0;
    if (awg_link_send_packet(split->config.link, packet, packet_len) != 0)
        fail(split, AWG_LINK_ERR_UDP, "the udp socket to the server failed");
    return 1;
}

/* the addresses a lookup answered are what lets a domain rule decide the flow
   that follows it, the way tun_udp feeds them on the stream tunnels */
static void on_inbound(void *ctx, const uint8_t *packet, size_t len) {
    awg_split_t *split = ctx;
    tun_flow_key_t key;
    size_t l4 = 0;
    if (!split->policy.map || !flow_split_key(packet, len, &key, NULL, &l4) ||
        key.protocol != TUN_FLOW_UDP || key.source_port != 53)
        return;
    const uint8_t *message = packet + l4 + 8;
    size_t message_len = len - l4 - 8;
    dns_question_t question;
    dns_response_info_t info;
    if (dns_msg_parse_response_question(message, message_len, &question) != DNS_MSG_OK ||
        dns_msg_response_info(message, message_len, &info) != DNS_MSG_OK || !info.ipv4_count)
        return;
    rule_action_t action = tun_policy_domain(&split->policy, question.name);
    dns_policy_record(split->policy.map, question.name, action, info.ipv4, info.ipv4_count,
                      (uint64_t)(senko_now_ms() / 1000), info.min_ttl);
}

static utun_device_status_t write_through_loop(void *ctx, const uint8_t *frame, size_t len) {
    awg_split_t *split = ctx;
    return tun_loop_write_frame(split->loop, frame, len);
}

static int hook_start(void *ctx, tun_stack_t *stack, char *error, size_t error_cap) {
    awg_split_t *split = ctx;
    if (split->previous.start &&
        split->previous.start(split->previous.ctx, stack, error, error_cap) != 0)
        return -1;
    char reason[96];
    if (awg_link_begin(split->config.link, reason, sizeof reason) != AWG_LINK_RUNNING) {
        snprintf(error, error_cap, "%s", reason);
        return -1;
    }
    return 0;
}

static void hook_prepare(void *ctx, fd_set *readable, fd_set *writable, int *highest,
                         uint32_t *wait_ms) {
    awg_split_t *split = ctx;
    if (split->previous.prepare)
        split->previous.prepare(split->previous.ctx, readable, writable, highest, wait_ms);
    FD_SET(split->config.link->udp_fd, readable);
    FD_SET(split->config.wake_fd, readable);
    if (split->config.link->udp_fd > *highest) *highest = split->config.link->udp_fd;
    if (split->config.wake_fd > *highest) *highest = split->config.wake_fd;
    if (*wait_ms > SPLIT_TICK_MS) *wait_ms = SPLIT_TICK_MS;
}

static void hook_dispatch(void *ctx, const fd_set *readable, const fd_set *writable) {
    awg_split_t *split = ctx;
    if (split->previous.dispatch) split->previous.dispatch(split->previous.ctx, readable, writable);
    /* a refused flow is otherwise visible only as a page that never loads, and
       its reason sits in the relay until the next one overwrites it */
    const tun_tcp_stats_t *tcp = tun_tcp_stats(split->tcp);
    split->counts.tcp_refused = (uint32_t)tcp->refused;
    split->counts.tcp_aborted = (uint32_t)tcp->aborted;
    if (tcp->last_error[0] && strcmp(tcp->last_error, split->last_tcp_error) != 0) {
        snprintf(split->last_tcp_error, sizeof split->last_tcp_error, "%s", tcp->last_error);
        fprintf(stderr, "senkod: amneziawg direct flow: %s\n", split->last_tcp_error);
    }
    if (split->failed || split->stop_asked) return;
    if (FD_ISSET(split->config.wake_fd, readable)) {
        split->stop_asked = 1;
        tun_loop_request_stop(split->loop);
        return;
    }
    char reason[192];
    awg_link_status_t status = awg_link_service(split->config.link,
                                                FD_ISSET(split->config.link->udp_fd, readable),
                                                reason, sizeof reason);
    if (status != AWG_LINK_RUNNING) fail(split, status, reason);
}

static void hook_stop(void *ctx) {
    awg_split_t *split = ctx;
    if (split->previous.stop) split->previous.stop(split->previous.ctx);
}

void awg_split_counts(const awg_split_t *split, awg_split_counts_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (split) *out = split->counts;
}

void awg_split_close(awg_split_t *split) {
    if (!split) return;
    /* a loop that never ran tun_loop_init must not be torn down as one */
    if (split->loop_ready) tun_loop_destroy(split->loop);
    free(split->loop);
    if (split->policy_ready) tun_policy_free(&split->policy);
    free(split->tcp);
    free(split->udp);
    free(split);
}

awg_split_t *awg_split_open(const awg_split_config_t *config, char *reason, size_t reason_cap) {
    if (reason && reason_cap) reason[0] = '\0';
    if (!config || !config->link || !config->device || config->device->fd < 0 || !config->rules) {
        if (reason) snprintf(reason, reason_cap, "the split tunnel was given no link, device or rules");
        return NULL;
    }
    if (config->link->udp_fd >= FD_SETSIZE || config->wake_fd >= FD_SETSIZE) {
        if (reason) snprintf(reason, reason_cap, "a tunnel descriptor is too high for select");
        return NULL;
    }
    awg_split_t *split = calloc(1, sizeof *split);
    if (!split) {
        if (reason) snprintf(reason, reason_cap, "not enough memory for the split tunnel");
        return NULL;
    }
    split->config = *config;
    flow_split_init(&split->table);
    if (tun_policy_init(&split->policy, config->rules) != TUN_POLICY_OK) {
        if (reason) snprintf(reason, reason_cap, "not enough memory for the rule tables");
        free(split);
        return NULL;
    }
    split->policy_ready = 1;
    split->policy.app_of = config->app_of;
    split->policy.app_ctx = config->app_ctx;
    split->loop = calloc(1, tun_loop_size());
    split->tcp = calloc(1, tun_tcp_size());
    split->udp = calloc(1, tun_udp_size());
    if (!split->loop || !split->tcp || !split->udp) {
        if (reason) snprintf(reason, reason_cap, "not enough memory for the split tunnel flows");
        goto failed_before_loop;
    }

    tun_loop_config_t loop;
    memset(&loop, 0, sizeof loop);
    loop.generation = SPLIT_GENERATION;
    loop.stack.mtu = config->mtu;
    loop.stack.have_ipv4 = loop.stack.have_ipv6 = 1;
    loop.stack.address4[0] = loop.stack.synthetic4[0] = 198;
    loop.stack.address4[1] = loop.stack.synthetic4[1] = 18;
    loop.stack.address4[3] = 1;
    loop.stack.synthetic4[3] = 2;
    memset(loop.stack.netmask4, 255, 4);
    loop.stack.address6[0] = loop.stack.synthetic6[0] = 0xfd;
    loop.stack.address6[2] = loop.stack.synthetic6[2] = 0x5e;
    loop.stack.address6[3] = loop.stack.synthetic6[3] = 0x4b;
    loop.stack.address6[15] = 1;
    loop.stack.synthetic6[15] = 2;
    snprintf(loop.stack.ifname, sizeof loop.stack.ifname, "%s", config->device->ifname);
    loop.retain_device_until_destroy = 1;

    tun_tcp_config_t tcp;
    memset(&tcp, 0, sizeof tcp);
    tcp.policy = &split->policy;
    tcp.direct_socket = direct_socket_bound;
    tcp.direct_ctx = &split->config.direct;
    tcp.max_flows = SPLIT_TCP_FLOWS;
    tun_udp_config_t udp;
    memset(&udp, 0, sizeof udp);
    udp.policy = &split->policy;
    udp.direct_socket = direct_socket_bound;
    udp.direct_ctx = &split->config.direct;
    memcpy(udp.dns_upstream, config->dns, 4);
    udp.dns_upstream_len = 4;
    if (tun_tcp_init(split->tcp, &tcp, &loop) != TUN_TCP_OK ||
        tun_udp_init(split->udp, &udp, &loop) != 0) {
        if (reason) snprintf(reason, reason_cap, "the direct flow relays could not be set up");
        goto failed_before_loop;
    }
    split->previous = loop.hooks;
    loop.hooks.start = hook_start;
    loop.hooks.prepare = hook_prepare;
    loop.hooks.dispatch = hook_dispatch;
    loop.hooks.stop = hook_stop;
    loop.hooks.ctx = split;
    loop.frame_filter = on_frame;
    loop.filter_ctx = split;

    /* tun_loop_init takes the device and closes it itself when it fails */
    if (tun_loop_init(split->loop, &loop, config->device) != TUN_LOOP_OK) {
        if (reason) snprintf(reason, reason_cap, "the packet loop could not be initialised");
        goto failed_before_loop;
    }
    split->loop_ready = 1;
    tun_tcp_bind(split->tcp, split->loop, SPLIT_GENERATION);
    tun_udp_bind(split->udp, split->loop, SPLIT_GENERATION);
    split->config.link->write_frame = write_through_loop;
    split->config.link->write_ctx = split;
    split->config.link->on_inbound = on_inbound;
    split->config.link->inbound_ctx = split;
    return split;

failed_before_loop:
    awg_split_close(split);
    return NULL;
}

awg_link_status_t awg_split_run(awg_split_t *split, char *reason, size_t reason_cap) {
    if (reason && reason_cap) reason[0] = '\0';
    tun_loop_result_t result;
    tun_loop_status_t status = tun_loop_run(split->loop, &result);
    if (split->failed) {
        if (reason && reason_cap) snprintf(reason, reason_cap, "%s", split->reason);
        return split->status;
    }
    if (status == TUN_LOOP_OK && result.end == TUN_LOOP_ENDED_STOP) return AWG_LINK_STOPPED;
    if (reason && reason_cap) snprintf(reason, reason_cap, "%s", result.message);
    return AWG_LINK_ERR_DEVICE;
}
