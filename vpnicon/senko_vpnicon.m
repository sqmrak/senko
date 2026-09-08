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
static const CFStringRef kSenkoVPNIconNotify =
    CFSTR("com.senko.vpnicon.changed");

static BOOL gSenkoReady = NO;
static BOOL gSenkoQueued = NO;
static BOOL gSenkoDidApply = NO;
static BOOL gSenkoLastState = NO;
static unsigned gSenkoRetries = 0;

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
static BOOL (*gSenkoOrigUsingVPN)(id, SEL) = NULL;

static BOOL SenkoUsingVPNConnection(id self, SEL _cmd) {
    if (gSenkoVPNForced) return YES;
    if (gSenkoOrigUsingVPN) return gSenkoOrigUsingVPN(self, _cmd);
    return NO;
}

/* springboard rebuilds the status bar from SBTelephonyManager whenever the
   network changes, so a value written into the manager was overwritten within
   seconds and writing its ivar by offset also cleared the neighbouring wifi
   state. answering the getter keeps every rebuild reporting the tunnel */
static BOOL SenkoInstallVPNHook(void) {
    static const char *names[] = {
        "isUsingVPNConnection", "usingVPNConnection", "isVPNActive", NULL
    };
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
    if (!SenkoInstallVPNHook()) return NO;
    gSenkoVPNForced = enabled;

/* the aggregator was renamed when statuskit took over the status bar, so both
   names are tried and whichever one the system has answers. only the vpn item
   is refreshed: rebuilding the data network or service items dropped the wifi
   glyph until the next system update */
    static NSString * const kAggregators[] = {
        @"SBStatusBarStateAggregator", @"STStatusBarStateAggregator"
    };
    for (size_t i = 0; i < sizeof kAggregators / sizeof kAggregators[0]; ++i) {
        id aggregator = SenkoSharedObject(kAggregators[i],
                                          @selector(sharedInstance),
                                          @selector(sharedAggregator));
        if (!aggregator) continue;
        if (!SenkoCallVoid(aggregator, NSSelectorFromString(@"_updateVPNItem")))
            SenkoCallVoid(aggregator, NSSelectorFromString(@"updateVPNItem"));
    }

    id telephony = SenkoSharedObject(@"SBTelephonyManager",
                                     @selector(sharedTelephonyManager),
                                     @selector(sharedInstance));
    SenkoCallVoid(telephony, @selector(updateSpringBoard));

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

    BOOL enabled = SenkoReadVPNState();
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
