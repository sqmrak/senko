#import "native_vpn.h"

#import <UIKit/UIKit.h>
#import <objc/message.h>
#import <dispatch/dispatch.h>

static NSString * const SenkoNativeProviderID = @"com.senko.app.tunnel";

static BOOL SenkoNativeOSAvailable(void) {
    NSString *version = [[UIDevice currentDevice] systemVersion];
    NSArray *parts = [version componentsSeparatedByString:@"."];
    NSInteger major = [parts count] ? [[parts objectAtIndex:0] integerValue] : 0;
    return major >= 9;
}

static void SenkoNativeFinish(void (^block)(NSError *), NSError *error) {
    if (!block) return;
    if ([NSThread isMainThread]) block(error);
    else dispatch_async(dispatch_get_main_queue(), ^{ block(error); });
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
        id manager = SenkoNativeFindManager(managers);
        if (!manager) manager = [[[SenkoNativeManagerClass() alloc] init] autorelease];
        [_manager release];
        _manager = [manager retain];

        id protocol = [[SenkoNativeProtocolClass() alloc] init];
        ((void (*)(id, SEL, id))objc_msgSend)(protocol,
                                              @selector(setProviderBundleIdentifier:),
                                              SenkoNativeProviderID);
        ((void (*)(id, SEL, id))objc_msgSend)(protocol,
                                              @selector(setServerAddress:),
                                              @"198.18.0.2");
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
            id connection = ((id (*)(id, SEL))objc_msgSend)(
                manager, @selector(connection));
            NSError *startError = nil;
            BOOL started = ((BOOL (*)(id, SEL, NSError **))objc_msgSend)(
                connection, @selector(startVPNTunnelAndReturnError:), &startError);
            if (!started) {
                SenkoNativeFinish(completion, startError ? startError :
                                  SenkoNativeError(@"Network Extension refused to start"));
                return;
            }
            SenkoNativeFinish(completion, nil);
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
        if (connection)
            ((void (*)(id, SEL))objc_msgSend)(connection, @selector(stopVPNTunnel));
        SenkoNativeFinish(completion, nil);
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

@end
