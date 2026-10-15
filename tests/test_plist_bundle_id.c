#include "plist_bundle_id.h"

#include <stdio.h>
#include <string.h>

#include "plist_fixture.inc"

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static int ident(const unsigned char *p, size_t n, char *out, size_t cap) {
    return plist_bundle_id(p, n, out, cap);
}

int main(void) {
    char id[128];
    ok("binary", ident(kPlistBinary, sizeof kPlistBinary, id, sizeof id) == 22 &&
       strcmp(id, "com.example.Demo-App_1") == 0);
    ok("xml", ident(kPlistXml, sizeof kPlistXml, id, sizeof id) == 22 &&
       strcmp(id, "com.example.Demo-App_1") == 0);
    ok("binary with many keys", ident(kPlistBinaryBig, sizeof kPlistBinaryBig, id, sizeof id) == 22 &&
       strcmp(id, "com.apple.mobilesafari") == 0);
    ok("binary with utf-16 strings elsewhere",
       ident(kPlistBinaryUtf16, sizeof kPlistBinaryUtf16, id, sizeof id) == 19 &&
       strcmp(id, "com.example.unicode") == 0);
    ok("output too small", ident(kPlistBinary, sizeof kPlistBinary, id, 10) == -1);
    ok("no output buffer", plist_bundle_id(kPlistBinary, sizeof kPlistBinary, NULL, 10) == -1 &&
       plist_bundle_id(NULL, 4, id, sizeof id) == -1);

    /* every truncation of a binary plist is refused, never read past its end */
    int bad = 0;
    for (size_t n = 0; n < sizeof kPlistBinary; ++n)
        if (ident(kPlistBinary, n, id, sizeof id) != -1) ++bad;
    ok("truncated binary plists are refused", bad == 0);
    bad = 0;
    for (size_t n = 0; n < sizeof kPlistBinaryBig; n += 7)
        if (ident(kPlistBinaryBig, n, id, sizeof id) != -1) ++bad;
    ok("truncated big binary plists are refused", bad == 0);

    /* a damaged trailer or object table must not send the reader out of the file */
    unsigned char copy[sizeof kPlistBinary];
    bad = 0;
    for (size_t i = 0; i < sizeof copy; ++i) {
        memcpy(copy, kPlistBinary, sizeof copy);
        copy[i] ^= 0xff;
        (void)ident(copy, sizeof copy, id, sizeof id); /* must not crash; the answer may be anything */
    }
    ok("flipped bytes are survived", bad == 0);
    for (size_t i = sizeof copy - 32; i < sizeof copy; ++i) {
        memcpy(copy, kPlistBinary, sizeof copy);
        copy[i] = 0xff;
        (void)ident(copy, sizeof copy, id, sizeof id);
    }

    const char *xml_no_key = "<plist><dict><key>CFBundleName</key><string>x</string></dict></plist>";
    ok("xml without the key", plist_bundle_id((const uint8_t *)xml_no_key, strlen(xml_no_key), id, sizeof id) == -1);
    const char *xml_spaced = "<dict><key>CFBundleIdentifier</key>\n\t<string>com.a.b</string></dict>";
    ok("xml with whitespace", plist_bundle_id((const uint8_t *)xml_spaced, strlen(xml_spaced), id, sizeof id) == 7 &&
       strcmp(id, "com.a.b") == 0);
    const char *xml_wrong = "<dict><key>CFBundleIdentifier</key><integer>4</integer></dict>";
    ok("xml with a non string value", plist_bundle_id((const uint8_t *)xml_wrong, strlen(xml_wrong), id, sizeof id) == -1);
    const char *xml_cut = "<dict><key>CFBundleIdentifier</key><string>com.a.b";
    ok("xml cut inside the value", plist_bundle_id((const uint8_t *)xml_cut, strlen(xml_cut), id, sizeof id) == -1);
    const char *xml_hostile = "<dict><key>CFBundleIdentifier</key><string>a b;rm</string></dict>";
    ok("xml identifier with odd bytes", plist_bundle_id((const uint8_t *)xml_hostile, strlen(xml_hostile), id, sizeof id) == -1);
    const char *xml_empty = "<dict><key>CFBundleIdentifier</key><string></string></dict>";
    ok("xml empty identifier", plist_bundle_id((const uint8_t *)xml_empty, strlen(xml_empty), id, sizeof id) == -1);
    ok("not a plist", plist_bundle_id((const uint8_t *)"hello", 5, id, sizeof id) == -1 &&
       plist_bundle_id((const uint8_t *)"", 0, id, sizeof id) == -1);

    if (failures) {
        fprintf(stderr, "%d plist checks failed\n", failures);
        return 1;
    }
    puts("all plist checks passed");
    return 0;
}
