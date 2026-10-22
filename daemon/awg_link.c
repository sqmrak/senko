#define _DEFAULT_SOURCE

#include "awg_link.h"

#include "core/senko_time.h"

#include <errno.h>
#include <openssl/crypto.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* one pass reads at most this many packets from each side, so a flood on one
   side cannot starve the other or the timers */
#define AWG_LINK_BURST 64

static void set_reason(char *reason, size_t cap, const char *fmt, int value) {
    if (reason && cap) snprintf(reason, cap, fmt, value);
}

void awg_link_init(awg_link_t *link, const awg_config_t *cfg, utun_device_t *device,
                   int udp_fd, int wake_fd, const awg_handshake_t *established,
                   int64_t now_ms) {
    memset(link, 0, sizeof *link);
    link->cfg = cfg;
    link->device = device;
    link->udp_fd = udp_fd;
    link->wake_fd = wake_fd;
    awg_tunnel_init(&link->current, cfg);
    if (established) link->current.handshake = *established;
    link->handshake_ms = now_ms;
    link->last_tx_ms = now_ms;
    link->last_rx_ms = now_ms;
}

static int pick_timers(awg_link_t *link) {
    uint16_t seconds = 0;
    if (awg_range_pick(link->cfg->timers[AWG_TIMER_REKEY_AFTER],
                       AWG_LINK_REKEY_AFTER_MS / 1000, &seconds) != 0) return -1;
    link->rekey_after_ms = (int64_t)seconds * 1000;
    if (awg_range_pick(link->cfg->timers[AWG_TIMER_REKEY_TIMEOUT],
                       AWG_LINK_REKEY_RETRY_MS / 1000, &seconds) != 0) return -1;
    link->rekey_retry_ms = (int64_t)seconds * 1000;
    if (awg_range_pick(link->cfg->timers[AWG_TIMER_REJECT_AFTER],
                       AWG_LINK_REJECT_AFTER_MS / 1000, &seconds) != 0) return -1;
    link->reject_after_ms = (int64_t)seconds * 1000;
    if (awg_range_pick(link->cfg->timers[AWG_TIMER_KEEPALIVE], 10, &seconds) != 0)
        return -1;
    link->keepalive_timeout_ms = (int64_t)seconds * 1000;
    if (awg_range_pick(link->cfg->timers[AWG_TIMER_MAX_ATTEMPTS], 18,
                       &link->max_handshake_attempts) != 0) return -1;
    link->rekey_attempts = 0;
    return 0;
}

void awg_link_clear(awg_link_t *link) {
    if (!link) return;
    OPENSSL_cleanse(&link->current, sizeof link->current);
    OPENSSL_cleanse(&link->previous, sizeof link->previous);
    OPENSSL_cleanse(&link->pending, sizeof link->pending);
    link->have_previous = 0;
    link->rekey_pending = 0;
}

/* errors a moving or briefly absent network produces. the packet is lost,
   the tunnel is not: the timers decide when a silent server ends it */
static int transient_udp_error(int err) {
    return err == EAGAIN || err == EWOULDBLOCK || err == EINTR || err == ENOBUFS ||
           err == EHOSTUNREACH || err == ENETUNREACH || err == ENETDOWN ||
           err == EHOSTDOWN || err == ECONNREFUSED || err == EADDRNOTAVAIL;
}

/* 0 when sent or dropped for a transient reason, -1 when the socket is dead */
static int send_datagram(awg_link_t *link, const uint8_t *data, size_t len) {
    ssize_t sent = send(link->udp_fd, data, len, 0);
    if (sent == (ssize_t)len) {
        link->last_tx_ms = senko_now_ms();
        return 0;
    }
    if (sent >= 0 || transient_udp_error(errno)) {
        ++link->dropped;
        return 0;
    }
    return -1;
}

static int send_keepalive(awg_link_t *link) {
    size_t len = 0;
    if (awg_tunnel_seal(&link->current, NULL, 0, link->wire, sizeof link->wire,
                        &len) != AWG_TUN_OK) {
        ++link->dropped;
        return 0;
    }
    return send_datagram(link, link->wire, len);
}

static void add_bytes(awg_link_t *link, uint64_t up, uint64_t down) {
    if (!up && !down) return;
    if (link->stats_lock) pthread_mutex_lock(link->stats_lock);
    link->bytes_up += up;
    link->bytes_down += down;
    if (link->stats_lock) pthread_mutex_unlock(link->stats_lock);
}

/* 0 when the packet was sent or dropped for a reason that spares the link, -1
   when the udp socket is dead */
static int seal_and_send(awg_link_t *link, const uint8_t *packet, size_t packet_len) {
    size_t wire_len = 0;
    if (awg_tunnel_seal(&link->current, packet, packet_len, link->wire,
                        sizeof link->wire, &wire_len) != AWG_TUN_OK) {
        ++link->dropped;
        return 0;
    }
    return send_datagram(link, link->wire, wire_len);
}

int awg_link_send_packet(awg_link_t *link, const uint8_t *packet, size_t packet_len) {
    if (seal_and_send(link, packet, packet_len) != 0) return -1;
    add_bytes(link, packet_len, 0);
    return 0;
}

/* device to server. -1 when the device failed */
static int pump_device(awg_link_t *link, uint64_t *up, int *udp_dead) {
    for (int i = 0; i < AWG_LINK_BURST; ++i) {
        size_t frame_len = 0;
        utun_device_status_t st = utun_device_read_frame(link->device, link->frame,
                                                         sizeof link->frame, &frame_len);
        if (st == UTUN_DEVICE_AGAIN) return 0;
        if (st != UTUN_DEVICE_OK) return -1;
        const uint8_t *packet = NULL;
        size_t packet_len = 0;
        uint8_t address_len = 0;
        if (utun_frame_parse(link->frame, frame_len, &packet, &packet_len,
                             &address_len) != UTUN_FRAME_OK) {
            ++link->dropped;
            continue;
        }
        if (seal_and_send(link, packet, packet_len) != 0) {
            *udp_dead = 1;
            return 0;
        }
        *up += packet_len;
    }
    return 0;
}

static int accept_renewal(awg_link_t *link, const awg_handshake_t *answered) {
    if (pick_timers(link) != 0) return -1;
    OPENSSL_cleanse(&link->previous, sizeof link->previous);
    link->previous = link->current;
    link->have_previous = 1;
    awg_tunnel_init(&link->current, link->cfg);
    link->current.handshake = *answered;
    link->handshake_ms = senko_now_ms();
    link->rekey_pending = 0;
    OPENSSL_cleanse(&link->pending, sizeof link->pending);
    ++link->renewals;
    return 0;
}

/* server to device. -1 when the socket or the device failed */
static int pump_udp(awg_link_t *link, uint64_t *down, int *device_dead) {
    for (int i = 0; i < AWG_LINK_BURST; ++i) {
        ssize_t got = recv(link->udp_fd, link->wire, sizeof link->wire, 0);
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            if (transient_udp_error(errno)) continue;
            return -1;
        }
        if (link->rekey_pending) {
            awg_handshake_t trial = link->pending;
            awg_hs_status_t hs = awg_handshake_consume_response(&trial, link->wire,
                                                                (size_t)got);
            if (hs == AWG_HS_OK && trial.established) {
                if (accept_renewal(link, &trial) != 0) {
                    OPENSSL_cleanse(&trial, sizeof trial);
                    return -1;
                }
                OPENSSL_cleanse(&trial, sizeof trial);
                /* the server starts using the new keys only once it has seen
                   a packet sealed with them */
                if (send_keepalive(link) != 0) return -1;
                continue;
            }
            OPENSSL_cleanse(&trial, sizeof trial);
        }
        size_t inner_len = 0;
        uint8_t *inner = link->frame + UTUN_FRAME_HEADER_LEN;
        size_t inner_cap = sizeof link->frame - UTUN_FRAME_HEADER_LEN;
        awg_tun_status_t tr = awg_tunnel_open(&link->current, link->wire, (size_t)got,
                                              inner, inner_cap, &inner_len);
        if (tr == AWG_TUN_ERR_FORMAT && link->have_previous)
            tr = awg_tunnel_open(&link->previous, link->wire, (size_t)got,
                                 inner, inner_cap, &inner_len);
        if (tr != AWG_TUN_OK) {
            ++link->dropped;
            continue;
        }
        link->last_rx_ms = senko_now_ms();
        if (!inner_len) continue; /* keepalive */
        if (utun_frame_header(inner[0], link->frame) != UTUN_FRAME_OK) {
            ++link->dropped;
            continue;
        }
        if (link->on_inbound) link->on_inbound(link->inbound_ctx, inner, inner_len);
        utun_device_status_t st = link->write_frame
            ? link->write_frame(link->write_ctx, link->frame, UTUN_FRAME_HEADER_LEN + inner_len)
            : utun_device_write_frame(link->device, link->frame,
                                      UTUN_FRAME_HEADER_LEN + inner_len);
        if (st == UTUN_DEVICE_OK) {
            *down += inner_len;
        } else if (st == UTUN_DEVICE_AGAIN || st == UTUN_DEVICE_ERR_SHORT) {
            ++link->dropped;
        } else {
            *device_dead = 1;
            return 0;
        }
    }
    return 0;
}

static void start_renewal(awg_link_t *link, int64_t now) {
    awg_handshake_init(&link->pending, link->cfg);
    char why[96];
    why[0] = '\0';
    if (awg_handshake_send_initiation(link->udp_fd, &link->pending, why, sizeof why) == AWG_HS_OK)
        link->rekey_pending = 1;
    else
        fprintf(stderr, "senkod: amneziawg key renewal not sent: %s\n",
                why[0] ? why : "udp send failed");
    link->rekey_sent_ms = now;
    ++link->rekey_attempts;
}

awg_link_status_t awg_link_begin(awg_link_t *link, char *reason, size_t reason_cap) {
    if (reason && reason_cap) reason[0] = '\0';
    if (pick_timers(link) != 0) {
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "amneziawg timer random generation failed");
        return AWG_LINK_ERR_EXPIRED;
    }
    /* a responder starts sending on new keys only after a packet sealed with
       them arrives, so a link with nothing to send would leave the server's
       first packets waiting for a timer */
    if (send_keepalive(link) != 0) {
        set_reason(reason, reason_cap, "the udp socket to the server failed (errno %d)", errno);
        return AWG_LINK_ERR_UDP;
    }
    return AWG_LINK_RUNNING;
}

/* key renewal, expiry and keepalives; the part of a pass that needs no descriptor */
static awg_link_status_t run_timers(awg_link_t *link, char *reason, size_t reason_cap) {
    int64_t now = senko_now_ms();
    if (now - link->handshake_ms >= link->reject_after_ms) {
        set_reason(reason, reason_cap,
                   "the server has not answered a new handshake for %d s",
                   (int)(link->reject_after_ms / 1000));
        return AWG_LINK_ERR_EXPIRED;
    }
    if (now - link->handshake_ms >= link->rekey_after_ms &&
        (!link->rekey_pending || now - link->rekey_sent_ms >= link->rekey_retry_ms) &&
        link->rekey_attempts < link->max_handshake_attempts)
        start_renewal(link, now);
    if (link->rekey_attempts >= link->max_handshake_attempts &&
        now - link->rekey_sent_ms >= link->rekey_retry_ms) {
        set_reason(reason, reason_cap, "amneziawg handshake failed after %d attempts",
                   (int)link->max_handshake_attempts);
        return AWG_LINK_ERR_EXPIRED;
    }
    if (link->cfg->timers[AWG_TIMER_KEEPALIVE].max &&
        now - link->last_rx_ms < link->keepalive_timeout_ms &&
        now - link->last_tx_ms >= link->keepalive_timeout_ms &&
        send_keepalive(link) != 0) {
        set_reason(reason, reason_cap, "the udp socket to the server failed (errno %d)", errno);
        return AWG_LINK_ERR_UDP;
    }
    if (link->cfg->persistent_keepalive &&
        now - link->last_tx_ms >= (int64_t)link->cfg->persistent_keepalive * 1000 &&
        send_keepalive(link) != 0) {
        set_reason(reason, reason_cap, "the udp socket to the server failed (errno %d)", errno);
        return AWG_LINK_ERR_UDP;
    }
    return AWG_LINK_RUNNING;
}

awg_link_status_t awg_link_service(awg_link_t *link, int udp_readable,
                                   char *reason, size_t reason_cap) {
    if (reason && reason_cap) reason[0] = '\0';
    awg_link_status_t timers = run_timers(link, reason, reason_cap);
    if (timers != AWG_LINK_RUNNING) return timers;
    if (udp_readable) {
        uint64_t down = 0;
        int device_dead = 0;
        int udp_dead = pump_udp(link, &down, &device_dead) != 0;
        add_bytes(link, 0, down);
        if (device_dead) {
            set_reason(reason, reason_cap, "the utun device failed (errno %d)",
                       link->device->last_errno);
            return AWG_LINK_ERR_DEVICE;
        }
        if (udp_dead) {
            set_reason(reason, reason_cap, "the udp socket to the server failed (errno %d)", errno);
            return AWG_LINK_ERR_UDP;
        }
    }
    return AWG_LINK_RUNNING;
}

awg_link_status_t awg_link_run(awg_link_t *link, char *reason, size_t reason_cap) {
    awg_link_status_t begun = awg_link_begin(link, reason, reason_cap);
    if (begun != AWG_LINK_RUNNING) return begun;
    for (;;) {
        awg_link_status_t timers = run_timers(link, reason, reason_cap);
        if (timers != AWG_LINK_RUNNING) return timers;

        struct pollfd pfd[3];
        pfd[0].fd = link->device->fd; pfd[0].events = POLLIN; pfd[0].revents = 0;
        pfd[1].fd = link->udp_fd;     pfd[1].events = POLLIN; pfd[1].revents = 0;
        pfd[2].fd = link->wake_fd;    pfd[2].events = POLLIN; pfd[2].revents = 0;
        int pr = poll(pfd, 3, 1000);
        if (pr < 0 && errno == EINTR) continue;
        if (pr < 0) {
            set_reason(reason, reason_cap, "poll failed (errno %d)", errno);
            return AWG_LINK_ERR_DEVICE;
        }
        if (pfd[2].revents) return AWG_LINK_STOPPED;

        uint64_t up = 0, down = 0;
        int udp_dead = 0, device_dead = 0;
        if (pfd[0].revents & (POLLIN | POLLERR | POLLHUP) &&
            pump_device(link, &up, &udp_dead) != 0)
            device_dead = 1;
        if (!device_dead && !udp_dead && pfd[1].revents & (POLLIN | POLLERR | POLLHUP) &&
            pump_udp(link, &down, &device_dead) != 0)
            udp_dead = 1;
        add_bytes(link, up, down);
        if (device_dead) {
            set_reason(reason, reason_cap, "the utun device failed (errno %d)",
                       link->device->last_errno);
            return AWG_LINK_ERR_DEVICE;
        }
        if (udp_dead) {
            set_reason(reason, reason_cap, "the udp socket to the server failed (errno %d)", errno);
            return AWG_LINK_ERR_UDP;
        }
    }
}
