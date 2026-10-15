#include "utun_dns.h"
#include "legacy_ios.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

void utun_dns_init(utun_dns_t *dns) {
    if (dns) dns->store = NULL;
}

#if defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>

/* SystemConfiguration exports these on every ios release, but the ios sdk
   headers mark them __IPHONE_NA and the compiler refuses the calls */
typedef const struct __SCDynamicStore *SCDynamicStoreRef;
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef allocator, CFStringRef name,
                                              void *callout, void *context);
extern Boolean SCDynamicStoreAddTemporaryValue(SCDynamicStoreRef store, CFStringRef key,
                                               CFPropertyListRef value);
extern Boolean SCDynamicStoreRemoveValue(SCDynamicStoreRef store, CFStringRef key);
extern int SCError(void);
extern const char *SCErrorString(int status);

#define UTUN_DNS_SERVICE "State:/Network/Service/com.senko.utun/"

static CFStringRef cf_text(const char *text) {
    return CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
}

static CFArrayRef cf_one(const char *text) {
    CFStringRef item = cf_text(text);
    if (!item) return NULL;
    CFArrayRef array = CFArrayCreate(kCFAllocatorDefault, (const void **)&item, 1,
                                     &kCFTypeArrayCallBacks);
    CFRelease(item);
    return array;
}

static void put(CFMutableDictionaryRef dict, const char *key, CFTypeRef value) {
    CFStringRef name = cf_text(key);
    if (name && value) CFDictionarySetValue(dict, name, value);
    if (name) CFRelease(name);
    if (value) CFRelease(value);
}

static void remove_keys(SCDynamicStoreRef store) {
    CFStringRef ipv4 = cf_text(UTUN_DNS_SERVICE "IPv4");
    CFStringRef dns = cf_text(UTUN_DNS_SERVICE "DNS");
    if (dns) { (void)SCDynamicStoreRemoveValue(store, dns); CFRelease(dns); }
    if (ipv4) { (void)SCDynamicStoreRemoveValue(store, ipv4); CFRelease(ipv4); }
}

static int add_temporary(SCDynamicStoreRef store, const char *key,
                         CFDictionaryRef value, char *reason, size_t reason_cap) {
    CFStringRef name = cf_text(key);
    Boolean added = name && value && SCDynamicStoreAddTemporaryValue(store, name, value);
    if (name) CFRelease(name);
    if (added) return 0;
    int status = SCError();
    if (reason && reason_cap)
        snprintf(reason, reason_cap, "configd refused %s: %s (SCError %d)",
                 key, SCErrorString(status), status);
    return -1;
}

int utun_dns_publish(utun_dns_t *dns, const char *ifname, const char *local4,
                     const char *peer4, const char *dns_server,
                     char *reason, size_t reason_cap) {
    if (reason && reason_cap) reason[0] = '\0';
    if (!dns || !ifname || !local4 || !peer4 || !dns_server) return -1;
    struct in_addr server_address;
    if (inet_pton(AF_INET, dns_server, &server_address) != 1) {
        if (reason && reason_cap) snprintf(reason, reason_cap, "invalid tunnel dns server");
        return -1;
    }
    utun_dns_withdraw(dns);

    CFStringRef name = cf_text("senkod");
    SCDynamicStoreRef store = name ? SCDynamicStoreCreate(kCFAllocatorDefault, name,
                                                          NULL, NULL) : NULL;
    if (name) CFRelease(name);
    if (!store) {
        int status = SCError();
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "cannot open the configd store: %s (SCError %d)",
                     SCErrorString(status), status);
        return -1;
    }
    /* a temporary value cannot replace an existing key, and a key left by a
       manual scutil session would otherwise block every later publish */
    remove_keys(store);

    int primary_service = senko_ios_major() < 10;
    CFMutableDictionaryRef ipv4 = primary_service
        ? CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks)
        : NULL;
    CFMutableDictionaryRef resolver = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int one = 1;
    if (ipv4) {
        put(ipv4, "Addresses", cf_one(local4));
        put(ipv4, "DestAddresses", cf_one(peer4));
        put(ipv4, "InterfaceName", cf_text(ifname));
        put(ipv4, "Router", cf_text(peer4));
        /* makes configd rank the tunnel above wifi, as a vpn service is */
        put(ipv4, "OverridePrimary", CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &one));
    }
    if (resolver) {
        put(resolver, "ServerAddresses", cf_one(dns_server));
        put(resolver, "InterfaceName", cf_text(ifname));
        /* springboard on ios 10 and later hides the wifi glyph when utun becomes
           PrimaryInterface; an empty match domain selects tunnel dns without
           changing it */
        if (!primary_service)
            put(resolver, "SupplementalMatchDomains", cf_one(""));
    }
    int result = -1;
    if ((primary_service && !ipv4) || !resolver) {
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "not enough memory for the configd dns entry");
    } else if (add_temporary(store, UTUN_DNS_SERVICE "DNS", resolver, reason, reason_cap) == 0 &&
               (!primary_service || add_temporary(store, UTUN_DNS_SERVICE "IPv4", ipv4,
                                                   reason, reason_cap) == 0)) {
        result = 0;
    }
    if (ipv4) CFRelease(ipv4);
    if (resolver) CFRelease(resolver);
    if (result != 0) {
        remove_keys(store);
        CFRelease(store);
        return -1;
    }
    dns->store = (void *)store;
    return 0;
}

void utun_dns_withdraw(utun_dns_t *dns) {
    if (!dns || !dns->store) return;
    SCDynamicStoreRef store = (SCDynamicStoreRef)dns->store;
    remove_keys(store);
    CFRelease(store);
    dns->store = NULL;
}

#else

int utun_dns_publish(utun_dns_t *dns, const char *ifname, const char *local4,
                     const char *peer4, const char *dns_server,
                     char *reason, size_t reason_cap) {
    (void)dns; (void)ifname; (void)local4; (void)peer4; (void)dns_server;
    if (reason && reason_cap)
        snprintf(reason, reason_cap, "configd is unavailable on this host build");
    return -1;
}

void utun_dns_withdraw(utun_dns_t *dns) {
    if (dns) dns->store = NULL;
}

#endif
