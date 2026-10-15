#include "routing_sync.h"

#include <stdio.h>
#include <string.h>

static void refuse(routing_sync_t *out, const char *why) {
    out->kind = ROUTING_SYNC_REFUSED;
    snprintf(out->message, sizeof out->message, "routing profile refused: %s", why);
}

void routing_sync_apply(store_t *st, size_t sub_index, const char *header,
                        int header_too_long, const char *body, size_t body_len,
                        routing_sync_t *out) {
    static char json[ROUTING_JSON_MAX + 1];
    routing_cmd_t cmd = ROUTING_CMD_ONADD;
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!st || sub_index >= STORE_MAX_SUBS || !st->subs[sub_index].used) {
        refuse(out, "no such subscription");
        return;
    }
    if (header_too_long && (!header || !header[0])) {
        refuse(out, "the routing header is too long");
        return;
    }
    routing_status_t found = routing_find(header, body, body_len, &cmd, json, sizeof json);
    if (found == ROUTING_ERR_NONE) {
        out->kind = ROUTING_SYNC_NONE;
        out->activated = st->routing_active == (int)sub_index;
        return;
    }
    if (found == ROUTING_ERR_TOOBIG) {
        refuse(out, "the profile is too large");
        return;
    }
    if (found != ROUTING_OK) {
        refuse(out, "the link is not a happ routing profile");
        return;
    }
    if (cmd == ROUTING_CMD_OFF) {
        if (!st->subs[sub_index].routing[0]) {
            out->kind = ROUTING_SYNC_NONE;
            return;
        }
        (void)store_set_sub_routing(st, sub_index, "", 0);
        out->kind = ROUTING_SYNC_OFF;
        snprintf(out->message, sizeof out->message, "the subscription switched its routing off");
        return;
    }
    size_t len = strlen(json);
    if (strcmp(st->subs[sub_index].routing, json) == 0) {
        out->kind = ROUTING_SYNC_UNCHANGED;
    } else {
        store_status_t s = store_set_sub_routing(st, sub_index, json, len);
        if (s == STORE_ERR_PARSE) {
            refuse(out, "it is not a routing profile");
            return;
        }
        if (s != STORE_OK) {
            refuse(out, "the profile is too large");
            return;
        }
        out->kind = ROUTING_SYNC_SET;
    }
    if (cmd == ROUTING_CMD_ONADD || st->routing_active < 0) {
        /* a stored profile always has a json, so this cannot be refused */
        (void)store_set_routing_active(st, (int)sub_index);
    }
    out->activated = st->routing_active == (int)sub_index;
}
