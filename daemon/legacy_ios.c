#define _DEFAULT_SOURCE

#include "legacy_ios.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

static int parse_major(const char *version) {
    if (!version || version[0] < '0' || version[0] > '9') return 0;
    return atoi(version);
}

int senko_ios_major(void) {
    FILE *f = fopen("/System/Library/CoreServices/SystemVersion.plist", "r");
    if (f) {
        char line[256];
        int found_key = 0;
        while (fgets(line, sizeof line, f)) {
            if (strstr(line, "<key>ProductVersion</key>")) {
                found_key = 1;
                continue;
            }
            if (found_key) {
                char version[64];
                const char *start = strstr(line, "<string>");
                if (start) {
                    start += 8;
                    const char *end = strchr(start, '<');
                    if (end && end > start) {
                        size_t len = (size_t)(end - start);
                        if (len >= sizeof version) len = sizeof version - 1;
                        memcpy(version, start, len);
                        version[len] = '\0';
                        fclose(f);
                        return parse_major(version);
                    }
                }
                if (strstr(line, "</dict>")) break;
            }
        }
        fclose(f);
    }

#if defined(__APPLE__)
    struct utsname uts;
    if (uname(&uts) == 0 && strncmp(uts.release, "11.", 3) == 0)
        return 5;
#endif
    return 0;
}

int senko_is_ios5(void) {
    return senko_ios_major() == 5;
}
