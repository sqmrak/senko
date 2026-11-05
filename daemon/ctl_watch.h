#ifndef CTL_WATCH_H
#define CTL_WATCH_H

typedef struct {
    int fd;
    int directory_fd;
    char directory[108];
} ctl_watch_t;

int ctl_watch_open(ctl_watch_t *watch, const char *socket_path);
int ctl_watch_drain(ctl_watch_t *watch);
int ctl_watch_refresh(ctl_watch_t *watch, const char *socket_path, int force);
void ctl_watch_close(ctl_watch_t *watch);

#endif
