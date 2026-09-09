#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <stddef.h>

#include <fcntl.h>
#include <unistd.h>
#include <dispatch/dispatch.h>

static const char kSenkoVPNIconStatePath[] =
    "/var/mobile/Library/Preferences/com.senko.vpnicon.state";
/* springboard is the only process that can see which status bar this firmware
   has, and the app cannot read its log. one line here is what the logs screen
   shows, so the mode never has to be guessed from the outside again */
static const char kSenkoVPNIconStatusPath[] =
    "/var/mobile/Library/Preferences/com.senko.vpnicon.status";
/* the app writes this when the user turns the badge off, because the status bar
   on some firmwares drops the wifi glyph to make room for it */
static const char kSenkoVPNIconOffPath[] =
    "/var/mobile/Library/Preferences/com.senko.vpnicon.off";
static const CFStringRef kSenkoVPNIconNotify =
    CFSTR("com.senko.vpnicon.changed");

static BOOL gSenkoReady = NO;
static BOOL gSenkoQueued = NO;
static BOOL gSenkoDidApply = NO;
static BOOL gSenkoLastState = NO;
static unsigned gSenkoRetries = 0;

static void SenkoWriteStatus(NSString *line) {
    if (![line length]) return;
    const char *text = [line UTF8String];
    if (!text) return;
    int fd = open(kSenkoVPNIconStatusPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    (void)write(fd, text, strlen(text));
    (void)write(fd, "\n", 1);
    close(fd);
    NSLog(@"senko vpnicon: %@", line);
}

static BOOL SenkoIsIOS5(void) {
    NSDictionary *system = [NSDictionary dictionaryWithContentsOfFile:
        @"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *version = [system objectForKey:@"ProductVersion"];
    return [version hasPrefix:@"5."];
}

static BOOL SenkoReadVPNState(void) {
    char buf[8];
    int fd = open(kSenkoVPNIconStatePath, O_RDONLY);
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
/* set once when this firmware exposes no vpn item to scope the answer to */
static BOOL gSenkoBadgeDisabled = NO;
static BOOL (*gSenkoOrigUsingVPN)(id, SEL) = NULL;
static void (*gSenkoOrigUpdateVPNItem)(id, SEL) = NULL;
/* set only while the status bar is recomputing its vpn item */
static BOOL gSenkoInVPNItemUpdate = NO;

static NSString * const kSenkoAggregators[] = {
    @"SBStatusBarStateAggregator", @"STStatusBarStateAggregator"
};

/* the tunnel is not a system vpn, so somebody has to answer for it. on ios 13
   and later the status bar derives the wifi glyph from the same telephony
   manager, and answering yes to every caller told it the link was a vpn rather
   than wifi: the glyph went away for as long as the tunnel was up. the forced
   answer is now scoped to the one method that paints the vpn item */
static BOOL SenkoUsingVPNConnection(id self, SEL _cmd) {
    BOOL original = gSenkoOrigUsingVPN ? gSenkoOrigUsingVPN(self, _cmd) : NO;
    if (!gSenkoVPNForced || !gSenkoInVPNItemUpdate) return original;
    return YES;
}

/* springboard recomputes the item on its own whenever the network changes, and
   it walks through here every time, so the icon survives a rebuild without the
   answer leaking to the rest of the status bar */
static void SenkoUpdateVPNItem(id self, SEL _cmd) {
    BOOL outer = gSenkoInVPNItemUpdate;
    gSenkoInVPNItemUpdate = YES;
    if (gSenkoOrigUpdateVPNItem) gSenkoOrigUpdateVPNItem(self, _cmd);
    gSenkoInVPNItemUpdate = outer;
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
        if (!gSenkoBadgeDisabled) {
            gSenkoBadgeDisabled = YES;
            SenkoWriteStatus(@"no vpn item update on this firmware; badge stays "
                             @"off so the wifi glyph is not replaced");
        }
        return NO;
    }
    if (gSenkoOrigUsingVPN) return YES;
    Class cls = NSClassFromString(@"SBTelephonyManager");
    if (!cls) return NO;
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
    NSLog(@"senko vpnicon: SBTelephonyManager has no vpn getter to answer");
    return NO;
}

static BOOL SenkoApplyVPNIcon(BOOL enabled) {
    /* a firmware without the item is a settled answer, not a transient failure,
       so the caller must not keep retrying it once a second */
    if (gSenkoBadgeDisabled) return YES;
    if (!SenkoInstallVPNHook()) return gSenkoBadgeDisabled;
    gSenkoVPNForced = enabled;

/* the aggregator was renamed when statuskit took over the status bar, so both
   names are tried and whichever one the system has answers. only the vpn item
   is refreshed: rebuilding the data network or service items dropped the wifi
   glyph until the next system update */
    BOOL refreshed = NO;
    for (size_t i = 0; i < sizeof kSenkoAggregators / sizeof kSenkoAggregators[0]; ++i) {
        id aggregator = SenkoSharedObject(kSenkoAggregators[i],
                                          @selector(sharedInstance),
                                          @selector(sharedAggregator));
        if (!aggregator) continue;
        if (SenkoCallVoid(aggregator, NSSelectorFromString(@"_updateVPNItem")) ||
            SenkoCallVoid(aggregator, NSSelectorFromString(@"updateVPNItem")))
            refreshed = YES;
    }

/* ios 6 and 7 have no aggregator; the data manager owns the same item there */
    if (!refreshed) {
        id data = SenkoSharedObject(@"SBStatusBarDataManager",
                                    @selector(sharedDataManager),
                                    @selector(sharedInstance));
        if (SenkoCallVoid(data, NSSelectorFromString(@"_updateVPNItem")) ||
            SenkoCallVoid(data, NSSelectorFromString(@"updateVPNItem")))
            refreshed = YES;
    }

/* SBTelephonyManager republishes the data network type, and the wifi glyph is
   that item: with the tunnel up telephony reports no data service and the glyph
   went away until the next real network change. it is the last resort, for a
   springboard that exposes neither item refresh */
    if (!refreshed) {
        id telephony = SenkoSharedObject(@"SBTelephonyManager",
                                         @selector(sharedTelephonyManager),
                                         @selector(sharedInstance));
        if (!SenkoCallVoid(telephony, @selector(updateSpringBoard)))
            return NO;
    }

    return YES;
}

@interface SenkoVPNIconBridge : NSObject
+ (void)markReady;
+ (void)applyLater;
+ (void)applyNow;
@end

@implementation SenkoVPNIconBridge

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

    BOOL enabled = SenkoReadVPNState() && access(kSenkoVPNIconOffPath, F_OK) != 0;
    if (gSenkoDidApply && gSenkoLastState == enabled) return;
    if (SenkoApplyVPNIcon(enabled)) {
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

static void SenkoVPNIconNotify(CFNotificationCenterRef center,
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
        [SenkoVPNIconBridge applyLater];
    });
}

__attribute__((constructor))
static void SenkoVPNIconInit(void) {
    @autoreleasepool {
        if (SenkoIsIOS5()) return;
        NSString *bundle = [[NSBundle mainBundle] bundleIdentifier];
        if (![bundle isEqualToString:@"com.apple.springboard"]) return;

        CFNotificationCenterAddObserver(CFNotificationCenterGetDarwinNotifyCenter(),
                                        NULL,
                                        SenkoVPNIconNotify,
                                        kSenkoVPNIconNotify,
                                        NULL,
                                        CFNotificationSuspensionBehaviorDeliverImmediately);

        [[NSNotificationCenter defaultCenter] addObserver:[SenkoVPNIconBridge class]
                                                 selector:@selector(markReady)
                                                     name:UIApplicationDidFinishLaunchingNotification
                                                   object:nil];

        [SenkoVPNIconBridge performSelector:@selector(markReady)
                                  withObject:nil
                                  afterDelay:2.0];
    }
}
