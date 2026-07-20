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
    CTL_CMD_ADD_SERVER, /* preserve the add-server wire verb */
    CTL_CMD_ADD_SUB, /* preserve the add-subscription wire verb */
    CTL_CMD_REFRESH, /* preserve the refresh wire verb */
    CTL_CMD_DEL_SERVER, /* preserve the delete-server wire verb */
    CTL_CMD_DEL_SUB, /* preserve the delete-subscription wire verb */
    CTL_CMD_FETCH, /* preserve the fetch wire verb */
    CTL_CMD_LIST, /* preserve the catalog wire verb */
    CTL_CMD_GET_SERVER,
    CTL_CMD_AUTH /* local sock is world-reachable on jb without this */
} ctl_cmd_kind_t;

typedef struct {
    ctl_cmd_kind_t kind;
    int            server_index; /* command target, or -1 when unused */
/* keep urls and names separate because both may contain spaces */
    char           text[1024];
    char           name[64];
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
    CTL_ERR_PARSE  = -2, /* reject unknown or malformed commands */
    CTL_ERR_BUF    = -3 /* reject output that cannot fit */
} ctl_status_t;

ctl_status_t ctl_parse_cmd(const char *line, size_t len, ctl_cmd_t *out);

ctl_status_t ctl_build_connect(int server_index, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_disconnect(char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_ping(int server_index, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_add_server(const char *link, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_add_sub(const char *url, const char *name,
                               char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_refresh(int sub_index, char *buf, size_t cap, size_t *n);

ctl_status_t ctl_build_state(ctl_state_t st, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_pong(int server_index, int ms, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_stat(uint64_t up, uint64_t down, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_ok(const char *msg, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_err(const char *msg, char *buf, size_t cap, size_t *n);

/* keep catalog records space-tolerant so old ui clients can parse them */
ctl_status_t ctl_build_sub(int idx, const char *name, const char *url,
                           char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_srv(int idx, int selected, int group,
                           const char *proto, const char *net, const char *sec,
                           int supported, const char *host, int port, const char *remark,
                           char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_listend(int count, char *buf, size_t cap, size_t *n);
ctl_status_t ctl_build_link(int idx, const char *link, char *buf, size_t cap, size_t *n);

const char *ctl_state_name(ctl_state_t st);

#ifdef __cplusplus
}
#endif

#endif /* control_h */
