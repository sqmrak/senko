#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dispatch/dispatch.h>

static const char kSenkoStatusStatePath[] =
    "/var/mobile/Library/Preferences/com.senko.status.state";
/* springboard is the only process that can see which status bar this firmware
   has, and the app cannot read its log. this file is what the logs screen
   shows, so a mode or a failure never has to be guessed from the outside
   again. it is an append-only ring, not one line: a single overwritten line
   cannot show the sequence that led to a wifi glyph report after the fact */
static const char kSenkoStatusLogPath[] =
    "/var/mobile/Library/Preferences/com.senko.status.log";
#define SENKO_STATUS_LOG_CAP (32 * 1024)
/* the app writes this when the user turns the badge off, because the status bar
   on some firmwares drops the wifi glyph to make room for it */
static const char kSenkoStatusOffPath[] =
    "/var/mobile/Library/Preferences/com.senko.status.off";
static const CFStringRef kSenkoStatusNotify =
    CFSTR("com.senko.status.changed");

static BOOL gSenkoReady = NO;
static BOOL gSenkoQueued = NO;
static BOOL gSenkoDidApply = NO;
static BOOL gSenkoLastState = NO;
static unsigned gSenkoRetries = 0;
static BOOL gSenkoHookFailureLogged = NO;

static void SenkoWriteStatus(NSString *line) {
    if (![line length]) return;
    NSString *stamped = [NSString stringWithFormat:@"%.0f thread=%p %@",
                          [[NSDate date] timeIntervalSince1970],
                          (void *)pthread_self(), line];
    const char *text = [stamped UTF8String];
    if (!text) return;

    struct stat st;
    /* a bounded ring: past the cap the log starts over instead of growing
       forever on a device nobody restarts for months */
    if (stat(kSenkoStatusLogPath, &st) == 0 && st.st_size > SENKO_STATUS_LOG_CAP) {
        int trunc = open(kSenkoStatusLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (trunc >= 0) close(trunc);
    }

    int fd = open(kSenkoStatusLogPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    (void)write(fd, text, strlen(text));
    (void)write(fd, "\n", 1);
    close(fd);
    NSLog(@"senko status: %@", line);
}

static NSString *SenkoOSVersion(void) {
    NSDictionary *system = [NSDictionary dictionaryWithContentsOfFile:
        @"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *version = [system objectForKey:@"ProductVersion"];
    return version ? version : @"unknown";
}

static BOOL SenkoReadVPNState(void) {
    char buf[8];
    int fd = open(kSenkoStatusStatePath, O_RDONLY);
    if (fd < 0) return NO;
    ssize_t n = read(fd, buf, sizeof buf);
    close(fd);
    return n > 0 && buf[0] == '1';
}

static id SenkoSharedObject(NSString *className, SEL first, SEL second) {
    Class cls = NSClassFromString(className);
    if (!cls) return nil;

    if ([cls respondsToSelector:first]) {
        IMP imp = [cls methodForSelector:first];
        id (*call)(id, SEL) = (id (*)(id, SEL))imp;
        return call((id)cls, first);
    }
    if ([cls respondsToSelector:second]) {
        IMP imp = [cls methodForSelector:second];
        id (*call)(id, SEL) = (id (*)(id, SEL))imp;
        return call((id)cls, second);
    }
    return nil;
}

static BOOL SenkoCallVoid(id obj, SEL sel) {
    if (!obj || ![obj respondsToSelector:sel]) return NO;
    IMP imp = [obj methodForSelector:sel];
    void (*call)(id, SEL) = (void (*)(id, SEL))imp;
    call(obj, sel);
    return YES;
}

static BOOL gSenkoVPNForced = NO;
/* answer yes only while the status bar is recomputing its vpn item */
static BOOL (*gSenkoOrigUsingVPN)(id, SEL) = NULL;
static void (*gSenkoOrigUpdateVPNItem)(id, SEL) = NULL;

static pthread_key_t gSenkoUpdateDepthKey;
static pthread_once_t gSenkoUpdateDepthOnce = PTHREAD_ONCE_INIT;

static void SenkoUpdateDepthKeyInit(void) {
    pthread_key_create(&gSenkoUpdateDepthKey, free);
}

/* one counter per thread, not a process-wide flag: statuskit does not promise
   the vpn item is recomputed on the main thread, and a global would let a
   forced answer leak into a getter call a different thread makes for an
   unrelated item while this thread is still inside its own update */
static long *SenkoUpdateDepthSlot(void) {
    pthread_once(&gSenkoUpdateDepthOnce, SenkoUpdateDepthKeyInit);
    long *slot = pthread_getspecific(gSenkoUpdateDepthKey);
    if (!slot) {
        slot = calloc(1, sizeof(long));
        if (slot) pthread_setspecific(gSenkoUpdateDepthKey, slot);
    }
    return slot;
}

static NSString * const kSenkoAggregators[] = {
    @"SBStatusBarStateAggregator", @"STStatusBarStateAggregator"
};

/* the tunnel is not a system vpn, so somebody has to answer for it. on ios 13
   and later the status bar derives the wifi glyph from the same telephony
   manager, and answering yes to every caller told it the link was a vpn rather
   than wifi: the glyph went away for as long as the tunnel was up. the forced
   answer is now scoped to the calling thread's own run of the method that
   paints the vpn item */
static BOOL SenkoUsingVPNConnection(id self, SEL _cmd) {
    BOOL original = gSenkoOrigUsingVPN ? gSenkoOrigUsingVPN(self, _cmd) : NO;
    if (!gSenkoVPNForced) return original;
    long *depth = SenkoUpdateDepthSlot();
    if (!depth || *depth <= 0) return original;
    return YES;
}

/* springboard recomputes the item on its own whenever the network changes, and
   it walks through here every time, so the icon survives a rebuild without the
   answer leaking to the rest of the status bar. the depth counter (rather than
   a bool) keeps nested calls on the same thread scoped correctly too */
static void SenkoUpdateVPNItem(id self, SEL _cmd) {
    long *depth = SenkoUpdateDepthSlot();
    if (depth) (*depth)++;
    if (gSenkoOrigUpdateVPNItem) gSenkoOrigUpdateVPNItem(self, _cmd);
    if (depth) (*depth)--;
}

/* ios 6 and 7 keep the same item on the data manager instead of an aggregator */
static NSString * const kSenkoItemOwners[] = {
    @"SBStatusBarStateAggregator", @"STStatusBarStateAggregator",
    @"SBStatusBarDataManager"
};

static SEL gSenkoItemSel = NULL;

/* the item method is not named the same on every firmware and no ios 10
   springboard is at hand to read it from, so the class is asked for a void
   method of no arguments with VPN in its name. the first run also logs every
   such name, which is what a missing badge report needs */
static Method SenkoFindItemMethod(Class cls, NSString *owner) {
    static const char *names[] = { "_updateVPNItem", "updateVPNItem", NULL };
    for (size_t n = 0; names[n]; ++n) {
        Method method = class_getInstanceMethod(cls, sel_registerName(names[n]));
        if (!method) continue;
        const char *types = method_getTypeEncoding(method);
        if (types && types[0] != 'v') continue;
        return method;
    }
    unsigned count = 0;
    Method *list = class_copyMethodList(cls, &count);
    Method found = NULL;
    NSMutableString *seen = [NSMutableString string];
    for (unsigned i = 0; list && i < count; ++i) {
        const char *name = sel_getName(method_getName(list[i]));
        if (!strcasestr(name, "vpn")) continue;
        [seen appendFormat:@" %s", name];
        if (found || method_getNumberOfArguments(list[i]) != 2) continue;
        char ret[4] = "";
        method_getReturnType(list[i], ret, sizeof ret);
        if (ret[0] == 'v' && (strcasestr(name, "update") || strcasestr(name, "refresh")))
            found = list[i];
    }
    free(list);
    if ([seen length])
        SenkoWriteStatus([NSString stringWithFormat:@"%@ vpn methods:%@", owner, seen]);
    return found;
}

static BOOL SenkoInstallItemHook(void) {
    if (gSenkoOrigUpdateVPNItem) return YES;
    for (size_t i = 0; i < sizeof kSenkoItemOwners / sizeof kSenkoItemOwners[0]; ++i) {
        Class cls = NSClassFromString(kSenkoItemOwners[i]);
        if (!cls) continue;
        Method method = SenkoFindItemMethod(cls, kSenkoItemOwners[i]);
        if (!method) continue;
        gSenkoItemSel = method_getName(method);
        gSenkoOrigUpdateVPNItem = (void (*)(id, SEL))
            method_setImplementation(method, (IMP)SenkoUpdateVPNItem);
        SenkoWriteStatus([NSString stringWithFormat:
            @"badge scoped to -[%@ %s]; wifi glyph untouched",
            kSenkoItemOwners[i], sel_getName(gSenkoItemSel)]);
        return YES;
    }
    return NO;
}

static BOOL SenkoInstallVPNHook(void) {
    static const char *names[] = {
        "isUsingVPNConnection", "usingVPNConnection", "isVPNActive", NULL
    };
    /* the item hook decides whether the forced answer can be contained, so it
       goes in before the getter can be asked anything. without it the answer
       would reach every consumer of the getter, and on ios 13 and later that
       includes the data network item the wifi glyph is drawn from. a decorative
       badge is not worth replacing system network state, so senko goes without
       the badge instead */
    if (!SenkoInstallItemHook()) {
        if (!gSenkoHookFailureLogged) {
            gSenkoHookFailureLogged = YES;
            SenkoWriteStatus(@"no vpn item update on this firmware; badge stays "
                             @"pending while springboard initializes");
        }
        return NO;
    }
    if (gSenkoOrigUsingVPN) return YES;
    Class cls = NSClassFromString(@"SBTelephonyManager");
    if (cls) {
        for (size_t i = 0; names[i]; ++i) {
            SEL sel = sel_registerName(names[i]);
            Method method = class_getInstanceMethod(cls, sel);
            if (!method) continue;
            const char *types = method_getTypeEncoding(method);
/* both BOOL encodings return one byte in the same register; anything else
   would be a different method wearing the same name */
            if (types && types[0] != 'c' && types[0] != 'B') continue;
            gSenkoOrigUsingVPN = (BOOL (*)(id, SEL))
                method_setImplementation(method, (IMP)SenkoUsingVPNConnection);
            return YES;
        }
    }
/* the item hook is in place but springboard has not published the getter yet */
    if (!gSenkoHookFailureLogged) {
        gSenkoHookFailureLogged = YES;
        SenkoWriteStatus(@"SBTelephonyManager has no vpn getter; badge stays "
                         @"pending while springboard initializes");
    }
    return NO;
}

/* springboard draws the wifi glyph only while configd's primary interface is
   the wifi interface: SBWiFiManager -_primaryInterfaceChanged: sets the flag
   SBTelephonyManager -dataConnectionType reads (ios 6.1 springboard). a senko
   tunnel is primary while it carries the system dns, so the glyph and the vpn
   badge beside it vanished for as long as it ran. the tunnel still leaves by
   wifi, and senkod writes which interface it rides into the state file */
static void (*gSenkoOrigPrimaryChanged)(id, SEL, BOOL) = NULL;
static BOOL gSenkoWiFiHookTried = NO;

static BOOL SenkoTunnelRidesWiFi(void) {
    char buf[32];
    int fd = open(kSenkoStatusStatePath, O_RDONLY);
    if (fd < 0) return NO;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n < 4) return NO;
    buf[n] = '\0';
    /* ios names its wifi interface en0; cellular is pdp_ip */
    return buf[0] == '1' && buf[1] == ' ' && strncmp(buf + 2, "en", 2) == 0;
}

static NSString *SenkoNetworkFacts(void);

static void SenkoPrimaryInterfaceChanged(id self, SEL _cmd, BOOL primary) {
    if (!primary && SenkoTunnelRidesWiFi()) {
        primary = YES;
        SenkoWriteStatus(@"configd made the tunnel primary; wifi kept, the tunnel rides it");
        gSenkoOrigPrimaryChanged(self, _cmd, primary);
        SenkoWriteStatus(SenkoNetworkFacts());
        return;
    }
    gSenkoOrigPrimaryChanged(self, _cmd, primary);
}

static BOOL SenkoCallBool(id obj, SEL sel, BOOL *out) {
    if (!obj || ![obj respondsToSelector:sel]) return NO;
    BOOL (*call)(id, SEL) = (BOOL (*)(id, SEL))[obj methodForSelector:sel];
    *out = call(obj, sel);
    return YES;
}

/* what the status bar will draw: 8 is wifi on the ios 5.1.1 and 6.1
   springboards, -1 when this firmware has no such getter */
static int SenkoDataConnectionType(void);

/* the three facts the wifi glyph is drawn from, for the log */
static NSString *SenkoNetworkFacts(void) {
    id wifi = SenkoSharedObject(@"SBWiFiManager", @selector(sharedInstance),
                                @selector(sharedWiFiManager));
    id telephony = SenkoSharedObject(@"SBTelephonyManager",
                                     @selector(sharedTelephonyManager),
                                     @selector(sharedInstance));
    BOOL primary = NO, have_primary = SenkoCallBool(wifi, sel_registerName("isPrimaryInterface"), &primary);
    int using_wifi = -1;
    Ivar ivar = telephony ? class_getInstanceVariable([telephony class], "_usingWifi") : NULL;
    if (ivar) using_wifi = (*((unsigned char *)telephony + ivar_getOffset(ivar))) & 1;
    return [NSString stringWithFormat:@"data=%d wifi_primary=%d using_wifi=%d",
            SenkoDataConnectionType(), have_primary ? (int)primary : -1, using_wifi];
}

static int SenkoDataConnectionType(void) {
    id telephony = SenkoSharedObject(@"SBTelephonyManager",
                                     @selector(sharedTelephonyManager),
                                     @selector(sharedInstance));
    SEL sel = sel_registerName("dataConnectionType");
    if (!telephony || ![telephony respondsToSelector:sel]) return -1;
    int (*call)(id, SEL) = (int (*)(id, SEL))[telephony methodForSelector:sel];
    return call(telephony, sel);
}

static void SenkoInstallWiFiHook(void) {
    if (gSenkoOrigPrimaryChanged || gSenkoWiFiHookTried) return;
    Class cls = NSClassFromString(@"SBWiFiManager");
    if (!cls) return; /* springboard may publish it later */
    gSenkoWiFiHookTried = YES;
    SEL sel = sel_registerName("_primaryInterfaceChanged:");
    Method method = class_getInstanceMethod(cls, sel);
    char ret[4] = "", arg[4] = "";
    if (method) {
        method_getReturnType(method, ret, sizeof ret);
        if (method_getNumberOfArguments(method) == 3)
            method_getArgumentType(method, 2, arg, sizeof arg);
    }
    /* a void method taking one BOOL, whichever of the two encodings */
    if (!method || ret[0] != 'v' || (arg[0] != 'c' && arg[0] != 'B')) {
        SenkoWriteStatus(@"no -[SBWiFiManager _primaryInterfaceChanged:] on this "
                         @"firmware; the wifi glyph follows configd alone");
        return;
    }
    gSenkoOrigPrimaryChanged = (void (*)(id, SEL, BOOL))
        method_setImplementation(method, (IMP)SenkoPrimaryInterfaceChanged);
    SenkoWriteStatus(@"wifi glyph kept while a tunnel rides wifi");
}

/* configd already told springboard the tunnel is primary by the time senkod
   writes the state file, so the flag is set again here rather than waiting for
   the next network change */
static void SenkoRefreshWiFi(void) {
    SenkoInstallWiFiHook();
    if (!gSenkoOrigPrimaryChanged || !SenkoTunnelRidesWiFi()) return;
    id wifi = SenkoSharedObject(@"SBWiFiManager", @selector(sharedInstance),
                                @selector(sharedWiFiManager));
    if (!wifi) return;
    SenkoPrimaryInterfaceChanged(wifi, sel_registerName("_primaryInterfaceChanged:"), YES);
}

static BOOL SenkoApplyStatus(BOOL enabled) {
    /* springboard can publish its status classes after the injected library is
       loaded, so an early missing selector must remain retryable */
    if (!SenkoInstallVPNHook()) return NO;
    gSenkoVPNForced = enabled;

/* the aggregator was renamed when statuskit took over the status bar, so both
   names are tried and whichever one the system has answers. only the vpn item
   is refreshed: rebuilding the data network or service items dropped the wifi
   glyph until the next system update */
    BOOL refreshed = NO;
    NSString *via = @"none";
    for (size_t i = 0; i < sizeof kSenkoAggregators / sizeof kSenkoAggregators[0]; ++i) {
        id aggregator = SenkoSharedObject(kSenkoAggregators[i],
                                          @selector(sharedInstance),
                                          @selector(sharedAggregator));
        if (!aggregator) continue;
        if (SenkoCallVoid(aggregator, gSenkoItemSel)) {
            refreshed = YES;
            via = kSenkoAggregators[i];
        }
    }

/* ios 6 and 7 have no aggregator; the data manager owns the same item there */
    if (!refreshed) {
        id data = SenkoSharedObject(@"SBStatusBarDataManager",
                                    @selector(sharedDataManager),
                                    @selector(sharedInstance));
        if (SenkoCallVoid(data, gSenkoItemSel)) {
            refreshed = YES;
            via = @"SBStatusBarDataManager";
        }
    }

/* old status bars have no item refresh and need the telephony republish. modern
   statuskit uses the same republish to rebuild the wifi item, so never call it
   when an aggregator exists */
    BOOL result = refreshed;
    BOOL modernStatusKit = NSClassFromString(@"SBStatusBarStateAggregator") != nil ||
        NSClassFromString(@"STStatusBarStateAggregator") != nil;
    if (!refreshed && !modernStatusKit) {
        id telephony = SenkoSharedObject(@"SBTelephonyManager",
                                         @selector(sharedTelephonyManager),
                                         @selector(sharedInstance));
        result = SenkoCallVoid(telephony, @selector(updateSpringBoard));
        if (result) via = @"SBTelephonyManager.updateSpringBoard";
    }

/* one line per apply, not just the first hook install: the only way a future
   wifi glyph report turns into evidence instead of another guess */
    SenkoWriteStatus([NSString stringWithFormat:
        @"apply enabled=%d via=%@ ios=%@ %@", enabled, via, SenkoOSVersion(),
        SenkoNetworkFacts()]);
    return result;
}

@interface SenkoStatusBridge : NSObject
+ (void)markReady;
+ (void)applyLater;
+ (void)applyNow;
@end

@implementation SenkoStatusBridge

+ (void)markReady {
    if (gSenkoReady) return;
    gSenkoReady = YES;
    [self applyLater];
}

+ (void)applyLater {
    if (gSenkoQueued) return;
    gSenkoQueued = YES;
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(applyNow)
                                               object:nil];
    [self performSelector:@selector(applyNow) withObject:nil afterDelay:0.2];
}

+ (void)applyNow {
    gSenkoQueued = NO;
    if (!gSenkoReady) return;

    BOOL enabled = SenkoReadVPNState() && access(kSenkoStatusOffPath, F_OK) != 0;
    if (gSenkoDidApply && gSenkoLastState == enabled) {
        SenkoRefreshWiFi();
        return;
    }
    BOOL applied = SenkoApplyStatus(enabled);
    /* ios 6 can recalculate the wifi item while publishing the vpn item, so
       restore the physical link after that publication */
    SenkoRefreshWiFi();
    if (applied) {
        gSenkoDidApply = YES;
        gSenkoLastState = enabled;
        gSenkoRetries = 0;
        return;
    }
    if (gSenkoRetries++ < 30) {
        [self performSelector:@selector(applyLater) withObject:nil afterDelay:1.0];
    }
}

@end

static void SenkoStatusNotify(CFNotificationCenterRef center,
                               void *observer,
                               CFStringRef name,
                               const void *object,
                               CFDictionaryRef userInfo) {
    (void)center;
    (void)observer;
    (void)name;
    (void)object;
    (void)userInfo;
    dispatch_async(dispatch_get_main_queue(), ^{
        [SenkoStatusBridge applyLater];
    });
}

__attribute__((constructor))
static void SenkoStatusInit(void) {
    @autoreleasepool {
        NSString *bundle = [[NSBundle mainBundle] bundleIdentifier];
        if (![bundle isEqualToString:@"com.apple.springboard"]) return;
        SenkoWriteStatus([NSString stringWithFormat:@"loaded in springboard ios=%@",
                          SenkoOSVersion()]);

        CFNotificationCenterAddObserver(CFNotificationCenterGetDarwinNotifyCenter(),
                                        NULL,
                                        SenkoStatusNotify,
                                        kSenkoStatusNotify,
                                        NULL,
                                        CFNotificationSuspensionBehaviorDeliverImmediately);

        [[NSNotificationCenter defaultCenter] addObserver:[SenkoStatusBridge class]
                                                 selector:@selector(markReady)
                                                     name:UIApplicationDidFinishLaunchingNotification
                                                   object:nil];

        [SenkoStatusBridge performSelector:@selector(markReady)
                                  withObject:nil
                                  afterDelay:2.0];
    }
}
