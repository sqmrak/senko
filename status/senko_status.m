#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <stddef.h>

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

static BOOL SenkoInstallItemHook(void) {
    static const char *names[] = { "_updateVPNItem", "updateVPNItem", NULL };
    if (gSenkoOrigUpdateVPNItem) return YES;
    for (size_t i = 0; i < sizeof kSenkoItemOwners / sizeof kSenkoItemOwners[0]; ++i) {
        Class cls = NSClassFromString(kSenkoItemOwners[i]);
        if (!cls) continue;
        for (size_t n = 0; names[n]; ++n) {
            Method method = class_getInstanceMethod(cls, sel_registerName(names[n]));
            if (!method) continue;
            const char *types = method_getTypeEncoding(method);
            if (types && types[0] != 'v') continue;
            gSenkoOrigUpdateVPNItem = (void (*)(id, SEL))
                method_setImplementation(method, (IMP)SenkoUpdateVPNItem);
            SenkoWriteStatus([NSString stringWithFormat:
                @"badge scoped to -[%@ %s]; wifi glyph untouched",
                kSenkoItemOwners[i], names[n]]);
            return YES;
        }
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
        if (SenkoCallVoid(aggregator, NSSelectorFromString(@"_updateVPNItem")) ||
            SenkoCallVoid(aggregator, NSSelectorFromString(@"updateVPNItem"))) {
            refreshed = YES;
            via = kSenkoAggregators[i];
        }
    }

/* ios 6 and 7 have no aggregator; the data manager owns the same item there */
    if (!refreshed) {
        id data = SenkoSharedObject(@"SBStatusBarDataManager",
                                    @selector(sharedDataManager),
                                    @selector(sharedInstance));
        if (SenkoCallVoid(data, NSSelectorFromString(@"_updateVPNItem")) ||
            SenkoCallVoid(data, NSSelectorFromString(@"updateVPNItem"))) {
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
        @"apply enabled=%d via=%@ ios=%@", enabled, via, SenkoOSVersion()]);
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
    if (gSenkoDidApply && gSenkoLastState == enabled) return;
    if (SenkoApplyStatus(enabled)) {
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
