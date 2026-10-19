#import "native_vpn.h"

#import <UIKit/UIKit.h>
#import <objc/message.h>
#import <dispatch/dispatch.h>
#import "../daemon/core/control.h"

static NSString * const SenkoNativeProviderID = @"com.senko.app.tunnel";

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

static NSError *SenkoNativeError(NSString *message) {
    return [NSError errorWithDomain:@"com.senko.native-vpn"
                                code:1
                            userInfo:[NSDictionary dictionaryWithObject:
                                      (message ? message : @"native VPN failed")
                                      forKey:NSLocalizedDescriptionKey]];
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
    return SenkoNativeOSAvailable() && SenkoNativeManagerClass() &&
           SenkoNativeProtocolClass();
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
        /* a profile saved by an older install reads "needs an update" in Settings
           and never starts, so it is removed and made again */
        if (found && SenkoNativeConnectionStatus(found) == 0) {
            ((void (*)(id, SEL, void (^)(NSError *)))objc_msgSend)(found,
                @selector(removeFromPreferencesWithCompletionHandler:),
                ^(NSError *removeError) {
                if (removeError) {
                    SenkoNativeFinish(completion, SenkoNativeError(
                        @"the Senko VPN profile needs an update, delete it in Settings, General, VPN and connect again"));
                    return;
                }
                configure([[[SenkoNativeManagerClass() alloc] init] autorelease]);
            });
            return;
        }
        configure(found ? found : [[[SenkoNativeManagerClass() alloc] init] autorelease]);
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
