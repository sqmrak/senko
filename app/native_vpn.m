#import "native_vpn.h"

#import <UIKit/UIKit.h>
#import <objc/message.h>
#import <dispatch/dispatch.h>
#import "../daemon/core/control.h"

static NSString * const SenkoNativeProviderID = @"com.senko.app.tunnel";
static NSString * const SenkoNativeRejectedKey = @"SenkoNativeRejectedBuild";
NSString * const SenkoNativeVPNErrorDomain = @"com.senko.native-vpn";

static BOOL SenkoNativeOSAvailable(void) {
#if !defined(__LP64__)
    /* the packaged Go extension has no armv7 slice */
    return NO;
#else
    NSString *version = [[UIDevice currentDevice] systemVersion];
    NSArray *parts = [version componentsSeparatedByString:@"."];
    NSInteger major = [parts count] ? [[parts objectAtIndex:0] integerValue] : 0;
    return major >= 9;
#endif
}

/* the rootful install copies the app to /Applications without PlugIns, so the
   registered bundle has no extension and ios 12 lists any profile pointing at
   it as "needs an update", whatever the payload under /var/jb holds */
static BOOL SenkoNativeExtensionInstalled(void) {
    NSString *plugins = [[NSBundle mainBundle] builtInPlugInsPath];
    if (![plugins length]) return NO;
    BOOL isDirectory = NO;
    NSString *appex = [plugins stringByAppendingPathComponent:@"SenkoTunnel.appex"];
    return [[NSFileManager defaultManager] fileExistsAtPath:appex isDirectory:&isDirectory] &&
           isDirectory;
}

static void SenkoNativeFinish(void (^block)(NSError *), NSError *error) {
    if (!block) return;
    if ([NSThread isMainThread]) block(error);
    else dispatch_async(dispatch_get_main_queue(), ^{ block(error); });
}

static void SenkoNativeTrafficFinish(void (^block)(BOOL, uint64_t, uint64_t),
                                     BOOL known, uint64_t up, uint64_t down) {
    if (!block) return;
    if ([NSThread isMainThread]) block(known, up, down);
    else {
        void (^saved)(BOOL, uint64_t, uint64_t) = [block copy];
        dispatch_async(dispatch_get_main_queue(), ^{
            saved(known, up, down);
            [saved release];
        });
    }
}

static NSError *SenkoNativeErrorCode(NSInteger code, NSString *message) {
    return [NSError errorWithDomain:SenkoNativeVPNErrorDomain
                                code:code
                            userInfo:[NSDictionary dictionaryWithObject:
                                      (message ? message : @"native VPN failed")
                                      forKey:NSLocalizedDescriptionKey]];
}

static NSError *SenkoNativeError(NSString *message) {
    return SenkoNativeErrorCode(1, message);
}

/* a rejection is kept per build: a new build gets one more try, because its
   install runs uicache again */
static BOOL SenkoNativeRejected(void) {
    NSString *build = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"];
    NSString *rejected = [[NSUserDefaults standardUserDefaults] stringForKey:SenkoNativeRejectedKey];
    return build && [rejected isEqualToString:build];
}

static void SenkoNativeReject(void) {
    NSString *build = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"];
    if (!build) return;
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:build forKey:SenkoNativeRejectedKey];
    [defaults synchronize];
}

static id SenkoNativeManagerClass(void) {
    return NSClassFromString(@"NETunnelProviderManager");
}

static id SenkoNativeProtocolClass(void) {
    return NSClassFromString(@"NETunnelProviderProtocol");
}

static void SenkoNativeManagers(void (^done)(NSArray *, NSError *)) {
    Class klass = SenkoNativeManagerClass();
    if (!klass) {
        if (done) done(nil, SenkoNativeError(@"Network Extension is unavailable"));
        return;
    }
    void (*load)(id, SEL, void (^)(NSArray *, NSError *)) =
        (void (*)(id, SEL, void (^)(NSArray *, NSError *)))objc_msgSend;
    load(klass, @selector(loadAllFromPreferencesWithCompletionHandler:), done);
}

static id SenkoNativeFindManager(NSArray *managers) {
    for (id manager in managers) {
        id configuration = ((id (*)(id, SEL))objc_msgSend)(
            manager, @selector(protocolConfiguration));
        id identifier = configuration ?
            ((id (*)(id, SEL))objc_msgSend)(configuration,
                                             @selector(providerBundleIdentifier)) : nil;
        if ([identifier isEqual:SenkoNativeProviderID]) return manager;
    }
    return nil;
}

/* stopVPNTunnel returns while the extension is still going down, and a start
   issued in that window is dropped, which left a server switch on a dead card */
static void SenkoNativeWaitStopped(id connection, void (^completion)(NSError *)) {
    NSInteger (*statusOf)(id, SEL) = (NSInteger (*)(id, SEL))objc_msgSend;
    SEL statusSel = @selector(status);
    void (^done)(NSError *) = [completion copy];
    __block BOOL finished = NO;
    __block id observer = nil;
    void (^finish)(NSError *) = ^(NSError *error) {
        if (finished) return;
        finished = YES;
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
        [observer release];
        observer = nil;
        SenkoNativeFinish(done, error);
        [done release];
    };
    observer = [[[NSNotificationCenter defaultCenter]
        addObserverForName:@"NEVPNStatusDidChangeNotification"
                    object:connection
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
        (void)note;
        if (statusOf(connection, statusSel) <= 1) finish(nil);
    }] retain];
    if (statusOf(connection, statusSel) <= 1) {
        finish(nil);
        return;
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC),
                   dispatch_get_main_queue(), ^{
        finish(SenkoNativeError(@"native VPN did not stop in time"));
    });
}

static NSInteger SenkoNativeConnectionStatus(id manager) {
    id connection = ((id (*)(id, SEL))objc_msgSend)(manager, @selector(connection));
    return connection ? ((NSInteger (*)(id, SEL))objc_msgSend)(connection, @selector(status)) : 0;
}

/* finish is owned by the chain and released when the chain ends */
static void SenkoNativeRemoveNext(NSMutableArray *queue, void (^finish)(NSError *)) {
    if (![queue count]) {
        finish(nil);
        [finish release];
        return;
    }
    id manager = [[queue objectAtIndex:0] retain];
    [queue removeObjectAtIndex:0];
    ((void (*)(id, SEL, void (^)(NSError *)))objc_msgSend)(manager,
        @selector(removeFromPreferencesWithCompletionHandler:),
        ^(NSError *removeError) {
        [manager release];
        if (removeError) {
            finish(removeError);
            [finish release];
            return;
        }
        SenkoNativeRemoveNext(queue, finish);
    });
}

/* removes the profiles one after another and reports the first failure */
static void SenkoNativeRemove(NSArray *managers, void (^done)(NSError *)) {
    SenkoNativeRemoveNext([[managers mutableCopy] autorelease], [done copy]);
}

static void SenkoNativeReadTraffic(id manager,
                                   void (^completion)(BOOL, uint64_t, uint64_t)) {
    id connection = manager ? ((id (*)(id, SEL))objc_msgSend)(
        manager, @selector(connection)) : nil;
    SEL send = @selector(sendProviderMessage:returnError:responseHandler:);
    if (!connection || ![connection respondsToSelector:send]) {
        SenkoNativeTrafficFinish(completion, NO, 0, 0);
        return;
    }
    NSData *request = [@"stats" dataUsingEncoding:NSASCIIStringEncoding];
    void (^done)(BOOL, uint64_t, uint64_t) = [completion copy];
    NSError *error = nil;
    BOOL sent = ((BOOL (*)(id, SEL, NSData *, NSError **, void (^)(NSData *)))objc_msgSend)(
        connection, send, request, &error, ^(NSData *response) {
            uint64_t up = 0, down = 0;
            BOOL known = [response isKindOfClass:[NSData class]] &&
                ctl_parse_stat([response bytes], [response length], &up, &down) == CTL_OK;
            SenkoNativeTrafficFinish(done, known, up, down);
            [done release];
        });
    if (!sent) {
        SenkoNativeTrafficFinish(done, NO, 0, 0);
        [done release];
    }
}

@implementation SenkoNativeVPN

+ (BOOL)available {
    return SenkoNativeOSAvailable() && SenkoNativeExtensionInstalled() &&
           SenkoNativeManagerClass() && SenkoNativeProtocolClass() &&
           !SenkoNativeRejected();
}

- (void)dealloc {
    [_manager release];
    [super dealloc];
}

- (void)startWithConfiguration:(NSString *)json
                 serverAddress:(NSString *)serverAddress
                    completion:(void (^)(NSError *))completion {
    if (![SenkoNativeVPN available]) {
        SenkoNativeFinish(completion,
                          SenkoNativeError(@"native VPN requires iOS 9 or newer"));
        return;
    }
    if (![json isKindOfClass:[NSString class]] || ![json length]) {
        SenkoNativeFinish(completion,
                          SenkoNativeError(@"native VPN configuration is empty"));
        return;
    }

    SenkoNativeManagers(^(NSArray *managers, NSError *loadError) {
        if (loadError) {
            SenkoNativeFinish(completion, loadError);
            return;
        }
        id found = SenkoNativeFindManager(managers);
        void (^configure)(id) = ^(id manager) {
        [_manager release];
        _manager = [manager retain];

        id protocol = [[SenkoNativeProtocolClass() alloc] init];
        ((void (*)(id, SEL, id))objc_msgSend)(protocol,
                                              @selector(setProviderBundleIdentifier:),
                                              SenkoNativeProviderID);
        ((void (*)(id, SEL, id))objc_msgSend)(protocol,
                                              @selector(setServerAddress:),
                                              @"198.18.0.2");
        if ([protocol respondsToSelector:@selector(setDisconnectOnSleep:)])
            ((void (*)(id, SEL, BOOL))objc_msgSend)(protocol,
                @selector(setDisconnectOnSleep:), NO);
        NSMutableDictionary *provider = [NSMutableDictionary dictionaryWithObject:json
                                                                               forKey:@"config"];
        if ([serverAddress length])
            [provider setObject:serverAddress forKey:@"endpoint"];
        ((void (*)(id, SEL, id))objc_msgSend)(protocol,
                                              @selector(setProviderConfiguration:),
                                              provider);
        ((void (*)(id, SEL, id))objc_msgSend)(manager,
                                              @selector(setProtocolConfiguration:),
                                              protocol);
        ((void (*)(id, SEL, id))objc_msgSend)(manager,
                                              @selector(setLocalizedDescription:),
                                              @"Senko");
        ((void (*)(id, SEL, BOOL))objc_msgSend)(manager,
                                                @selector(setEnabled:), YES);
        [protocol release];

        void (*save)(id, SEL, void (^)(NSError *)) =
            (void (*)(id, SEL, void (^)(NSError *)))objc_msgSend;
        save(manager, @selector(saveToPreferencesWithCompletionHandler:),
             ^(NSError *saveError) {
            if (saveError) {
                if ([[saveError domain] isEqualToString:@"NEVPNErrorDomain"] &&
                    [saveError code] == 1 /* NEVPNErrorConfigurationInvalid */) {
                    SenkoNativeReject();
                    SenkoNativeFinish(completion, SenkoNativeErrorCode(
                        SenkoNativeVPNErrorProviderMissing,
                        @"the system refused the Senko packet tunnel profile"));
                    return;
                }
                SenkoNativeFinish(completion, saveError);
                return;
            }
            /* a newly approved VPN has no usable connection until preferences reload */
            save(manager, @selector(loadFromPreferencesWithCompletionHandler:),
                 ^(NSError *reloadError) {
                if (reloadError) {
                    SenkoNativeFinish(completion, reloadError);
                    return;
                }
/* a profile that is still invalid after a fresh save has no extension the
   system can run: Settings words it as "needs an update" and it never
   starts. it is removed and the daemon backend takes the tunnel */
                if (SenkoNativeConnectionStatus(manager) == 0) {
                    SenkoNativeReject();
                    SenkoNativeRemove([NSArray arrayWithObject:manager], ^(NSError *removeError) {
                        if (removeError)
                            NSLog(@"senko: could not remove the unusable VPN profile: %@",
                                  removeError);
                        SenkoNativeFinish(completion, SenkoNativeErrorCode(
                            SenkoNativeVPNErrorProviderMissing,
                            @"the system has no Senko packet tunnel extension"));
                    });
                    return;
                }
                id connection = ((id (*)(id, SEL))objc_msgSend)(
                    manager, @selector(connection));
                NSError *startError = nil;
                BOOL started = connection && ((BOOL (*)(id, SEL, NSError **))objc_msgSend)(
                    connection, @selector(startVPNTunnelAndReturnError:), &startError);
                if (!started) {
                    SenkoNativeFinish(completion, startError ? startError :
                                      SenkoNativeError(@"Network Extension refused to start"));
                    return;
                }
                SenkoNativeFinish(completion, nil);
            });
        });
        };
        /* one profile belongs to the app. copies an older install left, and a
           profile that reads "needs an update", are removed and made again */
        NSMutableArray *stale = [NSMutableArray array];
        id keep = found;
        for (id manager in managers)
            if (manager != found) [stale addObject:manager];
        if (found && SenkoNativeConnectionStatus(found) == 0) {
            [stale addObject:found];
            keep = nil;
        }
        void (^proceed)(void) = ^{
            configure(keep ? keep : [[[SenkoNativeManagerClass() alloc] init] autorelease]);
        };
        if (![stale count]) {
            proceed();
            return;
        }
        SenkoNativeRemove(stale, ^(NSError *removeError) {
            if (removeError) {
                SenkoNativeFinish(completion, SenkoNativeError(
                    @"the Senko VPN profile needs an update, delete it in Settings, General, VPN and connect again"));
                return;
            }
            proceed();
        });
    });
}

- (void)removeInvalidProfiles {
    if (!SenkoNativeOSAvailable() || !SenkoNativeManagerClass()) return;
    SenkoNativeManagers(^(NSArray *managers, NSError *loadError) {
        if (loadError) return;
        NSMutableArray *invalid = [NSMutableArray array];
        BOOL hasExtension = SenkoNativeExtensionInstalled();
/* a profile the user switched off for another VPN can read as invalid too, and
   removing it would make the next connect ask for permission again. without
   the extension no profile can ever run, so all of them go */
        for (id manager in managers)
            if (!hasExtension || (SenkoNativeConnectionStatus(manager) == 0 &&
                ((BOOL (*)(id, SEL))objc_msgSend)(manager, @selector(isEnabled))))
                [invalid addObject:manager];
        if (![invalid count]) return;
        SenkoNativeRemove(invalid, ^(NSError *removeError) {
            if (removeError)
                NSLog(@"senko: could not remove an unusable VPN profile: %@", removeError);
        });
    });
}

- (void)stopWithCompletion:(void (^)(NSError *))completion {
    if (![SenkoNativeVPN available]) {
        SenkoNativeFinish(completion, nil);
        return;
    }
    SenkoNativeManagers(^(NSArray *managers, NSError *loadError) {
        if (loadError) {
            SenkoNativeFinish(completion, loadError);
            return;
        }
        id manager = SenkoNativeFindManager(managers);
        if (!manager) {
            SenkoNativeFinish(completion, nil);
            return;
        }
        [_manager release];
        _manager = [manager retain];
        id connection = ((id (*)(id, SEL))objc_msgSend)(
            manager, @selector(connection));
        if (!connection) {
            SenkoNativeFinish(completion, nil);
            return;
        }
        ((void (*)(id, SEL))objc_msgSend)(connection, @selector(stopVPNTunnel));
        SenkoNativeWaitStopped(connection, completion);
    });
}

- (void)status:(void (^)(NSInteger, NSDate *))completion {
    if (![SenkoNativeVPN available]) {
        if (completion) SenkoNativeFinish(^(NSError *unused) {
            (void)unused;
            completion(1, nil);
        }, nil);
        return;
    }
    SenkoNativeManagers(^(NSArray *managers, NSError *loadError) {
        if (loadError) {
            if (completion) SenkoNativeFinish(^(NSError *unused) {
                (void)unused;
                completion(-1, nil);
            }, nil);
            return;
        }
        id manager = SenkoNativeFindManager(managers);
        if (!manager) {
            if (completion) SenkoNativeFinish(^(NSError *unused) {
                (void)unused;
                completion(1, nil);
            }, nil);
            return;
        }
        [_manager release];
        _manager = [manager retain];
        id connection = ((id (*)(id, SEL))objc_msgSend)(
            manager, @selector(connection));
        NSInteger status = connection
            ? ((NSInteger (*)(id, SEL))objc_msgSend)(connection, @selector(status))
            : 1;
/* the extension's own connection tracks when it actually came up, unlike this
   process which only just asked; nil whenever there never was a connection */
        NSDate *connectedDate = connection
            ? ((NSDate * (*)(id, SEL))objc_msgSend)(connection, @selector(connectedDate))
            : nil;
        if (completion) SenkoNativeFinish(^(NSError *unused) {
            (void)unused;
            completion(status, connectedDate);
        }, nil);
    });
}

- (void)traffic:(void (^)(BOOL, uint64_t, uint64_t))completion {
    if (!completion) return;
    if (![SenkoNativeVPN available]) {
        SenkoNativeTrafficFinish(completion, NO, 0, 0);
        return;
    }
    if (_manager) {
        SenkoNativeReadTraffic(_manager, completion);
        return;
    }
    SenkoNativeManagers(^(NSArray *managers, NSError *loadError) {
        id manager = loadError ? nil : SenkoNativeFindManager(managers);
        if (!manager) {
            SenkoNativeTrafficFinish(completion, NO, 0, 0);
            return;
        }
        [_manager release];
        _manager = [manager retain];
        SenkoNativeReadTraffic(_manager, completion);
    });
}

@end
