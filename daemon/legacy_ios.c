#define _DEFAULT_SOURCE

#include "legacy_ios.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

static int parse_major(const char *version) {
    while (*version == ' ' || *version == '\t') version++;
    if (version[0] < '0' || version[0] > '9') return 0;
    return atoi(version);
}

int senko_ios_major_from_plist(const char *xml) {
    if (!xml) return 0;

    const char *key = strstr(xml, "<key>ProductVersion</key>");
    if (!key) return 0;

    /* the value element always follows its key in a plist dict; scanning from
       the key also catches the same-line form some generators emit, which the
       old line-by-line reader skipped and then parsed ProductBuildVersion */
    const char *start = strstr(key, "<string>");
    if (!start) return 0;
    start += 8;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        start++;

    const char *end = strchr(start, '<');
    if (!end || end == start) return 0;

    char version[64];
    size_t len = (size_t)(end - start);
    if (len >= sizeof version) len = sizeof version - 1;
    memcpy(version, start, len);
    version[len] = '\0';
    return parse_major(version);
}

int senko_ios_major(void) {
    FILE *f = fopen("/System/Library/CoreServices/SystemVersion.plist", "r");
    if (f) {
        /* the file is well under 1 KiB; one read keeps tag matching immune to
           line splitting on minified plists */
        char buf[4096];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = '\0';
        int major = senko_ios_major_from_plist(buf);
        if (major > 0) return major;
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
