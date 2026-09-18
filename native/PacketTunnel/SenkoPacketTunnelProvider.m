#import <NetworkExtension/NetworkExtension.h>
#import <Foundation/Foundation.h>

#import <net/if.h>
#import <sys/socket.h>
#import <unistd.h>
#import <string.h>

#import "../senko_native_core.h"

@interface SenkoPacketTunnelProvider : NEPacketTunnelProvider
@end

static int SenkoPacketTunnelFD(void) {
    char name[IFNAMSIZ];
    for (int fd = 0; fd <= 1024; ++fd) {
        socklen_t length = sizeof name;
        memset(name, 0, sizeof name);
        if (getsockopt(fd, 2, 2, name, &length) == 0 &&
            strncmp(name, "utun", 4) == 0)
            return fd;
    }
    return -1;
}

static NSError *SenkoPacketTunnelError(NSString *message) {
    return [NSError errorWithDomain:@"com.senko.native-vpn"
                                code:1
                            userInfo:[NSDictionary dictionaryWithObject:
                                      (message ? message : @"native VPN failed")
                                      forKey:NSLocalizedDescriptionKey]];
}

@implementation SenkoPacketTunnelProvider

- (void)startTunnelWithOptions:(NSDictionary *)options
              completionHandler:(void (^)(NSError *error))completionHandler {
    (void)options;
    NSDictionary *configuration = nil;
    if ([self.protocolConfiguration respondsToSelector:
         @selector(providerConfiguration)])
        configuration = [(id)self.protocolConfiguration providerConfiguration];
    NSString *json = [configuration objectForKey:@"config"];
    NSString *endpoint = [configuration objectForKey:@"endpoint"];
    if (![json isKindOfClass:[NSString class]] || ![json length]) {
        if (completionHandler)
            completionHandler(SenkoPacketTunnelError(@"native VPN configuration is missing"));
        return;
    }

    NEPacketTunnelNetworkSettings *network =
        [[[NEPacketTunnelNetworkSettings alloc]
          initWithTunnelRemoteAddress:@"198.18.0.2"] autorelease];
    NEIPv4Settings *ipv4 =
        [[[NEIPv4Settings alloc] initWithAddresses:
          [NSArray arrayWithObject:@"198.18.0.1"]
                                      subnetMasks:
          [NSArray arrayWithObject:@"255.255.255.0"]] autorelease];
    ipv4.includedRoutes = [NSArray arrayWithObject:[NEIPv4Route defaultRoute]];
    if ([endpoint length] &&
        [NEIPv4Route instancesRespondToSelector:
            @selector(initWithDestinationAddress:subnetMask:)]) {
        NEIPv4Route *excluded = [[[NEIPv4Route alloc]
                                  initWithDestinationAddress:endpoint
                                                 subnetMask:@"255.255.255.255"]
                                 autorelease];
        if (excluded) ipv4.excludedRoutes = [NSArray arrayWithObject:excluded];
    }
    network.IPv4Settings = ipv4;
    if ([network respondsToSelector:@selector(setMTU:)])
        network.MTU = [NSNumber numberWithInt:1500];
    if ([NEDNSSettings instancesRespondToSelector:
         @selector(initWithServers:)])
        network.DNSSettings = [[[NEDNSSettings alloc]
                                initWithServers:[NSArray arrayWithObject:@"1.1.1.1"]]
                               autorelease];

    [self setTunnelNetworkSettings:network completionHandler:^(NSError *error) {
        if (error) {
            if (completionHandler) completionHandler(error);
            return;
        }
        int tunFD = SenkoPacketTunnelFD();
        if (tunFD < 0) {
            if (completionHandler)
                completionHandler(SenkoPacketTunnelError(@"Network Extension utun fd was not found"));
            return;
        }
        NSData *bytes = [json dataUsingEncoding:NSUTF8StringEncoding];
        char detail[512];
        memset(detail, 0, sizeof detail);
        int result = SenkoNativeStart((char *)[bytes bytes], (int)[bytes length],
                                      tunFD, detail, sizeof detail);
        if (result != 0) {
            NSString *message = [NSString stringWithUTF8String:detail];
            if (completionHandler)
                completionHandler(SenkoPacketTunnelError(message));
            return;
        }
        if (completionHandler) completionHandler(nil);
    }];
}

- (void)stopTunnelWithReason:(NEProviderStopReason)reason
            completionHandler:(void (^)(void))completionHandler {
    (void)reason;
    char detail[512];
    memset(detail, 0, sizeof detail);
    int result = SenkoNativeStop(detail, sizeof detail);
    if (result != 0)
        NSLog(@"senko native VPN stop failed: %s", detail);
    if (completionHandler) completionHandler();
}

@end
