#define _DEFAULT_SOURCE
#include "ctl_watch.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/event.h>
#include <sys/mount.h>
#include <time.h>
#elif defined(__linux__)
#include <sys/inotify.h>
#endif

int ctl_watch_open(ctl_watch_t *watch, const char *socket_path) {
    char directory[108];
    char *slash;
    if (!watch || !socket_path || strlen(socket_path) >= sizeof directory) return -1;
    watch->fd = -1;
    watch->directory_fd = -1;
    memcpy(directory, socket_path, strlen(socket_path) + 1);
    slash = strrchr(directory, '/');
    if (!slash) { directory[0] = '.'; directory[1] = '\0'; }
    else if (slash == directory) slash[1] = '\0';
    else *slash = '\0';
    memcpy(watch->directory, directory, strlen(directory) + 1);
#ifdef __APPLE__
    watch->directory_fd = open(directory, O_EVTONLY);
    if (watch->directory_fd < 0) return -1;
    watch->fd = kqueue();
    if (watch->fd < 0) { ctl_watch_close(watch); return -1; }
    struct kevent event;
    EV_SET(&event, (uintptr_t)watch->directory_fd, EVFILT_VNODE,
           EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE, 0, NULL);
    if (kevent(watch->fd, &event, 1, NULL, 0, NULL) != 0) {
        ctl_watch_close(watch);
        return -1;
    }
    /* a boot-time mount can replace /var without touching its old directory */
    EV_SET(&event, 0, EVFILT_FS, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if (kevent(watch->fd, &event, 1, NULL, 0, NULL) != 0)
        fprintf(stderr, "senkod: cannot watch filesystem mounts: %s\n", strerror(errno));
#elif defined(__linux__)
    watch->directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (watch->directory_fd < 0) return -1;
    watch->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch->fd < 0) { ctl_watch_close(watch); return -1; }
    if (inotify_add_watch(watch->fd, directory, IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                          IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT) < 0) {
        ctl_watch_close(watch);
        return -1;
    }
#else
    (void)directory;
    return -1;
#endif
    if (fcntl(watch->fd, F_SETFD, FD_CLOEXEC) != 0 ||
        (watch->directory_fd >= 0 && fcntl(watch->directory_fd, F_SETFD, FD_CLOEXEC) != 0)) {
        ctl_watch_close(watch);
        return -1;
    }
    return 0;
}

int ctl_watch_drain(ctl_watch_t *watch) {
    if (!watch || watch->fd < 0) return -1;
#ifdef __APPLE__
    struct kevent events[8];
    struct timespec timeout = {0, 0};
    int count;
    do { count = kevent(watch->fd, NULL, 0, events, 8, &timeout); } while (count < 0 && errno == EINTR);
    if (count < 0) return -1;
    int invalidated = 0;
    for (int i = 0; i < count; ++i) {
        if (events[i].flags & EV_ERROR) return -1;
        if (events[i].filter == EVFILT_VNODE &&
            (events[i].fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE))) invalidated = 1;
    }
    return invalidated;
#elif defined(__linux__)
    union { struct inotify_event alignment; char bytes[4096]; } buf;
    int invalidated = 0;
    for (;;) {
        ssize_t n = read(watch->fd, buf.bytes, sizeof buf.bytes);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return invalidated;
        if (n <= 0) return -1;
        for (size_t off = 0; off < (size_t)n;) {
            if ((size_t)n - off < sizeof(struct inotify_event)) return -1;
            struct inotify_event *event = (struct inotify_event *)(void *)(buf.bytes + off);
            if (event->len > (size_t)n - off - sizeof *event) return -1;
            if (event->mask & IN_Q_OVERFLOW) return -1;
            if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT | IN_IGNORED)) invalidated = 1;
            off += sizeof *event + event->len;
        }
    }
#else
    return -1;
#endif
}

int ctl_watch_refresh(ctl_watch_t *watch, const char *socket_path, int force) {
    struct stat before, current;
    if (!watch || watch->directory_fd < 0 || !socket_path) return -1;
    if (fstat(watch->directory_fd, &before) != 0 || stat(watch->directory, &current) != 0) return -1;
    if (!force && before.st_dev == current.st_dev && before.st_ino == current.st_ino) return 0;
    ctl_watch_t replacement;
    if (ctl_watch_open(&replacement, socket_path) != 0) return -1;
    if (fstat(replacement.directory_fd, &before) != 0 ||
        stat(replacement.directory, &current) != 0 || before.st_dev != current.st_dev ||
        before.st_ino != current.st_ino) {
        ctl_watch_close(&replacement);
        return -1;
    }
    ctl_watch_close(watch);
    *watch = replacement;
    return 0;
}

void ctl_watch_close(ctl_watch_t *watch) {
    if (!watch) return;
    if (watch->fd >= 0) close(watch->fd);
    if (watch->directory_fd >= 0) close(watch->directory_fd);
    watch->fd = watch->directory_fd = -1;
}
