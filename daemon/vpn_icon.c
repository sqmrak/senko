#include "vpn_icon.h"

#if defined(__APPLE__)
#include <fcntl.h>
#include <notify.h>
#include <unistd.h>

static const char k_state_path[] =
    "/var/mobile/Library/Preferences/com.senko.vpnicon.state";
static const char k_notify_name[] = "com.senko.vpnicon.changed";
#endif

void vpn_icon_set(int enabled) {
#if defined(__APPLE__)
    const char *body = enabled ? "1\n" : "0\n";
    int fd = open(k_state_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        (void)write(fd, body, 2);
        close(fd);
    }
    (void)notify_post(k_notify_name);
#else
    (void)enabled;
#endif
}
