#include "ctl_engine.h"

#include <stdio.h>
#include <string.h>

void ctl_engine_init(ctl_engine_t *e) {
    if (!e) return;
    memset(e, 0, sizeof *e);
    store_init(&e->store);
    e->state = CTL_STATE_IDLE;
}

ctl_status_t ctl_engine_handle(ctl_engine_t *e, const ctl_cmd_t *cmd,
                               char *out, size_t cap, size_t *out_len,
                               ctl_action_t *action) {
    if (!e || !cmd || !out || !out_len || !action) return CTL_ERR_ARG;
    *out_len = 0;
    action->kind = CTL_ACT_NONE;
    action->server_index = -1;

    switch (cmd->kind) {
        case CTL_CMD_CONNECT: {
/* reject a bad index so the ui cannot enter a false state */
            if (store_select(&e->store, cmd->server_index) != STORE_OK) {
                return ctl_build_err("no such server", out, cap, out_len);
            }
            const vl_server_t *srv = store_selected(&e->store);
            if (!srv) return ctl_build_err("no such server", out, cap, out_len);

/* publish connecting before the asynchronous transport result */
            e->state = CTL_STATE_CONNECTING;
            action->kind = CTL_ACT_START;
            action->server_index = cmd->server_index;
            action->server = *srv;
            return ctl_build_state(CTL_STATE_CONNECTING, out, cap, out_len);
        }

        case CTL_CMD_DISCONNECT: {
            e->state = CTL_STATE_IDLE;
            action->kind = CTL_ACT_STOP;
            return ctl_build_state(CTL_STATE_IDLE, out, cap, out_len);
        }

        case CTL_CMD_STATUS:
            return ctl_build_state(e->state, out, cap, out_len);

        case CTL_CMD_GET_SERVER: {
            char link[2048];
            if (store_link_at(&e->store, (size_t)cmd->server_index,
                              link, sizeof link) != 0)
                return ctl_build_err("no such server", out, cap, out_len);
            return ctl_build_link(cmd->server_index, link, out, cap, out_len);
        }

        case CTL_CMD_PING: {
/* defer measurement so ping does not mutate tunnel state */
            const vl_server_t *srv = NULL;
            if (cmd->server_index >= 0 &&
                (size_t)cmd->server_index < e->store.n) {
                srv = &e->store.servers[cmd->server_index];
            }
            if (!srv) return ctl_build_err("no such server", out, cap, out_len);
            action->kind = CTL_ACT_PING;
            action->server_index = cmd->server_index;
            action->server = *srv;
            *out_len = 0; /* no immediate event because pong is async */
            return CTL_OK;
        }

        case CTL_CMD_ADD_SERVER: {
/* mutate only the store so the daemon can persist a successful result */
            size_t idx;
            store_status_t r = store_add_manual(&e->store, cmd->text, &idx);
            if (r == STORE_ERR_PARSE)
                return ctl_build_err("bad vless link", out, cap, out_len);
            if (r == STORE_ERR_UNSUPPORTED) {
                char reason[128];
                if (!cfg_validate_link(cmd->text, reason, sizeof reason))
                    return ctl_build_err(reason, out, cap, out_len);
                return ctl_build_err("unsupported server", out, cap, out_len);
            }
            if (r == STORE_ERR_FULL)
                return ctl_build_err("server list full", out, cap, out_len);
            if (r == STORE_ERR_EXISTS)
                return ctl_build_err("server already exists", out, cap, out_len);
            if (r != STORE_OK)
                return ctl_build_err("could not add server", out, cap, out_len);
            char msg[64];
            snprintf(msg, sizeof msg, "added server %zu", idx);
            return ctl_build_ok(msg, out, cap, out_len);
        }

        case CTL_CMD_DEL_SERVER: {
/* stop the live tunnel first so routing never targets a deleted entry */
            if (cmd->server_index < 0 || (size_t)cmd->server_index >= e->store.n)
                return ctl_build_err("no such server", out, cap, out_len);
            int need_stop = (cmd->server_index == e->store.selected &&
                (e->state == CTL_STATE_CONNECTED ||
                 e->state == CTL_STATE_CONNECTING));
            if (store_remove(&e->store, (size_t)cmd->server_index) != STORE_OK)
                return ctl_build_err("could not remove server", out, cap, out_len);
            if (!need_stop)
                return ctl_build_ok("removed server", out, cap, out_len);

            e->state = CTL_STATE_IDLE;
            action->kind = CTL_ACT_STOP;
            char st[64]; size_t sn = 0;
            char okln[128]; size_t okn = 0;
            if (ctl_build_state(CTL_STATE_IDLE, st, sizeof st, &sn) != CTL_OK)
                return CTL_ERR_BUF;
            if (ctl_build_ok("removed server", okln, sizeof okln, &okn) != CTL_OK)
                return CTL_ERR_BUF;
            if (sn + okn > cap) return CTL_ERR_BUF;
            memcpy(out, st, sn);
            memcpy(out + sn, okln, okn);
            *out_len = sn + okn;
            return CTL_OK;
        }

        case CTL_CMD_ADD_SUB: {
            size_t sub;
            store_status_t r = store_add_sub(&e->store, cmd->name, cmd->text, &sub);
            if (r == STORE_ERR_FULL)
                return ctl_build_err("subscription list full", out, cap, out_len);
            if (r == STORE_ERR_TOO_LONG)
                return ctl_build_err("subscription url too long", out, cap, out_len);
            if (r != STORE_OK)
                return ctl_build_err("could not add subscription", out, cap, out_len);
/* expose the subscription index so the ui can request its first refresh */
            char msg[64];
            snprintf(msg, sizeof msg, "added subscription %zu", sub);
            return ctl_build_ok(msg, out, cap, out_len);
        }

        case CTL_CMD_DEL_SUB: {
            int si = cmd->server_index;
            if (si < 0 || si >= STORE_MAX_SUBS || !e->store.subs[si].used)
                return ctl_build_err("no such subscription", out, cap, out_len);
            int need_stop = 0;
            if (e->store.selected >= 0 &&
                (size_t)e->store.selected < e->store.n &&
                e->store.group[e->store.selected] == si &&
                (e->state == CTL_STATE_CONNECTED ||
                 e->state == CTL_STATE_CONNECTING))
                need_stop = 1;
            if (store_remove_sub(&e->store, (size_t)si) != STORE_OK)
                return ctl_build_err("could not remove subscription", out, cap, out_len);
            if (!need_stop)
                return ctl_build_ok("removed subscription", out, cap, out_len);
            e->state = CTL_STATE_IDLE;
            action->kind = CTL_ACT_STOP;
            char st[64]; size_t sn = 0;
            char okln[128]; size_t okn = 0;
            if (ctl_build_state(CTL_STATE_IDLE, st, sizeof st, &sn) != CTL_OK)
                return CTL_ERR_BUF;
            if (ctl_build_ok("removed subscription", okln, sizeof okln, &okn) != CTL_OK)
                return CTL_ERR_BUF;
            if (sn + okn > cap) return CTL_ERR_BUF;
            memcpy(out, st, sn);
            memcpy(out + sn, okln, okn);
            *out_len = sn + okn;
            return CTL_OK;
        }

        case CTL_CMD_REFRESH: {
/* validate the sub index and defer network refresh to the daemon */
            int si = cmd->server_index;
            if (si < 0 || si >= STORE_MAX_SUBS || !e->store.subs[si].used)
                return ctl_build_err("no such subscription", out, cap, out_len);
            action->kind = CTL_ACT_REFRESH;
            action->server_index = si;
            *out_len = 0; /* no immediate reply because daemon fetches async */
            return CTL_OK;
        }

        case CTL_CMD_NONE:
        default:
            return ctl_build_err("unknown command", out, cap, out_len);
    }
}

ctl_status_t ctl_engine_notify(ctl_engine_t *e, ctl_state_t new_state,
                               char *out, size_t cap, size_t *out_len) {
    if (!e || !out || !out_len) return CTL_ERR_ARG;
    e->state = new_state;
    return ctl_build_state(new_state, out, cap, out_len);
}
