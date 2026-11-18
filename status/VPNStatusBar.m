#import "VPNStatusBar.h"

#import <CoreFoundation/CoreFoundation.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <notify.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char kPrimaryStatePath[] =
    "/var/mobile/Library/Preferences/com.senko.status.state";
static const char kRootlessStatePath[] =
    "/var/jb/var/mobile/Library/Preferences/com.senko.status.state";

static const char kPrimaryLogPath[] =
    "/var/mobile/Library/Preferences/com.senko.status.log";
static const char kRootlessLogPath[] =
    "/var/jb/var/mobile/Library/Preferences/com.senko.status.log";
#define VPN_STATUS_LOG_CAP (32 * 1024)

static const char kPrimaryOffPath[] =
    "/var/mobile/Library/Preferences/com.senko.status.off";
static const char kRootlessOffPath[] =
    "/var/jb/var/mobile/Library/Preferences/com.senko.status.off";

static NSString * const kAggregators[] = {
    @"SBStatusBarStateAggregator", @"STStatusBarStateAggregator"
};

/* ios 6 and early 7 keep the same item on the data manager instead of an aggregator */
static NSString * const kItemOwners[] = {
    @"SBStatusBarStateAggregator", @"STStatusBarStateAggregator",
    @"SBStatusBarDataManager"
};

static int gInitialized = 0;
static BOOL gVPNActive = NO;
static BOOL gForcedSystemStatusVPN = NO;
static int gCachedVPNItemIndex = -1;

static BOOL gReady = NO;
static BOOL gQueued = NO;
static BOOL gDidApply = NO;
static BOOL gLastAppliedState = NO;
static BOOL gForceReapply = NO;
static unsigned gRetries = 0;
static BOOL gHookFailureLogged = NO;
static BOOL gKVCFailureLogged = NO;

static void (*gPostApplyCallback)(BOOL enabled, NSString *via) = NULL;

static pthread_mutex_t gHookLock = PTHREAD_MUTEX_INITIALIZER;
static Class gItemOwnerClass = Nil;
static BOOL gIsAggregator = NO;
static SEL gItemSel = NULL;
static void (*gOrigUpdateVPNItem)(id, SEL) = NULL;
static BOOL (*gOrigUsingVPN)(id, SEL) = NULL;

static BOOL gSetItemHooked = NO;
static BOOL gSetItemIsLong = NO;
static BOOL gSetItemReturnsBool = NO;
static BOOL (*gOrigSetItemBoolInt)(id, SEL, int, BOOL) = NULL;
static void (*gOrigSetItemVoidInt)(id, SEL, int, BOOL) = NULL;
static BOOL (*gOrigSetItemBoolLong)(id, SEL, long, BOOL) = NULL;
static void (*gOrigSetItemVoidLong)(id, SEL, long, BOOL) = NULL;

static BOOL gNotifyItemHooked = NO;
static void (*gOrigNotifyItemInt)(id, SEL, int) = NULL;
static void (*gOrigNotifyItemLong)(id, SEL, long) = NULL;

typedef struct {
    long depth;
    BOOL setItemCalled;
    BOOL notified;
} VPNThreadState;

static pthread_key_t gThreadStateKey;
static pthread_once_t gThreadStateOnce = PTHREAD_ONCE_INIT;

static void VPNThreadStateKeyInit(void) {
    pthread_key_create(&gThreadStateKey, free);
}

/* one state struct per thread: statuskit does not promise the vpn item is
   recomputed on the main thread, and a global flag would let a forced answer
   leak into a getter call a different thread makes for an unrelated item */
static VPNThreadState *VPNThreadStateSlot(void) {
    pthread_once(&gThreadStateOnce, VPNThreadStateKeyInit);
    VPNThreadState *slot = (VPNThreadState *)pthread_getspecific(gThreadStateKey);
    if (!slot) {
        slot = (VPNThreadState *)calloc(1, sizeof(VPNThreadState));
        if (slot) pthread_setspecific(gThreadStateKey, slot);
    }
    return slot;
}

static BOOL VPNIsSpringBoardProcess(void) {
    NSString *bundle = [[NSBundle mainBundle] bundleIdentifier];
    return [bundle isEqualToString:@"com.apple.springboard"];
}

static void VPNEnsureParentDir(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) return;
    size_t len = (size_t)(slash - path);
    if (len >= 256) return;
    char dir[256];
    memcpy(dir, path, len);
    dir[len] = '\0';
    if (access(dir, F_OK) == 0) return;
    for (char *p = dir + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        (void)mkdir(dir, 0755);
        *p = '/';
    }
    (void)mkdir(dir, 0755);
}

static int VPNOpenLogFile(int flags) {
    int fd = open(kPrimaryLogPath, flags, 0644);
    if (fd >= 0) return fd;
    if (access("/var/jb", F_OK) == 0) {
        VPNEnsureParentDir(kRootlessLogPath);
        fd = open(kRootlessLogPath, flags, 0644);
    }
    return fd;
}

static const char *VPNActiveLogPath(void) {
    if (access(kPrimaryLogPath, F_OK) == 0 || access("/var/mobile/Library/Preferences", W_OK) == 0) {
        return kPrimaryLogPath;
    }
    if (access("/var/jb", F_OK) == 0) {
        return kRootlessLogPath;
    }
    return kPrimaryLogPath;
}

void VPNStatusBarLog(NSString *line) {
    if (![line length]) return;
    NSString *stamped = [NSString stringWithFormat:@"%.0f thread=%p %@",
                          [[NSDate date] timeIntervalSince1970],
                          (void *)pthread_self(), line];
    const char *text = [stamped UTF8String];
    if (!text) return;

    const char *logPath = VPNActiveLogPath();
    struct stat st;
    if (stat(logPath, &st) == 0 && st.st_size > VPN_STATUS_LOG_CAP) {
        int trunc = VPNOpenLogFile(O_WRONLY | O_CREAT | O_TRUNC);
        if (trunc >= 0) close(trunc);
    }

    int fd = VPNOpenLogFile(O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) return;
    (void)write(fd, text, strlen(text));
    (void)write(fd, "\n", 1);
    close(fd);
    NSLog(@"senko status: %@", line);
}

static NSString *VPNOSVersion(void) {
    NSDictionary *system = [NSDictionary dictionaryWithContentsOfFile:
        @"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *version = [system objectForKey:@"ProductVersion"];
    return version ? version : @"unknown";
}

static ssize_t VPNReadStateBuffer(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
    int fd = open(kPrimaryStatePath, O_RDONLY);
    if (fd < 0 && access(kRootlessStatePath, R_OK) == 0) {
        fd = open(kRootlessStatePath, O_RDONLY);
    }
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    return n;
}

static BOOL VPNReadPersistedState(void) {
    char buf[32];
    ssize_t n = VPNReadStateBuffer(buf, sizeof buf);
    return n > 0 && buf[0] == '1';
}

static BOOL VPNBadgeDisabledByMarker(void) {
    return access(kPrimaryOffPath, F_OK) == 0 || access(kRootlessOffPath, F_OK) == 0;
}

static BOOL VPNSameFile(const char *a, const char *b) {
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) return NO;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static void VPNWriteStateFile(const char *path, const char *body, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 && errno == ENOENT) {
        VPNEnsureParentDir(path);
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    if (fd >= 0) {
        (void)write(fd, body, len);
        close(fd);
    }
}

static void VPNWritePersistedState(BOOL enabled) {
    char body[32];
    if (enabled) {
        char existing[32];
        ssize_t n = VPNReadStateBuffer(existing, sizeof existing);
        if (n > 0 && existing[0] == '1') {
            /* preserve "1 en0\n" written by senkod for legacy Wi-Fi glyph checks */
            return;
        }
        memcpy(body, "1\n", 3);
    } else {
        memcpy(body, "0\n", 3);
    }

    size_t len = strlen(body);
    int fd = open(kPrimaryStatePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 && errno == ENOENT && access("/var/mobile/Library", F_OK) == 0) {
        VPNEnsureParentDir(kPrimaryStatePath);
        fd = open(kPrimaryStatePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    if (fd >= 0) {
        (void)write(fd, body, len);
        close(fd);
        if (access(kRootlessStatePath, F_OK) == 0 &&
            !VPNSameFile(kPrimaryStatePath, kRootlessStatePath)) {
            VPNWriteStateFile(kRootlessStatePath, body, len);
        }
        return;
    }

    if (access("/var/jb", F_OK) == 0) {
        VPNWriteStateFile(kRootlessStatePath, body, len);
    }
}

BOOL VPNStatusBarTunnelRidesWiFi(void) {
    char buf[32];
    ssize_t n = VPNReadStateBuffer(buf, sizeof buf);
    if (n < 4) return NO;
    /* ios names its wifi interface en0; cellular is pdp_ip */
    return buf[0] == '1' && buf[1] == ' ' && strncmp(buf + 2, "en", 2) == 0;
}

static id VPNSharedObject(NSString *className, SEL first, SEL second) {
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

static BOOL VPNCallVoid(id obj, SEL sel) {
    if (!obj || !sel || ![obj respondsToSelector:sel]) return NO;
    IMP imp = [obj methodForSelector:sel];
    void (*call)(id, SEL) = (void (*)(id, SEL))imp;
    call(obj, sel);
    return YES;
}

static BOOL VPNUsingVPNConnection(id self, SEL _cmd) {
    BOOL original = gOrigUsingVPN ? gOrigUsingVPN(self, _cmd) : NO;
    if (!__atomic_load_n(&gVPNActive, __ATOMIC_ACQUIRE)) return original;
    VPNThreadState *tstate = VPNThreadStateSlot();
    if (!tstate || tstate->depth <= 0) return original;
    return YES;
}

static BOOL VPNSetItemCommon(int index, BOOL enabled) {
    VPNThreadState *tstate = VPNThreadStateSlot();
    if (tstate && tstate->depth > 0) {
        __atomic_store_n(&gCachedVPNItemIndex, index, __ATOMIC_RELEASE);
        tstate->setItemCalled = YES;
        if (__atomic_load_n(&gVPNActive, __ATOMIC_ACQUIRE)) {
            enabled = YES;
        }
    }
    return enabled;
}

static BOOL VPNSetItemBoolInt(id self, SEL _cmd, int index, BOOL enabled) {
    enabled = VPNSetItemCommon(index, enabled);
    return gOrigSetItemBoolInt ? gOrigSetItemBoolInt(self, _cmd, index, enabled) : NO;
}

static void VPNSetItemVoidInt(id self, SEL _cmd, int index, BOOL enabled) {
    enabled = VPNSetItemCommon(index, enabled);
    if (gOrigSetItemVoidInt) gOrigSetItemVoidInt(self, _cmd, index, enabled);
}

static BOOL VPNSetItemBoolLong(id self, SEL _cmd, long index, BOOL enabled) {
    enabled = VPNSetItemCommon((int)index, enabled);
    return gOrigSetItemBoolLong ? gOrigSetItemBoolLong(self, _cmd, index, enabled) : NO;
}

static void VPNSetItemVoidLong(id self, SEL _cmd, long index, BOOL enabled) {
    enabled = VPNSetItemCommon((int)index, enabled);
    if (gOrigSetItemVoidLong) gOrigSetItemVoidLong(self, _cmd, index, enabled);
}

static void VPNCallOrigSetItemEnabled(id aggregator, int index, BOOL enabled) {
    SEL sel = @selector(_setItem:enabled:);
    if (!gSetItemHooked) return;
    if (gSetItemIsLong) {
        if (gSetItemReturnsBool && gOrigSetItemBoolLong) {
            (void)gOrigSetItemBoolLong(aggregator, sel, (long)index, enabled);
        } else if (!gSetItemReturnsBool && gOrigSetItemVoidLong) {
            gOrigSetItemVoidLong(aggregator, sel, (long)index, enabled);
        }
    } else {
        if (gSetItemReturnsBool && gOrigSetItemBoolInt) {
            (void)gOrigSetItemBoolInt(aggregator, sel, index, enabled);
        } else if (!gSetItemReturnsBool && gOrigSetItemVoidInt) {
            gOrigSetItemVoidInt(aggregator, sel, index, enabled);
        }
    }
}

static void VPNNotifyItemCommon(int index) {
    VPNThreadState *tstate = VPNThreadStateSlot();
    if (tstate && tstate->depth > 0) {
        __atomic_store_n(&gCachedVPNItemIndex, index, __ATOMIC_RELEASE);
        tstate->notified = YES;
    }
}

static void VPNNotifyItemInt(id self, SEL _cmd, int index) {
    VPNNotifyItemCommon(index);
    if (gOrigNotifyItemInt) gOrigNotifyItemInt(self, _cmd, index);
}

static void VPNNotifyItemLong(id self, SEL _cmd, long index) {
    VPNNotifyItemCommon((int)index);
    if (gOrigNotifyItemLong) gOrigNotifyItemLong(self, _cmd, index);
}

static BOOL VPNEntryIsBoolYes(id entry) {
    if (!entry) return NO;
    BOOL enabled = YES;
    if ([entry respondsToSelector:@selector(isEnabled)]) {
        IMP imp = [entry methodForSelector:@selector(isEnabled)];
        BOOL (*call)(id, SEL) = (BOOL (*)(id, SEL))imp;
        enabled = call(entry, @selector(isEnabled));
    } else if ([entry respondsToSelector:@selector(enabled)]) {
        IMP imp = [entry methodForSelector:@selector(enabled)];
        BOOL (*call)(id, SEL) = (BOOL (*)(id, SEL))imp;
        enabled = call(entry, @selector(enabled));
    }
    if (!enabled) return NO;
    if ([entry respondsToSelector:@selector(boolValue)]) {
        IMP imp = [entry methodForSelector:@selector(boolValue)];
        BOOL (*call)(id, SEL) = (BOOL (*)(id, SEL))imp;
        return call(entry, @selector(boolValue));
    }
    return NO;
}

static BOOL VPNHasKVCReadKey(id obj, const char *ivarName, SEL getterSel) {
    if (!obj) return NO;
    if (getterSel && [obj respondsToSelector:getterSel]) return YES;
    return class_getInstanceVariable([obj class], ivarName) != NULL;
}

static BOOL VPNHasKVCWriteKey(id obj, const char *ivarName, SEL setterSel) {
    if (!obj) return NO;
    if (setterSel && [obj respondsToSelector:setterSel]) return YES;
    return class_getInstanceVariable([obj class], ivarName) != NULL;
}

static BOOL VPNSetSystemStatusVPNEntry(id aggregator,
                                       Class boolEntryCls,
                                       BOOL enabled,
                                       BOOL useDisabledEntry,
                                       BOOL onlyIfNotYes) {
    if (!aggregator || !boolEntryCls) return NO;
    if (!VPNHasKVCReadKey(aggregator, "_data", @selector(_data))) {
        if (!gKVCFailureLogged) {
            gKVCFailureLogged = YES;
            VPNStatusBarLog(@"aggregator has no _data key for KVC");
        }
        return NO;
    }
    id data = [aggregator valueForKey:@"_data"];
    if (!data) return NO;

    if (onlyIfNotYes && VPNHasKVCReadKey(data, "_vpnEntry", @selector(vpnEntry))) {
        id current = [data valueForKey:@"vpnEntry"];
        if (VPNEntryIsBoolYes(current)) return YES;
    }

    if (!VPNHasKVCWriteKey(data, "_vpnEntry", @selector(setVpnEntry:))) {
        if (!gKVCFailureLogged) {
            gKVCFailureLogged = YES;
            VPNStatusBarLog(@"status data has no vpnEntry key for KVC");
        }
        return NO;
    }

    id entry = nil;
    if (!enabled && useDisabledEntry &&
        [boolEntryCls respondsToSelector:@selector(disabledEntry)]) {
        IMP imp = [boolEntryCls methodForSelector:@selector(disabledEntry)];
        id (*call)(id, SEL) = (id (*)(id, SEL))imp;
        entry = call((id)boolEntryCls, @selector(disabledEntry));
    } else if ([boolEntryCls respondsToSelector:@selector(entryWithBoolValue:)]) {
        IMP imp = [boolEntryCls methodForSelector:@selector(entryWithBoolValue:)];
        id (*call)(id, SEL, BOOL) = (id (*)(id, SEL, BOOL))imp;
        entry = call((id)boolEntryCls, @selector(entryWithBoolValue:), enabled);
    }
    if (!entry) return NO;

    [data setValue:entry forKey:@"vpnEntry"];
    return YES;
}

static void VPNNotifyAggregatorItemChanged(id aggregator) {
    if (!aggregator) return;
    int idx = __atomic_load_n(&gCachedVPNItemIndex, __ATOMIC_ACQUIRE);
    SEL notifySel = @selector(_notifyItemChanged:);
    if (idx >= 0 && [aggregator respondsToSelector:notifySel]) {
        Method m = class_getInstanceMethod([aggregator class], notifySel);
        char arg2[8] = "";
        if (m && method_getNumberOfArguments(m) >= 3) {
            method_getArgumentType(m, 2, arg2, sizeof arg2);
        }
        IMP imp = [aggregator methodForSelector:notifySel];
        if (arg2[0] == 'q' || arg2[0] == 'Q' || arg2[0] == 'l' || arg2[0] == 'L') {
            void (*call)(id, SEL, long) = (void (*)(id, SEL, long))imp;
            call(aggregator, notifySel, (long)idx);
        } else {
            void (*call)(id, SEL, int) = (void (*)(id, SEL, int))imp;
            call(aggregator, notifySel, idx);
        }
        return;
    }
    VPNCallVoid(aggregator, @selector(_notifyNonItemDataChanged));
}

static void VPNUpdateVPNItem(id self, SEL _cmd) {
    BOOL active = __atomic_load_n(&gVPNActive, __ATOMIC_ACQUIRE) ? YES : NO;
    Class boolEntryCls = objc_getClass("STStatusBarDataBoolEntry");
    BOOL isAgg = gIsAggregator && gItemOwnerClass && [self isKindOfClass:gItemOwnerClass];
    VPNThreadState *tstate = VPNThreadStateSlot();
    BOOL outermost = (!tstate || tstate->depth == 0);

    if (outermost && isAgg && boolEntryCls) {
        if (active && __atomic_load_n(&gCachedVPNItemIndex, __ATOMIC_ACQUIRE) < 0) {
            /* seed vpnEntry before orig so equality-checking aggregators emit
               _notifyItemChanged: and reveal the dynamic VPN item index */
            VPNSetSystemStatusVPNEntry(self, boolEntryCls, YES, NO, YES);
        } else if (!active && __atomic_load_n(&gForcedSystemStatusVPN, __ATOMIC_ACQUIRE)) {
            /* clear our forced entry before orig so a real NetworkExtension VPN
               can re-enable it while an idle system leaves it disabled */
            VPNSetSystemStatusVPNEntry(self, boolEntryCls, NO, YES, NO);
        }
    }

    if (tstate) {
        if (outermost) {
            tstate->setItemCalled = NO;
            tstate->notified = NO;
        }
        tstate->depth++;
    }

    if (gOrigUpdateVPNItem) {
        gOrigUpdateVPNItem(self, _cmd);
    }

    if (tstate && tstate->depth > 0) {
        tstate->depth--;
    }

    if (outermost && isAgg) {
        if (boolEntryCls) {
            if (active) {
                if (VPNSetSystemStatusVPNEntry(self, boolEntryCls, YES, NO, NO)) {
                    __atomic_store_n(&gForcedSystemStatusVPN, YES, __ATOMIC_RELEASE);
                    VPNNotifyAggregatorItemChanged(self);
                }
            } else if (__atomic_load_n(&gForcedSystemStatusVPN, __ATOMIC_ACQUIRE)) {
                __atomic_store_n(&gForcedSystemStatusVPN, NO, __ATOMIC_RELEASE);
                if (!tstate || !tstate->notified) {
                    VPNNotifyAggregatorItemChanged(self);
                }
            }
        } else if (active && (!tstate || !tstate->setItemCalled)) {
            int idx = __atomic_load_n(&gCachedVPNItemIndex, __ATOMIC_ACQUIRE);
            if (idx >= 0) {
                VPNCallOrigSetItemEnabled(self, idx, YES);
            }
        }
    }
}

static Method VPNFindItemMethod(Class cls, NSString *owner) {
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
        VPNStatusBarLog([NSString stringWithFormat:@"%@ vpn methods:%@", owner, seen]);
    return found;
}

static void VPNInstallAggregatorItemHooks(Class cls) {
    if (!cls) return;

    if (!gSetItemHooked) {
        SEL sel = @selector(_setItem:enabled:);
        Method m = class_getInstanceMethod(cls, sel);
        if (m && method_getNumberOfArguments(m) == 4) {
            char ret[8] = "", arg2[8] = "", arg3[8] = "";
            method_getReturnType(m, ret, sizeof ret);
            method_getArgumentType(m, 2, arg2, sizeof arg2);
            method_getArgumentType(m, 3, arg3, sizeof arg3);
            BOOL retBool = (ret[0] == 'c' || ret[0] == 'B');
            BOOL retVoid = (ret[0] == 'v');
            BOOL arg2Int = (arg2[0] == 'i' || arg2[0] == 'I');
            BOOL arg2Long = (arg2[0] == 'q' || arg2[0] == 'Q' || arg2[0] == 'l' || arg2[0] == 'L');
            BOOL arg3Bool = (arg3[0] == 'c' || arg3[0] == 'B');
            if ((retBool || retVoid) && (arg2Int || arg2Long) && arg3Bool) {
                gSetItemIsLong = arg2Long;
                gSetItemReturnsBool = retBool;
                if (arg2Long) {
                    if (retBool) {
                        gOrigSetItemBoolLong = (BOOL (*)(id, SEL, long, BOOL))
                            method_setImplementation(m, (IMP)VPNSetItemBoolLong);
                    } else {
                        gOrigSetItemVoidLong = (void (*)(id, SEL, long, BOOL))
                            method_setImplementation(m, (IMP)VPNSetItemVoidLong);
                    }
                } else {
                    if (retBool) {
                        gOrigSetItemBoolInt = (BOOL (*)(id, SEL, int, BOOL))
                            method_setImplementation(m, (IMP)VPNSetItemBoolInt);
                    } else {
                        gOrigSetItemVoidInt = (void (*)(id, SEL, int, BOOL))
                            method_setImplementation(m, (IMP)VPNSetItemVoidInt);
                    }
                }
                gSetItemHooked = YES;
            }
        }
    }

    if (!gNotifyItemHooked) {
        SEL sel = @selector(_notifyItemChanged:);
        Method m = class_getInstanceMethod(cls, sel);
        if (m && method_getNumberOfArguments(m) == 3) {
            char ret[8] = "", arg2[8] = "";
            method_getReturnType(m, ret, sizeof ret);
            method_getArgumentType(m, 2, arg2, sizeof arg2);
            BOOL arg2Int = (arg2[0] == 'i' || arg2[0] == 'I');
            BOOL arg2Long = (arg2[0] == 'q' || arg2[0] == 'Q' || arg2[0] == 'l' || arg2[0] == 'L');
            if (ret[0] == 'v' && (arg2Int || arg2Long)) {
                if (arg2Long) {
                    gOrigNotifyItemLong = (void (*)(id, SEL, long))
                        method_setImplementation(m, (IMP)VPNNotifyItemLong);
                } else {
                    gOrigNotifyItemInt = (void (*)(id, SEL, int))
                        method_setImplementation(m, (IMP)VPNNotifyItemInt);
                }
                gNotifyItemHooked = YES;
            }
        }
    }
}

static BOOL VPNInstallItemHook(void) {
    if (gOrigUpdateVPNItem) return YES;
    for (size_t i = 0; i < sizeof kItemOwners / sizeof kItemOwners[0]; ++i) {
        Class cls = NSClassFromString(kItemOwners[i]);
        if (!cls) continue;
        Method method = VPNFindItemMethod(cls, kItemOwners[i]);
        if (!method) continue;
        gItemOwnerClass = cls;
        gIsAggregator = ![kItemOwners[i] isEqualToString:@"SBStatusBarDataManager"];
        gItemSel = method_getName(method);
        if (gIsAggregator) {
            VPNInstallAggregatorItemHooks(cls);
        }
        gOrigUpdateVPNItem = (void (*)(id, SEL))
            method_setImplementation(method, (IMP)VPNUpdateVPNItem);
        VPNStatusBarLog([NSString stringWithFormat:
            @"badge scoped to -[%@ %s]; systemstatus=%d setitem=%d notify=%d",
            kItemOwners[i], sel_getName(gItemSel),
            objc_getClass("STStatusBarDataBoolEntry") != Nil ? 1 : 0,
            gSetItemHooked ? 1 : 0,
            gNotifyItemHooked ? 1 : 0]);
        return YES;
    }
    return NO;
}

static void VPNInstallTelephonyHook(void) {
    static const char *names[] = {
        "isUsingVPNConnection", "usingVPNConnection", "isVPNActive", NULL
    };
    if (gOrigUsingVPN) return;
    Class cls = NSClassFromString(@"SBTelephonyManager");
    if (!cls) return;
    for (size_t i = 0; names[i]; ++i) {
        SEL sel = sel_registerName(names[i]);
        Method method = class_getInstanceMethod(cls, sel);
        if (!method) continue;
        const char *types = method_getTypeEncoding(method);
        if (types && types[0] != 'c' && types[0] != 'B') continue;
        gOrigUsingVPN = (BOOL (*)(id, SEL))
            method_setImplementation(method, (IMP)VPNUsingVPNConnection);
        return;
    }
}

static BOOL VPNInstallHooks(void) {
    pthread_mutex_lock(&gHookLock);
    if (!VPNInstallItemHook()) {
        pthread_mutex_unlock(&gHookLock);
        if (!gHookFailureLogged) {
            gHookFailureLogged = YES;
            VPNStatusBarLog(@"no vpn item update on this firmware; badge stays "
                            @"pending while springboard initializes");
        }
        return NO;
    }

    VPNInstallTelephonyHook();

    BOOL hasSystemStatus = (objc_getClass("STStatusBarDataBoolEntry") != Nil);
    BOOL ready = gIsAggregator
        ? (hasSystemStatus || gSetItemHooked || gOrigUsingVPN != NULL)
        : (gOrigUsingVPN != NULL);
    pthread_mutex_unlock(&gHookLock);

    if (!ready && !gHookFailureLogged) {
        gHookFailureLogged = YES;
        VPNStatusBarLog(@"SBTelephonyManager has no vpn getter; badge stays "
                        @"pending while springboard initializes");
    }
    return ready;
}

static BOOL VPNApplyStatus(BOOL enabled) {
    if (!VPNInstallHooks()) return NO;

    BOOL refreshed = NO;
    NSString *via = @"none";
    for (size_t i = 0; i < sizeof kAggregators / sizeof kAggregators[0]; ++i) {
        id aggregator = VPNSharedObject(kAggregators[i],
                                        @selector(sharedInstance),
                                        @selector(sharedAggregator));
        if (!aggregator) continue;
        if (VPNCallVoid(aggregator, gItemSel)) {
            refreshed = YES;
            via = kAggregators[i];
        }
    }

    if (!refreshed) {
        id data = VPNSharedObject(@"SBStatusBarDataManager",
                                    @selector(sharedDataManager),
                                    @selector(sharedInstance));
        if (VPNCallVoid(data, gItemSel)) {
            refreshed = YES;
            via = @"SBStatusBarDataManager";
        }
    }

    BOOL result = refreshed;
    BOOL modernStatusKit = NSClassFromString(@"SBStatusBarStateAggregator") != nil ||
        NSClassFromString(@"STStatusBarStateAggregator") != nil;
    if (!refreshed && !modernStatusKit) {
        id telephony = VPNSharedObject(@"SBTelephonyManager",
                                       @selector(sharedTelephonyManager),
                                       @selector(sharedInstance));
        result = VPNCallVoid(telephony, @selector(updateSpringBoard));
        if (result) via = @"SBTelephonyManager.updateSpringBoard";
    }

    VPNStatusBarLog([NSString stringWithFormat:
        @"apply enabled=%d via=%@ ios=%@ idx=%d",
        enabled, via, VPNOSVersion(),
        __atomic_load_n(&gCachedVPNItemIndex, __ATOMIC_ACQUIRE)]);
    if (gPostApplyCallback) {
        gPostApplyCallback(enabled, via);
    }
    return result;
}

@interface VPNStatusBarBridge : NSObject
+ (void)markReady;
+ (void)applyLater;
+ (void)applyNow;
@end

@implementation VPNStatusBarBridge

+ (void)markReady {
    if (gReady) return;
    gReady = YES;
    [self applyLater];
}

+ (void)applyLater {
    if (gQueued) return;
    gQueued = YES;
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(applyNow)
                                               object:nil];
    [self performSelector:@selector(applyNow) withObject:nil afterDelay:0.2];
}

+ (void)applyNow {
    gQueued = NO;
    if (!gReady) return;

    BOOL enabled = __atomic_load_n(&gVPNActive, __ATOMIC_ACQUIRE) ? YES : NO;
    BOOL force = gForceReapply;
    gForceReapply = NO;
    if (gDidApply && gLastAppliedState == enabled && !force) {
        if (gPostApplyCallback) gPostApplyCallback(enabled, @"cached");
        return;
    }
    BOOL applied = VPNApplyStatus(enabled);
    if (applied) {
        gDidApply = YES;
        gLastAppliedState = enabled;
        gRetries = 0;
        return;
    }
    if (gRetries++ < 30) {
        [self performSelector:@selector(applyLater) withObject:nil afterDelay:1.0];
    }
}

@end

static void VPNScheduleApply(BOOL immediate) {
    void (^work)(void) = ^{
        gRetries = 0;
        if (immediate) {
            gForceReapply = YES;
            if (gReady) {
                [NSObject cancelPreviousPerformRequestsWithTarget:[VPNStatusBarBridge class]
                                                         selector:@selector(applyNow)
                                                           object:nil];
                [VPNStatusBarBridge applyNow];
                return;
            }
        }
        [VPNStatusBarBridge applyLater];
    };

    if ([NSThread isMainThread]) {
        work();
    } else {
        dispatch_async(dispatch_get_main_queue(), work);
    }
}

void VPNStatusBarSetPostApplyCallback(void (*callback)(BOOL enabled, NSString *via)) {
    gPostApplyCallback = callback;
}

BOOL IsVPNStatusBarEnabled(void) {
    if (!__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE) && VPNIsSpringBoardProcess()) {
        return VPNReadPersistedState() && !VPNBadgeDisabledByMarker();
    }
    return __atomic_load_n(&gVPNActive, __ATOMIC_ACQUIRE) ? YES : NO;
}

void SetVPNStatusBarEnabled(BOOL enabled) {
    if (!VPNIsSpringBoardProcess()) return;
    if (!__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE)) {
        InitVPNStatusBar();
    }
    BOOL normalized = enabled ? YES : NO;
    __atomic_store_n(&gVPNActive, normalized, __ATOMIC_RELEASE);
    VPNWritePersistedState(normalized);
    VPNScheduleApply(YES);
}

void SyncVPNStatusBarFromState(void) {
    if (!VPNIsSpringBoardProcess()) return;
    BOOL enabled = VPNReadPersistedState() && !VPNBadgeDisabledByMarker();
    __atomic_store_n(&gVPNActive, enabled, __ATOMIC_RELEASE);
    VPNScheduleApply(NO);
}

static int gSenkoNotifyToken = NOTIFY_TOKEN_INVALID;
static int gStartNotifyToken = NOTIFY_TOKEN_INVALID;
static int gStopNotifyToken = NOTIFY_TOKEN_INVALID;

void InitVPNStatusBar(void) {
    if (!VPNIsSpringBoardProcess()) return;
    if (__atomic_exchange_n(&gInitialized, 1, __ATOMIC_ACQ_REL) != 0) return;

    @autoreleasepool {
        VPNStatusBarLog([NSString stringWithFormat:@"loaded in springboard ios=%@",
                         VPNOSVersion()]);

        BOOL initial = VPNReadPersistedState() && !VPNBadgeDisabledByMarker();
        __atomic_store_n(&gVPNActive, initial, __ATOMIC_RELEASE);

        notify_register_dispatch(SENKO_STATUS_CHANGED_NOTIFICATION,
                                 &gSenkoNotifyToken,
                                 dispatch_get_main_queue(),
                                 ^(int token) {
            (void)token;
            SyncVPNStatusBarFromState();
        });

        notify_register_dispatch(SENKO_VPN_STARTED_NOTIFICATION,
                                 &gStartNotifyToken,
                                 dispatch_get_main_queue(),
                                 ^(int token) {
            (void)token;
            SetVPNStatusBarEnabled(YES);
        });

        notify_register_dispatch(SENKO_VPN_STOPPED_NOTIFICATION,
                                 &gStopNotifyToken,
                                 dispatch_get_main_queue(),
                                 ^(int token) {
            (void)token;
            SetVPNStatusBarEnabled(NO);
        });

        void (^scheduleReady)(void) = ^{
            [[NSNotificationCenter defaultCenter]
                addObserver:[VPNStatusBarBridge class]
                   selector:@selector(markReady)
                       name:UIApplicationDidFinishLaunchingNotification
                     object:nil];

            [VPNStatusBarBridge performSelector:@selector(markReady)
                                       withObject:nil
                                       afterDelay:2.0];
        };
        if ([NSThread isMainThread]) {
            scheduleReady();
        } else {
            dispatch_async(dispatch_get_main_queue(), scheduleReady);
        }
    }
}
