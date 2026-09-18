#include "control.h"

#include <limits.h>
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
    out->value[0] = '\0';
    out->want_stages = 0;

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
    if (verb_is(line, len, "CHECK", &rest, &rl)) {
        const char *sp = memchr(rest, ' ', rl);
        int idx = -1;
        size_t ml = sp ? (size_t)(sp - rest) : 0;
        int want_stages = 0;
        size_t index_len = sp ? rl - ml - 1 : 0;
/* the optional trailing word asks for the per stage timings. a client that
   does not send it gets the reply this verb has always given */
        if (sp && index_len >= 8 &&
            memcmp(sp + 1 + index_len - 7, " stages", 7) == 0) {
            want_stages = 1;
            index_len -= 7;
        }
        if (!sp || ml == 0 || ml >= sizeof out->name ||
            parse_int_span(sp + 1, index_len, &idx) != 0 || idx < 0)
            return CTL_ERR_PARSE;
        out->want_stages = want_stages;
        memcpy(out->name, rest, ml);
        out->name[ml] = '\0';
        if (strcmp(out->name, "tcp") != 0 && strcmp(out->name, "proxy") != 0 &&
            strcmp(out->name, "tunnel") != 0 && strcmp(out->name, "handshake") != 0)
            return CTL_ERR_PARSE;
        out->server_index = idx;
        out->kind = CTL_CMD_CHECK;
        return CTL_OK;
    }
    if (verb_is(line, len, "REFRESH", &rest, &rl)) {
        int idx;
        if (parse_int_span(rest, rl, &idx) != 0) return CTL_ERR_PARSE;
        out->kind = CTL_CMD_REFRESH;
        out->server_index = idx; /* store the subscription index */
        return CTL_OK;
    }
    if (verb_is(line, len, "DIAG", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_DIAG;
        return CTL_OK;
    }
    if (verb_is(line, len, "FWCONF", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_FWCONF;
        return CTL_OK;
    }
    if (verb_is(line, len, "HWIDRESET", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_HWID_RESET;
        return CTL_OK;
    }
    if (verb_is(line, len, "FLUSH", &rest, &rl)) {
/* naming the target rather than taking a flag keeps an accidental FLUSH from
   removing more than the caller meant */
        if (rl == 0 || rl >= sizeof out->name) return CTL_ERR_PARSE;
        memcpy(out->name, rest, rl);
        out->name[rl] = '\0';
        if (strcmp(out->name, "dns") != 0 && strcmp(out->name, "bypass") != 0 &&
            strcmp(out->name, "rules") != 0 && strcmp(out->name, "config") != 0)
            return CTL_ERR_PARSE;
        out->kind = CTL_CMD_FLUSH;
        return CTL_OK;
    }
    if (verb_is(line, len, "SETTINGS", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_SETTINGS;
        return CTL_OK;
    }
    if (verb_is(line, len, "RULES", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_RULES;
        return CTL_OK;
    }
    if (verb_is(line, len, "DELRULE", &rest, &rl)) {
        int idx;
        if (parse_int_span(rest, rl, &idx) != 0 || idx < 0) return CTL_ERR_PARSE;
        out->kind = CTL_CMD_DEL_RULE;
        out->server_index = idx;
        return CTL_OK;
    }
    if (verb_is(line, len, "SET", &rest, &rl)) {
        const char *sp = memchr(rest, ' ', rl);
        if (!sp) return CTL_ERR_PARSE; /* require a key and a value */
        size_t kl = (size_t)(sp - rest);
        size_t vl = rl - kl - 1;
        if (kl == 0 || kl >= sizeof out->name) return CTL_ERR_PARSE;
        if (vl == 0 || vl >= sizeof out->text) return CTL_ERR_PARSE;
        memcpy(out->name, rest, kl); out->name[kl] = '\0';
        memcpy(out->text, sp + 1, vl); out->text[vl] = '\0';
        out->kind = CTL_CMD_SET;
        return CTL_OK;
    }
    if (verb_is(line, len, "SETSUBHDR", &rest, &rl)) {
        const char *sp = memchr(rest, ' ', rl);
        int idx = -1;
        if (!sp || parse_int_span(rest, (size_t)(sp - rest), &idx) != 0 || idx < 0)
            return CTL_ERR_PARSE;
        size_t hl = rl - (size_t)(sp - rest) - 1;
        if (hl >= sizeof out->text) return CTL_ERR_PARSE;
        memcpy(out->text, sp + 1, hl);
        out->text[hl] = '\0';
        out->server_index = idx;
        out->kind = CTL_CMD_SET_SUB_HEADER;
        return CTL_OK;
    }
    if (verb_is(line, len, "ADDSRV", &rest, &rl)) {
        if (rl == 0 || rl >= sizeof out->text) return CTL_ERR_PARSE;
        memcpy(out->text, rest, rl);
        out->text[rl] = '\0';
        out->kind = CTL_CMD_ADD_SERVER;
        return CTL_OK;
    }
    if (verb_is(line, len, "REPLACESRV", &rest, &rl)) {
        const char *sp = memchr(rest, ' ', rl);
        int idx = -1;
        size_t link_len = sp ? rl - (size_t)(sp - rest) - 1 : 0;
        if (!sp || parse_int_span(rest, (size_t)(sp - rest), &idx) != 0 ||
            idx < 0 || link_len == 0 || link_len >= sizeof out->text)
            return CTL_ERR_PARSE;
        memcpy(out->text, sp + 1, link_len);
        out->text[link_len] = '\0';
        out->server_index = idx;
        out->kind = CTL_CMD_REPLACE_SERVER;
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
    if (verb_is(line, len, "REPLACESUB", &rest, &rl)) {
        const char *url = memchr(rest, ' ', rl);
        const char *header = NULL;
        const char *name = NULL;
        int idx = -1;
        size_t index_len, url_len, header_len, name_len;
        if (!url) return CTL_ERR_PARSE;
        index_len = (size_t)(url - rest);
        header = memchr(url + 1, ' ', rl - index_len - 1);
        if (!header) return CTL_ERR_PARSE;
        url_len = (size_t)(header - url - 1);
        name = memchr(header + 1, ' ', rl - index_len - url_len - 2);
        if (!name) return CTL_ERR_PARSE;
        header_len = (size_t)(name - header - 1);
        name_len = rl - index_len - url_len - header_len - 3;
        if (parse_int_span(rest, index_len, &idx) != 0 || idx < 0 ||
            url_len == 0 || url_len >= sizeof out->text ||
            header_len == 0 || header_len >= sizeof out->value ||
            name_len == 0 || name_len >= sizeof out->name)
            return CTL_ERR_PARSE;
        memcpy(out->text, url + 1, url_len); out->text[url_len] = '\0';
        memcpy(out->value, header + 1, header_len); out->value[header_len] = '\0';
        memcpy(out->name, name + 1, name_len); out->name[name_len] = '\0';
        out->server_index = idx;
        out->kind = CTL_CMD_REPLACE_SUB;
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
    if (verb_is(line, len, "HWID", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_HWID;
        return CTL_OK;
    }
    if (verb_is(line, len, "LOGS", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_LOGS;
        return CTL_OK;
    }
    if (verb_is(line, len, "IMPORT", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_IMPORT;
        return CTL_OK;
    }
    if (verb_is(line, len, "CLEARMANUAL", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_CLEAR_MANUAL;
        return CTL_OK;
    }
    if (verb_is(line, len, "EXPORT", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_EXPORT;
        return CTL_OK;
    }
    if (verb_is(line, len, "RESTORE", &rest, &rl) && rl == 0) {
        out->kind = CTL_CMD_RESTORE;
        return CTL_OK;
    }
    if (verb_is(line, len, "GETSRV", &rest, &rl)) {
        int idx;
        if (parse_int_span(rest, rl, &idx) != 0 || idx < 0) return CTL_ERR_PARSE;
        out->kind = CTL_CMD_GET_SERVER;
        out->server_index = idx;
        return CTL_OK;
    }
    if (verb_is(line, len, "NATIVE_CONFIG", &rest, &rl)) {
        int idx;
        if (parse_int_span(rest, rl, &idx) != 0 || idx < 0)
            return CTL_ERR_PARSE;
        out->kind = CTL_CMD_NATIVE_CONFIG;
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

static int ctl_pct_encode(const char *src, char *dst, size_t cap);

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

ctl_status_t ctl_build_set(const char *key, const char *value,
                           char *buf, size_t cap, size_t *n) {
    if (!buf || !key || !value || !key[0] || !value[0]) return CTL_ERR_ARG;
/* a key or value carrying a space would parse back as a different command */
    if (strchr(key, ' ') || strchr(value, ' ')) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "SET %s %s\n", key, value), cap, n);
}

ctl_status_t ctl_build_settings(char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "SETTINGS\n"), cap, n);
}

ctl_status_t ctl_build_setend(char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "SETEND\n"), cap, n);
}

ctl_status_t ctl_build_diag(const char *key, const char *value,
                            char *buf, size_t cap, size_t *n) {
    if (!buf || !key || !key[0] || !value) return CTL_ERR_ARG;
/* a newline in a value would read back as a second fact */
    if (strchr(key, ' ') || strchr(key, '\n') || strchr(value, '\n'))
        return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "DIAG %s %s\n", key,
                           value[0] ? value : "-"), cap, n);
}

ctl_status_t ctl_build_diagend(char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "DIAGEND\n"), cap, n);
}

ctl_status_t ctl_build_fwline(const char *text, char *buf, size_t cap, size_t *n) {
    if (!buf || !text) return CTL_ERR_ARG;
    if (strchr(text, '\n')) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "FWLINE %s\n", text), cap, n);
}

ctl_status_t ctl_build_fwend(char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "FWEND\n"), cap, n);
}

ctl_status_t ctl_build_stage(const char *name, int ms, int ok,
                             char *buf, size_t cap, size_t *n) {
    if (!buf || !name || !name[0]) return CTL_ERR_ARG;
    if (strchr(name, '\n')) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "STAGE %d %d %s\n", ok ? 1 : 0, ms, name),
                  cap, n);
}

ctl_status_t ctl_build_rule(size_t index, const char *action, const char *type,
                            uint64_t hits, const char *value,
                            char *buf, size_t cap, size_t *n) {
    if (!buf || !action || !type || !value) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "RULE %zu %s %s %llu %s\n",
                           index, action, type, (unsigned long long)hits, value),
                  cap, n);
}

ctl_status_t ctl_build_ruleend(size_t count, char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    return finish(snprintf(buf, cap, "RULEEND %zu\n", count), cap, n);
}

ctl_status_t ctl_build_state(ctl_state_t st, long uptime,
                             char *buf, size_t cap, size_t *n) {
    if (!buf) return CTL_ERR_ARG;
    /* the age is a trailing token so a reader that only wants the state name
       can keep taking the first word. a connected tunnel always carries one,
       because a missing token and an age of zero are not the same answer */
    if (uptime > 0 || st == CTL_STATE_CONNECTED)
        return finish(snprintf(buf, cap, "STATE %s %ld\n",
                               ctl_state_name(st), uptime), cap, n);
    return finish(snprintf(buf, cap, "STATE %s\n", ctl_state_name(st)), cap, n);
}

ctl_status_t ctl_parse_state(const char *line, size_t len,
                             ctl_state_t *state, long *uptime) {
    const char *rest;
    size_t rest_len;
    const char *space;
    size_t name_len;
    ctl_state_t parsed;
    long age = 0;
    if (!line || !state || !uptime) return CTL_ERR_ARG;
    *state = CTL_STATE_IDLE;
    *uptime = 0;
    len = trim_eol(line, len);
    if (!verb_is(line, len, "STATE", &rest, &rest_len) || rest_len == 0)
        return CTL_ERR_PARSE;
    space = memchr(rest, ' ', rest_len);
    name_len = space ? (size_t)(space - rest) : rest_len;
    if (name_len == 4 && memcmp(rest, "idle", 4) == 0)
        parsed = CTL_STATE_IDLE;
    else if (name_len == 10 && memcmp(rest, "connecting", 10) == 0)
        parsed = CTL_STATE_CONNECTING;
    else if (name_len == 9 && memcmp(rest, "connected", 9) == 0)
        parsed = CTL_STATE_CONNECTED;
    else if (name_len == 5 && memcmp(rest, "error", 5) == 0)
        parsed = CTL_STATE_ERROR;
    else
        return CTL_ERR_PARSE;
    if (space) {
        size_t age_len = rest_len - name_len - 1;
        const char *p = space + 1;
        if (age_len == 0) return CTL_ERR_PARSE;
        for (size_t i = 0; i < age_len; ++i) {
            int digit;
            if (p[i] < '0' || p[i] > '9') return CTL_ERR_PARSE;
            digit = p[i] - '0';
            if (age > (LONG_MAX - digit) / 10) return CTL_ERR_PARSE;
            age = age * 10 + digit;
        }
    }
    *state = parsed;
    *uptime = age;
    return CTL_OK;
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
ctl_status_t ctl_parse_stat(const char *line, size_t len, uint64_t *up, uint64_t *down) {
    uint64_t values[2] = {0, 0};
    if (up) *up = 0;
    if (down) *down = 0;
    if (!line || !up || !down) return CTL_ERR_ARG;
    len = trim_eol(line, len);
    if (len < 8 || len > 46 || memcmp(line, "STAT ", 5) != 0) return CTL_ERR_PARSE;
    size_t at = 5;
    for (size_t field = 0; field < 2; ++field) {
        size_t start = at;
        while (at < len && line[at] >= '0' && line[at] <= '9') {
            unsigned digit = (unsigned)(line[at++] - '0');
            if (values[field] > (UINT64_MAX - digit) / 10) return CTL_ERR_PARSE;
            values[field] = values[field] * 10 + digit;
        }
        if (at == start) return CTL_ERR_PARSE;
        if (field == 0 && (at >= len || line[at++] != ' ')) return CTL_ERR_PARSE;
    }
    if (at != len) return CTL_ERR_PARSE;
    *up = values[0];
    *down = values[1];
    return CTL_OK;
}

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

ctl_status_t ctl_build_subinfo(int idx, uint64_t upload, uint64_t download,
                               uint64_t total, const char *description,
                               const char *support_url,
                               char *buf, size_t cap, size_t *n) {
    char desc[768], support[1536];
    if (!buf || !description || !support_url) return CTL_ERR_ARG;
    if (ctl_pct_encode(description, desc, sizeof desc) != 0 ||
        ctl_pct_encode(support_url, support, sizeof support) != 0)
        return CTL_ERR_BUF;
    return finish(snprintf(buf, cap, "SUBINFO %d %llu %llu %llu %s %s\n", idx,
                           (unsigned long long)upload,
                           (unsigned long long)download,
                           (unsigned long long)total,
                           desc[0] ? desc : "-", support[0] ? support : "-"),
                  cap, n);
}

static int ctl_pct_encode(const char *src, char *dst, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t off = 0;
    if (!src || !dst || cap == 0) return -1;
    for (const unsigned char *p = (const unsigned char *)src; *p; ++p) {
        unsigned char c = *p;
        if (c <= 0x20 || c >= 0x7f || c == '%' || c == '#' || c == '+') {
            if (off + 3 >= cap) return -1;
            dst[off++] = '%';
            dst[off++] = hex[c >> 4];
            dst[off++] = hex[c & 0xf];
        } else {
            if (off + 1 >= cap) return -1;
            dst[off++] = (char)c;
        }
    }
    dst[off] = '\0';
    return 0;
}

ctl_status_t ctl_build_subhdr(int idx, const char *header,
                              char *buf, size_t cap, size_t *n) {
    if (!buf || !header) return CTL_ERR_ARG;
    char encoded[1536];
    if (!header[0]) return finish(snprintf(buf, cap, "SUBHDR %d -\n", idx), cap, n);
    if (ctl_pct_encode(header, encoded, sizeof encoded) != 0) return CTL_ERR_BUF;
    return finish(snprintf(buf, cap, "SUBHDR %d %s\n", idx, encoded), cap, n);
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
