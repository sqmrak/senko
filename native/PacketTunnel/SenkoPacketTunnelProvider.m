#import <NetworkExtension/NetworkExtension.h>
#import <Foundation/Foundation.h>

#import <net/if.h>
#import <ifaddrs.h>
#import <sys/socket.h>
#import <unistd.h>
#import <stdio.h>
#import <string.h>

#import "../senko_native_core.h"
#import "../../daemon/core/control.h"
#import "../../daemon/core/traffic.h"

@interface SenkoPacketTunnelProvider : NEPacketTunnelProvider {
    char _tunName[IFNAMSIZ];
    traffic_counter_t _upload;
    traffic_counter_t _download;
}
@end

static int SenkoPacketTunnelFD(char *name, size_t nameCap) {
    if (!name || nameCap < IFNAMSIZ) return -1;
    for (int fd = 0; fd <= 1024; ++fd) {
        socklen_t length = (socklen_t)nameCap;
        memset(name, 0, nameCap);
        if (getsockopt(fd, 2, 2, name, &length) == 0) {
            name[nameCap - 1] = '\0';
            if (strncmp(name, "utun", 4) == 0) return fd;
        }
    }
    return -1;
}

static int SenkoPacketTunnelBytes(const char *name, uint32_t *up, uint32_t *down) {
    struct ifaddrs *addresses = NULL;
    int found = 0;
    if (!name || !name[0] || !up || !down || getifaddrs(&addresses) != 0)
        return -1;
    for (struct ifaddrs *a = addresses; a; a = a->ifa_next) {
        if (!a->ifa_addr || !a->ifa_data || !a->ifa_name ||
            a->ifa_addr->sa_family != AF_LINK || strcmp(a->ifa_name, name) != 0)
            continue;
        const struct if_data *data = (const struct if_data *)a->ifa_data;
        *up = data->ifi_obytes;
        *down = data->ifi_ibytes;
        found = 1;
        break;
    }
    freeifaddrs(addresses);
    return found ? 0 : -1;
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
    /* apps that prefer IPv6 must enter the same tunnel as Safari's IPv4 fallback */
    NEIPv6Settings *ipv6 = [[[NEIPv6Settings alloc]
        initWithAddresses:[NSArray arrayWithObject:@"fd00:7365:6e6b::1"]
        networkPrefixLengths:[NSArray arrayWithObject:[NSNumber numberWithInt:64]]]
        autorelease];
    ipv6.includedRoutes = [NSArray arrayWithObject:[NEIPv6Route defaultRoute]];
    network.IPv6Settings = ipv6;
    if ([network respondsToSelector:@selector(setMTU:)])
        network.MTU = [NSNumber numberWithInt:1500];
    if ([NEDNSSettings instancesRespondToSelector:
         @selector(initWithServers:)])
    {
        NEDNSSettings *dns = [[[NEDNSSettings alloc]
            initWithServers:[NSArray arrayWithObject:@"1.1.1.1"]] autorelease];
        dns.matchDomains = [NSArray arrayWithObject:@""];
        network.DNSSettings = dns;
    }

    [self setTunnelNetworkSettings:network completionHandler:^(NSError *error) {
        if (error) {
            if (completionHandler) completionHandler(error);
            return;
        }
        char tunName[IFNAMSIZ];
        int tunFD = SenkoPacketTunnelFD(tunName, sizeof tunName);
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
        uint32_t up = 0, down = 0;
        int sampled = SenkoPacketTunnelBytes(tunName, &up, &down) == 0;
        @synchronized (self) {
            snprintf(_tunName, sizeof _tunName, "%s", tunName);
            memset(&_upload, 0, sizeof _upload);
            memset(&_download, 0, sizeof _download);
            if (sampled) {
                _upload.previous = up;
                _download.previous = down;
            }
        }
        if (completionHandler) completionHandler(nil);
    }];
}

- (void)handleAppMessage:(NSData *)messageData
       completionHandler:(void (^)(NSData *responseData))completionHandler {
    static const char request[] = "stats";
    if (!completionHandler) return;
    if (![messageData isKindOfClass:[NSData class]] ||
        [messageData length] != sizeof request - 1 ||
        memcmp([messageData bytes], request, sizeof request - 1) != 0) {
        completionHandler(nil);
        return;
    }
    NSData *response = nil;
    @synchronized (self) {
        uint32_t up = 0, down = 0;
        if (_tunName[0] && SenkoNativeIsRunning() &&
            SenkoPacketTunnelBytes(_tunName, &up, &down) == 0) {
            traffic_counter_update(&_upload, up);
            traffic_counter_update(&_download, down);
            char line[64];
            size_t length = 0;
            if (ctl_build_stat(_upload.bytes, _download.bytes,
                               line, sizeof line, &length) == CTL_OK)
                response = [NSData dataWithBytes:line length:length];
        }
    }
    completionHandler(response);
}

- (void)stopTunnelWithReason:(NEProviderStopReason)reason
            completionHandler:(void (^)(void))completionHandler {
    NSLog(@"senko native VPN stopped, reason %ld", (long)reason);
    char detail[512];
    memset(detail, 0, sizeof detail);
    int result = SenkoNativeStop(detail, sizeof detail);
    @synchronized (self) {
        _tunName[0] = '\0';
        memset(&_upload, 0, sizeof _upload);
        memset(&_download, 0, sizeof _download);
    }
    if (result != 0)
        NSLog(@"senko native VPN stop failed: %s", detail);
    if (completionHandler) completionHandler();
}

@end
