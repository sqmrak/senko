#include "stl_roots.h"
#include "stl_log.h"

#include <Security/Security.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define stl_roots_dir "/usr/lib/senkotlsfix/roots"

static unsigned char stl_b64_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A');
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0' + 52);
    if (c == '+' || c == '/') return c == '+' ? 62 : 63;
    return 255;
}

static int stl_pem_to_der(const char *pem, size_t len, unsigned char *out, size_t cap, size_t *olen) {
    const char *p = pem;
    const char *end = pem + len;
    size_t o = 0;
    unsigned acc = 0;
    int bits = 0;

    while (p < end) {
        if (strncmp(p, "-----BEGIN", 10) == 0) {
            while (p < end && *p != '\n') p++;
            continue;
        }
        if (strncmp(p, "-----END", 8) == 0) break;
        unsigned char v = stl_b64_val((unsigned char)*p);
        if (v == 255) { p++; continue; }
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) return -1;
            out[o++] = (unsigned char)((acc >> bits) & 0xff);
        }
        p++;
    }
    *olen = o;
    return o > 0 ? 0 : -1;
}

static SecCertificateRef stl_pem_to_cert(const char *pem, size_t len) {
    if (!pem || len == 0) return NULL;
    unsigned char der[8192];
    size_t dlen = 0;
    if (stl_pem_to_der(pem, len, der, sizeof der, &dlen) != 0) return NULL;
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, der, (CFIndex)dlen);
    if (!data) return NULL;
    SecCertificateRef cert = SecCertificateCreateWithData(kCFAllocatorDefault, data);
    CFRelease(data);
    return cert;
}

static int stl_cert_same(SecCertificateRef a, SecCertificateRef b) {
    CFDataRef da = SecCertificateCopyData(a);
    CFDataRef db = SecCertificateCopyData(b);
    if (!da || !db) {
        if (da) CFRelease(da);
        if (db) CFRelease(db);
        return 0;
    }
    CFIndex la = CFDataGetLength(da);
    CFIndex lb = CFDataGetLength(db);
    int same = (la == lb) &&
        (memcmp(CFDataGetBytePtr(da), CFDataGetBytePtr(db), (size_t)la) == 0);
    CFRelease(da);
    CFRelease(db);
    return same;
}

static void stl_append_cert(CFMutableArrayRef arr, SecCertificateRef cert) {
    if (!arr || !cert) return;
    CFIndex n = CFArrayGetCount(arr);
    for (CFIndex i = 0; i < n; ++i) {
        SecCertificateRef existing = (SecCertificateRef)CFArrayGetValueAtIndex(arr, i);
        if (stl_cert_same(existing, cert)) return;
    }
    CFArrayAppendValue(arr, cert);
}

static void stl_load_pem_file(CFMutableArrayRef arr, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 65536) { fclose(f); return; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return; }
    buf[sz] = '\0';
    fclose(f);
    SecCertificateRef cert = stl_pem_to_cert(buf, (size_t)sz);
    free(buf);
    if (cert) {
        stl_append_cert(arr, cert);
        CFRelease(cert);
    }
}

static void stl_load_dir(CFMutableArrayRef arr) {
    DIR *d = opendir(stl_roots_dir);
    if (!d) return;
    struct dirent *de;
    char path[512];
    while ((de = readdir(d)) != NULL) {
        size_t nl = strlen(de->d_name);
        if (nl < 5) continue;
        if (strcmp(de->d_name + nl - 4, ".pem") != 0) continue;
        snprintf(path, sizeof path, "%s/%s", stl_roots_dir, de->d_name);
        stl_load_pem_file(arr, path);
    }
    closedir(d);
}

CFArrayRef stl_roots_anchor_array(void) {
    static CFArrayRef cached = NULL;
    if (cached) return (CFArrayRef)CFRetain(cached);

    CFMutableArrayRef arr = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (!arr) return NULL;

    stl_load_dir(arr);

    if (CFArrayGetCount(arr) == 0) {
        CFRelease(arr);
        stl_log("roots: no anchors loaded");
        return NULL;
    }

    cached = CFArrayCreateCopy(kCFAllocatorDefault, arr);
    CFRelease(arr);
    stl_log("roots: loaded %ld anchor(s)", (long)CFArrayGetCount(cached));
    return (CFArrayRef)CFRetain(cached);
}