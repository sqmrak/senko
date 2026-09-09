#include "legacy_ios.h"

#include <stddef.h>
#include <stdio.h>

static int failed;

static void expect(const char *xml, int want, const char *name) {
    int got = senko_ios_major_from_plist(xml);
    if (got != want) {
        fprintf(stderr, "FAIL %s: want %d got %d\n", name, want, got);
        failed = 1;
    }
}

int main(void) {
    /* real device layout: value on the line after its key */
    expect("<dict>\n"
           "\t<key>ProductBuildVersion</key>\n\t<string>9B206</string>\n"
           "\t<key>ProductName</key>\n\t<string>iPhone OS</string>\n"
           "\t<key>ProductVersion</key>\n\t<string>5.1.1</string>\n"
           "</dict>", 5, "multiline ios 5.1.1");

    /* same-line form: the old reader skipped this and then parsed the build
       string that follows, reporting a bogus major */
    expect("<dict>\n"
           "<key>ProductVersion</key><string>5.0</string>\n"
           "<key>ProductBuildVersion</key><string>9A334</string>\n"
           "</dict>", 5, "same-line ios 5.0");

    expect("<key>ProductVersion</key>\n<string>10.3.4</string>", 10, "ios 10.3.4");
    expect("<key>ProductVersion</key>\n<string> 6.1 </string>", 6, "leading space in value");
    expect("<key>ProductVersion</key><string>12.5.7</string>", 12, "ios 12 same line");

    /* key present but no value before end of buffer */
    expect("<key>ProductVersion</key>\n", 0, "no string element");
    expect("<key>ProductName</key><string>iPhone OS</string>", 0, "wrong key only");
    expect("", 0, "empty");
    expect(NULL, 0, "null");
    expect("<key>ProductVersion</key><string></string>", 0, "empty value");
    expect("<key>ProductVersion</key><string>beta</string>", 0, "non-numeric value");

    /* the binary plist fallback: only the kernel version is readable there */
    struct { const char *release; int want; } darwin[] = {
        { "11.0.0", 5 }, { "13.0.0", 6 }, { "14.0.0", 7 },
        { "15.6.0", 9 }, { "16.7.0", 10 }, { "17.7.0", 11 },
        { "18.7.0", 12 }, { "19.6.0", 13 }, { "21.6.0", 15 },
        { "", 0 }, { "beta", 0 }, { NULL, 0 }
    };
    for (size_t i = 0; i < sizeof darwin / sizeof darwin[0]; ++i) {
        int got = senko_ios_major_from_darwin(darwin[i].release);
        if (got == darwin[i].want) continue;
        fprintf(stderr, "FAIL darwin %s: want %d got %d\n",
                darwin[i].release ? darwin[i].release : "(null)",
                darwin[i].want, got);
        failed = 1;
    }

    if (failed) return 1;
    puts("all legacy_ios checks passed");
    return 0;
}
