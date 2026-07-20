#define _DEFAULT_SOURCE

#include "stl_gate.h"
#include "stl_log.h"

#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define stl_prefs_path "/var/mobile/Library/Preferences/com.senko.senkotlsfix.plist"

static int g_gate_state; /* 0 unknown, 1 on, -1 off */
static pthread_once_t g_gate_once = PTHREAD_ONCE_INIT;

static int stl_default_on_bundle(const char *bid) {
    /* keep modern tls enabled because package fetches reject partial archives */
    static const char *k[] = {
        "com.senko.app",
        "com.apple.mobilesafari",
        "com.apple.WebKit.Networking",
        "com.apple.WebKit.WebContent",
        "com.saurik.Cydia",
        "org.coolstar.SileoStore",
        "org.coolstar.Sileo",
        NULL
    };
    for (int i = 0; k[i]; ++i)
        if (strcmp(bid, k[i]) == 0) return 1;
    if (strncmp(bid, "com.apple.WebKit", 16) == 0)
        return 1;
    return 0;
}

static CFDictionaryRef stl_load_prefs(void) {
    FILE *f = fopen(stl_prefs_path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz <= 0 || sz > 65536) { fclose(f); return NULL; }
    rewind(f);
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, buf, (CFIndex)sz);
    free(buf);
    if (!data) return NULL;

    CFStringRef err = NULL;
    CFPropertyListRef pl = CFPropertyListCreateFromXMLData(kCFAllocatorDefault, data,
                                                           kCFPropertyListImmutable, &err);
    CFRelease(data);
    if (err) CFRelease(err);
    if (!pl || CFGetTypeID(pl) != CFDictionaryGetTypeID()) {
        if (pl) CFRelease(pl);
        return NULL;
    }
    return (CFDictionaryRef)pl;
}

static int stl_cfbool(CFDictionaryRef d, const char *key, int def) {
    CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key,
                                              kCFStringEncodingUTF8);
    if (!k) return def;
    CFTypeRef v = CFDictionaryGetValue(d, k);
    CFRelease(k);
    if (!v || CFGetTypeID(v) != CFBooleanGetTypeID()) return def;
    return CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0;
}

static int stl_external_tlsfix(void) {
    return access("/Library/MobileSubstrate/DynamicLibraries/tlsfix.dylib", F_OK) == 0;
}

static void stl_gate_init(void) {
    if (stl_external_tlsfix()) {
        g_gate_state = -1;
        return;
    }

    const char *pn = getprogname();
    if (pn && strcmp(pn, "senkod") == 0) {
        g_gate_state = -1;
        stl_log("gate: senkod disabled");
        return;
    }

    CFDictionaryRef prefs = stl_load_prefs();
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle) {
        if (prefs && stl_cfbool(prefs, "enableAll", 0)) {
            CFRelease(prefs);
            g_gate_state = 1;
            stl_log("gate: active for %s enableall", pn ? pn : "unknown");
            return;
        }
        if (prefs) CFRelease(prefs);
        g_gate_state = -1;
        return;
    }
    CFStringRef bid_ref = CFBundleGetIdentifier(bundle);
    if (!bid_ref) {
        if (prefs) CFRelease(prefs);
        g_gate_state = -1;
        return;
    }
    char bid[256];
    if (!CFStringGetCString(bid_ref, bid, sizeof bid, kCFStringEncodingUTF8)) {
        if (prefs) CFRelease(prefs);
        g_gate_state = -1;
        return;
    }

    int on = -1;
    if (prefs) {
        char key[320];
        snprintf(key, sizeof key, "enabled-%s", bid);
        CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key,
                                                  kCFStringEncodingUTF8);
        CFTypeRef v = k ? CFDictionaryGetValue(prefs, k) : NULL;
        if (k) CFRelease(k);
        if (v && CFGetTypeID(v) == CFBooleanGetTypeID())
            on = CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0;
        if (on < 0 && stl_cfbool(prefs, "enableAll", 0))
            on = 1;
    }
    if (on < 0)
        on = stl_default_on_bundle(bid) ? 1 : 0;

    if (prefs) CFRelease(prefs);
    g_gate_state = on ? 1 : -1;
    if (on)
        stl_log("gate: active for %s", bid);
}

int stl_gate_skip_process(void) {
    const char *pn = getprogname();
    if (!pn) return 0;
    return !strcmp(pn, "SpringBoard") || !strcmp(pn, "backboardd") ||
           !strcmp(pn, "assertiond")  || !strcmp(pn, "lockdownd");
}

int stl_gate_is_active(void) {
    if (stl_gate_skip_process()) return 0;
    pthread_once(&g_gate_once, stl_gate_init);
    return g_gate_state == 1;
}

void stl_gate_load_tls_options(int *tls13, int *drain_guard, int *sys_fallback) {
    int t13 = 1, dg = 1, sf = 1;
    CFDictionaryRef prefs = stl_load_prefs();
    if (prefs) {
        t13 = stl_cfbool(prefs, "tls13", 1);
        dg  = stl_cfbool(prefs, "drainGuard", 1);
        sf  = stl_cfbool(prefs, "systemFallback", 1);
        CFRelease(prefs);
    }
    if (tls13) *tls13 = t13;
    if (drain_guard) *drain_guard = dg;
    if (sys_fallback) *sys_fallback = sf;
}
