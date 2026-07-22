#include "control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t trim_eol(const char *line, size_t len) {
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) --len;
    return len;
}

/* match a command verb */
static int verb_is(const char *line, size_t len, const char *verb,
                   const char **rest, size_t *rest_len) {
    size_t vl = strlen(verb);
    if (len < vl) return 0;
    if (memcmp(line, verb, vl) != 0) return 0;
    if (len == vl) { /* accept a bare verb */
        *rest = line + vl;
        *rest_len = 0;
        return 1;
    }
    if (line[vl] != ' ') return 0; /* require a word boundary */
    const char *r = line + vl + 1;
    *rest = r;
    *rest_len = len - vl - 1;
    return 1;
}

static int parse_int_span(const char *s, size_t len, int *out) {
    if (len == 0 || len > 11) return -1; /* cap the number */
    char tmp[12];
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    char *end = NULL;
    long v = strtol(tmp, &end, 10);
    if (end != tmp + len) return -1; /* reject trailing text */
    *out = (int)v;
    return 0;
}

ctl_status_t ctl_parse_cmd(const char *line, size_t len, ctl_cmd_t *out) {
    if (!line || !out) return CTL_ERR_ARG;
    out->kind = CTL_CMD_NONE;
    out->server_index = -1;
    out->target_index = -1;
    out->text[0] = '\0';
    out->name[0] = '\0';

    len = trim_eol(line, len);

    const char *rest; size_t rl;
    if (verb_is(line, len, "CONNECT", &rest, &rl)) {
        int idx;
        if (parse_int_span(rest, rl, &idx) != 0) return CTL_ERR_PARSE;
        out->kind = CTL_CMD_CONNECT;
        out->server_index = idx;
        return CTL_OK;
    }
    if (verb_is(line, len, "PING", &rest, &rl)) {
        int idx;
        if (parse_int_span(rest, rl, &idx) != 0) return CTL_ERR_PARSE;
        out->kind = CTL_CMD_PING;
        out->server_index = idx;
        return CTL_OK;
    }
    if (verb_is(line, len, "REFRESH", &rest, &rl)) {
        int idx;
        if (parse_int_span(rest, rl, &idx) != 0) return CTL_ERR_PARSE;
        out->kind = CTL_CMD_REFRESH;
        out->server_index = idx; /* store the subscription index */
        return CTL_OK;
    }
    if (verb_is(line, len, "ADDSRV", &rest, &rl)) {
        if (rl == 0 || rl >= sizeof out->text) return CTL_ERR_PARSE;
        memcpy(out->text, rest, rl);
        out->text[rl] = '\0';
        out->kind = CTL_CMD_ADD_SERVER;
        return CTL_OK;
    }
    if (verb_is(line, len, "ADDSUB", &rest, &rl)) {
/* read the url and the rest as the name */
        const char *sp = memchr(rest, ' ', rl);
        if (!sp) return CTL_ERR_PARSE; /* require url and name */
        size_t ul = (size_t)(sp - rest);
        size_t nl = rl - ul - 1;
        if (ul == 0 || ul >= sizeof out->text) return CTL_ERR_PARSE;
        if (nl == 0 || nl >= sizeof out->name) return CTL_ERR_PARSE;
        memcpy(out->text, rest, ul); out->text[ul] = '\0';
        memcpy(out->name, sp + 1, nl); out->name[nl] = '\0';
        out->kind = CTL_CMD_ADD_SUB;
        return CTL_OK;
    }
    if (verb_is(line, len, "DELSRV", &rest, &rl) && rl > 0) {
        out->kind = CTL_CMD_DEL_SERVER;
        int idx = -1;
        if (parse_int_span(rest, rl, &idx) != 0 || idx < 0) return CTL_ERR_PARSE;
        out->server_index = idx;
        return CTL_OK;
    }
    if (verb_is(line, len, "DELSUB", &rest, &rl) && rl > 0) {
        out->kind = CTL_CMD_DEL_SUB;
        int idx = -1;
        if (parse_int_span(rest, rl, &idx) != 0 || idx < 0) return CTL_ERR_PARSE;
        out->server_index = idx; /* store the subscription index */
        return CTL_OK;
    }
    if (verb_is(line, len, "DISCONNECT", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_DISCONNECT;
        return CTL_OK;
    }
    if (verb_is(line, len, "STATUS", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_STATUS;
        return CTL_OK;
    }
    if (verb_is(line, len, "LIST", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_LIST;
        return CTL_OK;
    }
    if (verb_is(line, len, "GETSRV", &rest, &rl)) {
        int idx;
        if (parse_int_span(rest, rl, &idx) != 0 || idx < 0) return CTL_ERR_PARSE;
        out->kind = CTL_CMD_GET_SERVER;
        out->server_index = idx;
        return CTL_OK;
    }
    if (verb_is(line, len, "FETCH", &rest, &rl)) {
        if (rl == 0 || rl >= sizeof out->text) return CTL_ERR_PARSE;
        memcpy(out->text, rest, rl);
        out->text[rl] = '\0';
        out->kind = CTL_CMD_FETCH;
        return CTL_OK;
    }
    if (verb_is(line, len, "AUTH", &rest, &rl)) {
        if (rl == 0 || rl >= sizeof out->text) return CTL_ERR_PARSE;
        memcpy(out->text, rest, rl);
        out->text[rl] = '\0';
        out->kind = CTL_CMD_AUTH;
        return CTL_OK;
    }
    if (verb_is(line, len, "MOVESECTION", &rest, &rl)) {
        const char *sp = memchr(rest, ' ', rl);
        int section_id = 0, to_pos = 0;
        if (!sp || parse_int_span(rest, (size_t)(sp - rest), &section_id) != 0 ||
            parse_int_span(sp + 1, rl - (size_t)(sp - rest) - 1, &to_pos) != 0 ||
            to_pos < 0)
            return CTL_ERR_PARSE;
        out->kind = CTL_CMD_MOVE_SECTION;
        out->server_index = section_id;
        out->target_index = to_pos;
        return CTL_OK;
    }
    if (verb_is(line, len, "MOVEMANUAL", &rest, &rl)) {
        const char *sp = memchr(rest, ' ', rl);
        int server_index = 0, to_pos = 0;
        if (!sp || parse_int_span(rest, (size_t)(sp - rest), &server_index) != 0 ||
            parse_int_span(sp + 1, rl - (size_t)(sp - rest) - 1, &to_pos) != 0 ||
            server_index < 0 || to_pos < 0)
            return CTL_ERR_PARSE;
        out->kind = CTL_CMD_MOVE_MANUAL;
        out->server_index = server_index;
        out->target_index = to_pos;
        return CTL_OK;
    }
    return CTL_ERR_PARSE;
}

static ctl_status_t finish(int written, size_t cap, size_t *n) {
    if (written < 0) return CTL_ERR_ARG;
    if ((size_t)written >= cap) return CTL_ERR_BUF; /* cap output */
    if (n) *n = (size_t)written;
    return CTL_OK;
}

ctl_status_t ctl_build_connect(int idx, char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "CONNECT %d\n", idx), cap, n);
}

ctl_status_t ctl_build_disconnect(char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "DISCONNECT\n"), cap, n);
}

ctl_status_t ctl_build_ping(int idx, char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "PING %d\n", idx), cap, n);
}

ctl_status_t ctl_build_add_server(const char *link, char *buf, size_t cap, size_t *n) {
    if (!buf || !link) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "ADDSRV %s\n", link), cap, n);
}

ctl_status_t ctl_build_add_sub(const char *url, const char *name,
                               char *buf, size_t cap, size_t *n) {
    if (!buf || !url || !name) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "ADDSUB %s %s\n", url, name), cap, n);
}

ctl_status_t ctl_build_refresh(int sub_index, char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "REFRESH %d\n", sub_index), cap, n);
}

ctl_status_t ctl_build_state(ctl_state_t st, char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "STATE %s\n", ctl_state_name(st)), cap, n);
}

ctl_status_t ctl_build_pong(int idx, int ms, char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "PONG %d %d\n", idx, ms), cap, n);
}

ctl_status_t ctl_build_stat(uint64_t up, uint64_t down, char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
/* keep the integer type explicit */
    return finish(snprintf(buf, cap, "STAT %llu %llu\n",
                           (unsigned long long)up, (unsigned long long)down), cap, n);
}

/* strip line breaks from free text */
static void sanitize_msg(const char *msg, char *clean, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; msg[i] && j + 1 < cap; ++i) {
        char c = msg[i];
        clean[j++] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    clean[j] = '\0';
}

ctl_status_t ctl_build_ok(const char *msg, char *buf, size_t cap, size_t *n) {
    if (!buf || !msg) return CTL_ERR_ARG;
    char clean[256];
    sanitize_msg(msg, clean, sizeof clean);
    return finish(snprintf(buf, cap, "OK %s\n", clean), cap, n);
}

ctl_status_t ctl_build_err(const char *msg, char *buf, size_t cap, size_t *n) {
    if (!buf || !msg) return CTL_ERR_ARG;
/* put free text last */
    char clean[256];
    sanitize_msg(msg, clean, sizeof clean);
    return finish(snprintf(buf, cap, "ERR %s\n", clean), cap, n);
}

ctl_status_t ctl_build_sub(int idx, const char *name, const char *url,
                           char *buf, size_t cap, size_t *n) {
    if (!buf || !name || !url) return CTL_ERR_ARG;
    char nm[64], u[512];
    sanitize_msg(name, nm, sizeof nm);
    sanitize_msg(url, u, sizeof u);
/* keep the name in one token */
    for (size_t i = 0; nm[i]; ++i)
        if (nm[i] == ' ' || nm[i] == '\t') nm[i] = '_';
    return finish(snprintf(buf, cap, "SUB %d %s %s\n", idx, nm, u), cap, n);
}

ctl_status_t ctl_build_srv(int idx, int selected, int group,
                           const char *proto, const char *net, const char *sec,
                           int supported, const char *host, int port, const char *remark,
                           char *buf, size_t cap, size_t *n) {
    if (!buf || !proto || !net || !sec || !host || !remark) return CTL_ERR_ARG;
/* clean short fields */
    char p[32], nt[32], h[256], s[32], r[256];
    sanitize_msg(proto, p, sizeof p);
    sanitize_msg(net, nt, sizeof nt);
    sanitize_msg(host, h, sizeof h);
    sanitize_msg(sec, s, sizeof s);
    sanitize_msg(remark, r, sizeof r);
    return finish(snprintf(buf, cap, "SRV %d %d %d %s %s %s %d %s %d %s\n",
                           idx, selected ? 1 : 0, group, p, nt, s,
                           supported ? 1 : 0, h, port, r), cap, n);
}

ctl_status_t ctl_build_listend(int count, char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "LISTEND %d\n", count), cap, n);
}

ctl_status_t ctl_build_submeta(int idx, uint64_t expire,
                               char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "SUBMETA %d %llu\n", idx,
                           (unsigned long long)expire), cap, n);
}

ctl_status_t ctl_build_link(int idx, const char *link, char *buf, size_t cap, size_t *n) {
    if (!buf || !link) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "LINK %d %s\n", idx, link), cap, n);
}

ctl_status_t ctl_build_move_section(int section_id, int to_pos,
                                    char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "MOVESECTION %d %d\n", section_id, to_pos), cap, n);
}

ctl_status_t ctl_build_move_manual(int server_index, int to_pos,
                                   char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "MOVEMANUAL %d %d\n", server_index, to_pos), cap, n);
}

const char *ctl_state_name(ctl_state_t st) {
    switch (st) {
        case CTL_STATE_IDLE:       return "idle";
        case CTL_STATE_CONNECTING: return "connecting";
        case CTL_STATE_CONNECTED:  return "connected";
        case CTL_STATE_ERROR:      return "error";
        default:                   return "unknown";
    }
}
