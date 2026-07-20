#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>

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

static BOOL SenkoCallBoolSetter(id obj, SEL sel, BOOL value) {
    if (!obj || ![obj respondsToSelector:sel]) return NO;
    IMP imp = [obj methodForSelector:sel];
    void (*call)(id, SEL, BOOL) = (void (*)(id, SEL, BOOL))imp;
    call(obj, sel, value);
    return YES;
}

static BOOL SenkoCallStatusSetter(id obj, SEL sel, int status) {
    if (!obj || ![obj respondsToSelector:sel]) return NO;
    IMP imp = [obj methodForSelector:sel];
    void (*call)(id, SEL, int) = (void (*)(id, SEL, int))imp;
    call(obj, sel, status);
    return YES;
}

static void SenkoCallVoid(id obj, SEL sel) {
    if (!obj || ![obj respondsToSelector:sel]) return;
    IMP imp = [obj methodForSelector:sel];
    void (*call)(id, SEL) = (void (*)(id, SEL))imp;
    call(obj, sel);
}

static BOOL SenkoApplyVPNIcon(BOOL enabled) {
    id telephony = SenkoSharedObject(@"SBTelephonyManager",
                                     @selector(sharedTelephonyManager),
                                     @selector(sharedInstance));
    if (!telephony) return NO;

    BOOL changed = SenkoCallBoolSetter(telephony,
                                       @selector(setIsUsingVPNConnection:),
                                       enabled);
    if (!changed) {
        changed = SenkoCallBoolSetter(telephony,
                                      @selector(_setIsUsingVPNConnection:),
                                      enabled);
    }
    if (!changed) {
        changed = SenkoCallStatusSetter(telephony,
                                        NSSelectorFromString(@"_setVPNConnectionStatus:"),
                                        enabled ? 2 : 0);
    }
    if (!changed) return NO;

    SenkoCallVoid(telephony, @selector(updateSpringBoard));
    SenkoCallVoid(telephony, @selector(_updateSpringBoard));
    id statusBar = SenkoSharedObject(@"SBStatusBarDataManager",
                                     @selector(sharedDataManager),
                                     @selector(sharedInstance));
    SenkoCallVoid(statusBar, @selector(_dataChanged));
    SenkoCallVoid(statusBar, @selector(_updateTimeString));
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
