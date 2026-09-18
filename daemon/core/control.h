#ifndef CONTROL_H
#define CONTROL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CTL_CMD_NONE = 0,
    CTL_CMD_CONNECT,
    CTL_CMD_DISCONNECT,
    CTL_CMD_STATUS,
    CTL_CMD_PING,
    CTL_CMD_ADD_SERVER,
    CTL_CMD_REPLACE_SERVER,
    CTL_CMD_ADD_SUB,
    CTL_CMD_REPLACE_SUB,
    CTL_CMD_REFRESH, /* refresh a subscription */
    CTL_CMD_DEL_SERVER, /* delete a server */
    CTL_CMD_DEL_SUB, /* delete a subscription */
    CTL_CMD_FETCH, /* fetch a url */
    CTL_CMD_LIST, /* list the catalog */
    CTL_CMD_GET_SERVER,
    CTL_CMD_AUTH,
    CTL_CMD_MOVE_SECTION,
    CTL_CMD_MOVE_MANUAL,
    CTL_CMD_SET_SUB_HEADER,
    CTL_CMD_EXPORT,
    CTL_CMD_RESTORE,
    CTL_CMD_CHECK,
    CTL_CMD_HWID, /* report the device id sent to subscription panels */
    CTL_CMD_LOGS, /* stream the daemon log tail to a client that cannot read it */
    CTL_CMD_IMPORT,       /* parse the staged file and add what it holds */
    CTL_CMD_CLEAR_MANUAL, /* drop every manual server */
    CTL_CMD_SET,          /* change one daemon setting */
    CTL_CMD_SETTINGS,     /* dump every setting as SET lines */
    CTL_CMD_RULES,
    CTL_CMD_DEL_RULE,
    CTL_CMD_DIAG, /* report which rung of every fallback ladder was taken */
    CTL_CMD_FWCONF, /* dump the firewall ruleset the kernel is running */
    CTL_CMD_FLUSH,  /* drop one piece of accumulated state by name */
    CTL_CMD_HWID_RESET, /* issue a new device id for the subscription panels */
    CTL_CMD_NATIVE_CONFIG /* render a provider configuration for iOS 12+ */
} ctl_cmd_kind_t;

typedef struct {
    ctl_cmd_kind_t kind;
    int            server_index; /* command target */
    int            target_index;
/* keep the url and name separate */
    char           text[2048];
    char           name[64];
    char           value[512];
/* CHECK only: report the stage timings as well as the total. old clients do
   not ask, and get exactly the reply they always got */
    int            want_stages;
} ctl_cmd_t;

typedef enum {
    CTL_STATE_IDLE = 0,
    CTL_STATE_CONNECTING,
    CTL_STATE_CONNECTED,
    CTL_STATE_ERROR
} ctl_state_t;

typedef enum {
    CTL_OK         =  0,
    CTL_ERR_ARG    = -1,
    CTL_ERR_PARSE  = -2, /* reject bad commands */
    CTL_ERR_BUF    = -3 /* cap output */
} ctl_status_t;

ctl_status_t ctl_parse_cmd(const char *line, size_t len, ctl_cmd_t *out);

ctl_status_t ctl_build_connect(int server_index, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_disconnect(char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_ping(int server_index, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_add_server(const char *link, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_add_sub(const char *url, const char *name,
                               char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_refresh(int sub_index, char *buf, size_t cap, size_t *n);

/* the settings dump answers in the same words the SET verb accepts, so a client
   can hand a line straight back instead of owning a second encoding */
ctl_status_t ctl_build_set(const char *key, const char *value,
                           char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_settings(char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_setend(char *buf, size_t cap, size_t *n);

/* one diagnostic fact. the value may carry spaces, so a reader splits on the
   first one only; unknown keys are meant to be shown verbatim, which is what
   lets an older app display a newer daemon's answer */
ctl_status_t ctl_build_diag(const char *key, const char *value,
                            char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_diagend(char *buf, size_t cap, size_t *n);

/* one line of the firewall ruleset. the text is sent verbatim, so a reader
   joins the lines back in order instead of parsing them */
ctl_status_t ctl_build_fwline(const char *text, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_fwend(char *buf, size_t cap, size_t *n);

/* one stage of a check and how long it took. a check that fails says which
   stage it reached, which "check failed" never did */
ctl_status_t ctl_build_stage(const char *name, int ms, int ok,
                             char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_rule(size_t index, const char *action, const char *type,
                            uint64_t hits, const char *value,
                            char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_ruleend(size_t count, char *buf, size_t cap, size_t *n);

/* connected carries an explicit zero so a new session is distinguishable from
   an older daemon that never reported session age */
ctl_status_t ctl_build_state(ctl_state_t st, long uptime,
                             char *buf, size_t cap, size_t *n);
ctl_status_t ctl_parse_state(const char *line, size_t len,
                             ctl_state_t *state, long *uptime);
ctl_status_t ctl_build_pong(int server_index, int ms, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_stat(uint64_t up, uint64_t down, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_parse_stat(const char *line, size_t len, uint64_t *up, uint64_t *down);
ctl_status_t ctl_build_ok(const char *msg, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_err(const char *msg, char *buf, size_t cap, size_t *n);

/* keep catalog records easy to split */
ctl_status_t ctl_build_sub(int idx, const char *name, const char *url,
                           char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_srv(int idx, int selected, int group,
                           const char *proto, const char *net, const char *sec,
                           int supported, const char *host, int port, const char *remark,
                           char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_listend(int count, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_submeta(int idx, uint64_t expire,
                                char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_subinfo(int idx, uint64_t upload, uint64_t download,
                               uint64_t total, const char *description,
                               const char *support_url,
                               char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_subhdr(int idx, const char *header,
                               char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_link(int idx, const char *link, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_move_section(int section_id, int to_pos,
                                    char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_move_manual(int server_index, int to_pos,
                                   char *buf, size_t cap, size_t *n);

const char *ctl_state_name(ctl_state_t st);

#ifdef __cplusplus
}
#endif

#endif /* control_h */
