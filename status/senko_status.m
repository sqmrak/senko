#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stddef.h>

#import "VPNStatusBar.h"

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

static BOOL SenkoCallBool(id obj, SEL sel, BOOL *out) {
    if (!obj || ![obj respondsToSelector:sel]) return NO;
    BOOL (*call)(id, SEL) = (BOOL (*)(id, SEL))[obj methodForSelector:sel];
    *out = call(obj, sel);
    return YES;
}

/* what the status bar will draw: 8 is wifi on the ios 5.1.1 and 6.1
   springboards, -1 when this firmware has no such getter */
static int SenkoDataConnectionType(void) {
    id telephony = SenkoSharedObject(@"SBTelephonyManager",
                                     @selector(sharedTelephonyManager),
                                     @selector(sharedInstance));
    SEL sel = sel_registerName("dataConnectionType");
    if (!telephony || ![telephony respondsToSelector:sel]) return -1;
    int (*call)(id, SEL) = (int (*)(id, SEL))[telephony methodForSelector:sel];
    return call(telephony, sel);
}

/* the three facts the wifi glyph is drawn from, for the log */
static NSString *SenkoNetworkFacts(void) {
    id wifi = SenkoSharedObject(@"SBWiFiManager", @selector(sharedInstance),
                                @selector(sharedWiFiManager));
    id telephony = SenkoSharedObject(@"SBTelephonyManager",
                                     @selector(sharedTelephonyManager),
                                     @selector(sharedInstance));
    BOOL primary = NO;
    BOOL have_primary = SenkoCallBool(wifi, sel_registerName("isPrimaryInterface"), &primary);
    int using_wifi = -1;
    Ivar ivar = telephony ? class_getInstanceVariable([telephony class], "_usingWifi") : NULL;
#if __has_feature(objc_arc)
    if (ivar) using_wifi = (*((const unsigned char *)(__bridge const void *)telephony + ivar_getOffset(ivar))) & 1;
#else
    if (ivar) using_wifi = (*((const unsigned char *)telephony + ivar_getOffset(ivar))) & 1;
#endif
    return [NSString stringWithFormat:@"data=%d wifi_primary=%d using_wifi=%d",
            SenkoDataConnectionType(), have_primary ? (int)primary : -1, using_wifi];
}

/* springboard draws the wifi glyph only while configd's primary interface is
   the wifi interface: SBWiFiManager -_primaryInterfaceChanged: sets the flag
   SBTelephonyManager -dataConnectionType reads (ios 6.1 springboard). a senko
   tunnel is primary while it carries the system dns, so the glyph and the vpn
   badge beside it vanished for as long as it ran. the tunnel still leaves by
   wifi, and senkod writes which interface it rides into the state file */
static void (*gSenkoOrigPrimaryChanged)(id, SEL, BOOL) = NULL;
static BOOL gSenkoWiFiHookTried = NO;

static BOOL SenkoWiFiIsAssociated(id wifi) {
    if (!wifi) return NO;
    BOOL associated = YES;
    if (SenkoCallBool(wifi, sel_registerName("isAssociated"), &associated))
        return associated;
    return YES;
}

static void SenkoPrimaryInterfaceChanged(id self, SEL _cmd, BOOL primary) {
    if (!primary && VPNStatusBarTunnelRidesWiFi() && SenkoWiFiIsAssociated(self)) {
        primary = YES;
        VPNStatusBarLog(@"configd made the tunnel primary; wifi kept, the tunnel rides it");
        gSenkoOrigPrimaryChanged(self, _cmd, primary);
        VPNStatusBarLog(SenkoNetworkFacts());
        return;
    }
    gSenkoOrigPrimaryChanged(self, _cmd, primary);
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
        VPNStatusBarLog(@"no -[SBWiFiManager _primaryInterfaceChanged:] on this "
                        @"firmware; the wifi glyph follows configd alone");
        return;
    }
    gSenkoOrigPrimaryChanged = (void (*)(id, SEL, BOOL))
        method_setImplementation(method, (IMP)SenkoPrimaryInterfaceChanged);
    VPNStatusBarLog(@"wifi glyph kept while a tunnel rides wifi");
}

/* configd already told springboard the tunnel is primary by the time senkod
   writes the state file, so the flag is set again here rather than waiting for
   the next network change */
static void SenkoRefreshWiFi(void) {
    SenkoInstallWiFiHook();
    if (!gSenkoOrigPrimaryChanged || !VPNStatusBarTunnelRidesWiFi()) return;
    id wifi = SenkoSharedObject(@"SBWiFiManager", @selector(sharedInstance),
                                @selector(sharedWiFiManager));
    if (!wifi || !SenkoWiFiIsAssociated(wifi)) return;
    SenkoPrimaryInterfaceChanged(wifi, sel_registerName("_primaryInterfaceChanged:"), YES);
}

static void SenkoPostApply(BOOL enabled, NSString *via) {
    (void)enabled;
    SenkoRefreshWiFi();
    if (![via isEqualToString:@"cached"]) {
        VPNStatusBarLog(SenkoNetworkFacts());
    }
}

__attribute__((constructor))
static void SenkoStatusInit(void) {
    @autoreleasepool {
        NSString *bundle = [[NSBundle mainBundle] bundleIdentifier];
        if (![bundle isEqualToString:@"com.apple.springboard"]) return;
        VPNStatusBarSetPostApplyCallback(SenkoPostApply);
        InitVPNStatusBar();
    }
}
