#import "native_config.h"
#import "control_client.h"

#include "../daemon/core/config.h"
#include "../daemon/core/blake2b256.h"
#include "../daemon/go_config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#import <dispatch/dispatch.h>

static NSString *SenkoNativeString(const char *text) {
    return [NSString stringWithUTF8String:text ? text : ""];
}

static NSString *SenkoNativeEndpoint(const char *host, NSString **error) {
    struct addrinfo hints;
    struct addrinfo *list = NULL;
    struct addrinfo *it;
    char address[INET_ADDRSTRLEN];
    int result;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    result = getaddrinfo(host, NULL, &hints, &list);
    if (result != 0) {
        if (error) *error = [NSString stringWithFormat:@"cannot resolve %@: %s",
                              SenkoNativeString(host), gai_strerror(result)];
        return nil;
    }
    for (it = list; it; it = it->ai_next) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)it->ai_addr;
        if (inet_ntop(AF_INET, &sin->sin_addr, address, sizeof address)) {
            NSString *value = [NSString stringWithUTF8String:address];
            freeaddrinfo(list);
            return value;
        }
    }
    freeaddrinfo(list);
    if (error) *error = [NSString stringWithFormat:@"%@ has no IPv4 address",
                          SenkoNativeString(host)];
    return nil;
}

static NSString *SenkoNativeServerRemark(const vl_server_t *server) {
    if (server->remark[0]) return SenkoNativeString(server->remark);
    return [NSString stringWithFormat:@"%@:%u", SenkoNativeString(server->host),
            (unsigned)server->port];
}

static NSString *SenkoNativeEscaped(NSString *value) {
    NSString *escaped = [value stringByAddingPercentEscapesUsingEncoding:
                         NSUTF8StringEncoding];
    return escaped ? escaped : value;
}

static NSString *SenkoNativeLinkFromParsed(const vl_server_t *server) {
    NSString *scheme;
    NSString *authority;
    NSMutableArray *query = [NSMutableArray array];
    if (server->proto == VL_PROTO_VLESS) {
        scheme = @"vless";
        authority = [NSString stringWithFormat:@"%s@%s:%u", server->uuid,
                     server->host, (unsigned)server->port];
        [query addObject:[NSString stringWithFormat:@"encryption=%@",
                          SenkoNativeEscaped(SenkoNativeString(server->encryption[0]
                              ? server->encryption : "none"))]];
    } else if (server->proto == VL_PROTO_TROJAN) {
        scheme = @"trojan";
        authority = [NSString stringWithFormat:@"%s@%s:%u", server->pass,
                     server->host, (unsigned)server->port];
    } else if (server->proto == VL_PROTO_HYSTERIA2) {
        scheme = @"hysteria2";
        NSString *portPart = server->port_hop[0]
            ? SenkoNativeString(server->port_hop)
            : [NSString stringWithFormat:@"%u", (unsigned)server->port];
        authority = [NSString stringWithFormat:@"%s@%s:%@", server->pass,
                     server->host, portPart];
    } else {
        return nil;
    }
    [query addObject:[NSString stringWithFormat:@"type=%s",
                      server->net == VL_NET_WS ? "ws" :
                      server->net == VL_NET_GRPC ? "grpc" :
                      server->net == VL_NET_XHTTP ? "xhttp" : "tcp"]];
    [query addObject:[NSString stringWithFormat:@"security=%s",
                      vl_sec_name(server->security)]];
    if (server->sni[0])
        [query addObject:[NSString stringWithFormat:@"sni=%@",
                          SenkoNativeEscaped(SenkoNativeString(server->sni))]];
    if (server->fp[0])
        [query addObject:[NSString stringWithFormat:@"fp=%@",
                          SenkoNativeEscaped(SenkoNativeString(server->fp))]];
    if (server->pbk[0])
        [query addObject:[NSString stringWithFormat:@"pbk=%@",
                          SenkoNativeEscaped(SenkoNativeString(server->pbk))]];
    if (server->sid[0])
        [query addObject:[NSString stringWithFormat:@"sid=%@",
                          SenkoNativeEscaped(SenkoNativeString(server->sid))]];
    if (server->flow[0])
        [query addObject:[NSString stringWithFormat:@"flow=%@",
                          SenkoNativeEscaped(SenkoNativeString(server->flow))]];
    if (server->path[0])
        [query addObject:[NSString stringWithFormat:@"%@=%@",
                          server->net == VL_NET_GRPC ? @"serviceName" : @"path",
                          SenkoNativeEscaped(SenkoNativeString(server->path))]];
    if (server->ws_host[0])
        [query addObject:[NSString stringWithFormat:@"host=%@",
                          SenkoNativeEscaped(SenkoNativeString(server->ws_host))]];
    if (server->proto == VL_PROTO_HYSTERIA2 && server->pin_sha256[0])
        [query addObject:[NSString stringWithFormat:@"pinSHA256=%@",
                          SenkoNativeEscaped(SenkoNativeString(server->pin_sha256))]];
    if (server->proto == VL_PROTO_HYSTERIA2 && server->obfs[0]) {
        [query addObject:[NSString stringWithFormat:@"obfs=%@",
                          SenkoNativeEscaped(SenkoNativeString(server->obfs))]];
        if (server->obfs_password[0])
            [query addObject:[NSString stringWithFormat:@"obfs-password=%@",
                              SenkoNativeEscaped(SenkoNativeString(server->obfs_password))]];
    }
    return [NSString stringWithFormat:@"%@://%@?%@#%@",
            scheme, authority, [query componentsJoinedByString:@"&"],
            SenkoNativeEscaped(SenkoNativeServerRemark(server))];
}

static SenkoServer *SenkoNativeServerFromParsed(const vl_server_t *parsed,
                                                NSString *raw, int index) {
    SenkoServer *server = [[[SenkoServer alloc] init] autorelease];
    server->index = index;
    server->selected = NO;
    server->group = -1;
    server->proto = [SenkoNativeString(parsed->proto == VL_PROTO_VLESS ? "vless" :
                                        parsed->proto == VL_PROTO_TROJAN ? "trojan" :
                                        parsed->proto == VL_PROTO_SOCKS5 ? "socks5" :
                                        parsed->proto == VL_PROTO_HTTP ? "http" :
                                        parsed->proto == VL_PROTO_HYSTERIA2 ? "hysteria2" :
                                        parsed->proto == VL_PROTO_SHADOWSOCKS ? "shadowsocks" :
                                        "unknown") copy];
    server->net = [SenkoNativeString(parsed->net == VL_NET_WS ? "ws" :
                                     parsed->net == VL_NET_GRPC ? "grpc" :
                                     parsed->net == VL_NET_XHTTP ? "xhttp" : "tcp") copy];
    server->security = [SenkoNativeString(vl_sec_name(parsed->security)) copy];
    server->supported = YES;
    server->host = [SenkoNativeString(parsed->host) copy];
    server->port = (int)parsed->port;
    server->remark = [SenkoNativeServerRemark(parsed) copy];
    server->link = [raw copy];
    return server;
}

static NSDictionary *SenkoNativeDictionaryForServer(SenkoServer *server) {
    NSMutableDictionary *dict = [NSMutableDictionary dictionary];
    [dict setObject:[NSNumber numberWithInt:server->index] forKey:@"index"];
    [dict setObject:[NSNumber numberWithBool:server->selected] forKey:@"selected"];
    [dict setObject:[NSNumber numberWithInt:server->group] forKey:@"group"];
    [dict setObject:server->proto ? server->proto : @"vless" forKey:@"proto"];
    [dict setObject:server->net ? server->net : @"tcp" forKey:@"net"];
    [dict setObject:server->security ? server->security : @"none" forKey:@"security"];
    [dict setObject:[NSNumber numberWithBool:server->supported] forKey:@"supported"];
    [dict setObject:server->host ? server->host : @"" forKey:@"host"];
    [dict setObject:[NSNumber numberWithInt:server->port] forKey:@"port"];
    [dict setObject:server->remark ? server->remark : @"" forKey:@"remark"];
    if (server->link) [dict setObject:server->link forKey:@"link"];
    return dict;
}

static SenkoServer *SenkoNativeServerFromDictionary(NSDictionary *dict) {
    if (![dict isKindOfClass:[NSDictionary class]]) return nil;
    SenkoServer *server = [[[SenkoServer alloc] init] autorelease];
    server->index = [[dict objectForKey:@"index"] intValue];
    server->selected = [[dict objectForKey:@"selected"] boolValue];
    server->group = [[dict objectForKey:@"group"] intValue];
    server->proto = [[dict objectForKey:@"proto"] copy];
    server->net = [[dict objectForKey:@"net"] copy];
    server->security = [[dict objectForKey:@"security"] copy];
    server->supported = [[dict objectForKey:@"supported"] boolValue];
    server->host = [[dict objectForKey:@"host"] copy];
    server->port = [[dict objectForKey:@"port"] intValue];
    server->remark = [[dict objectForKey:@"remark"] copy];
    server->link = [[dict objectForKey:@"link"] copy];
    if (!server->proto || !server->host || !server->link) {
        [server release];
        return nil;
    }
    return server;
}

NSArray *SenkoNativeLoadServers(void) {
    NSArray *stored = [[NSUserDefaults standardUserDefaults]
                       objectForKey:@"SenkoNativeCatalog"];
    NSMutableArray *servers = [NSMutableArray array];
    for (NSDictionary *dict in stored) {
        SenkoServer *server = SenkoNativeServerFromDictionary(dict);
        if (server) [servers addObject:server];
    }
    return servers;
}

void SenkoNativeSaveServers(NSArray *servers) {
    NSMutableArray *stored = [NSMutableArray array];
    for (SenkoServer *server in servers) {
        if (server) [stored addObject:SenkoNativeDictionaryForServer(server)];
    }
    [[NSUserDefaults standardUserDefaults] setObject:stored
                                               forKey:@"SenkoNativeCatalog"];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

void SenkoNativeAttachCachedLinks(NSArray *servers) {
    NSArray *cached = SenkoNativeLoadServers();
    for (SenkoServer *server in servers) {
        for (SenkoServer *old in cached) {
            if (old->index != server->index || !old->link) continue;
            [server->link release];
            server->link = [old->link copy];
            break;
        }
    }
}

void SenkoNativeCacheServerLink(SenkoServer *server, NSString *link) {
    if (!server || ![link length]) return;
    [server->link release];
    server->link = [link copy];
}

int SenkoNativeNextServerIndex(NSArray *servers) {
    int next = 0;
    for (SenkoServer *server in servers)
        if (server->index >= next) next = server->index + 1;
    return next;
}

SenkoServer *SenkoNativeServerFromLink(NSString *link, int index, NSString **error) {
    NSData *data;
    vl_server_t parsed;
    char reason[128] = {0};
    if (error) *error = nil;
    if (![link length]) {
        if (error) *error = @"server link is empty";
        return nil;
    }
    data = [link dataUsingEncoding:NSUTF8StringEncoding];
    if (!data || [data length] >= 8192 ||
        cfg_parse_link([data bytes], &parsed) != CFG_OK ||
        !cfg_validate_server(&parsed, reason, sizeof reason)) {
        if (error) *error = [NSString stringWithFormat:@"invalid server link: %s",
                              reason[0] ? reason : "bad link syntax"];
        return nil;
    }
    return SenkoNativeServerFromParsed(&parsed, link, index);
}

NSArray *SenkoNativeServersFromContent(NSData *data, NSString **error) {
    vl_server_t parsed[128];
    size_t count = 0;
    NSMutableArray *servers = [NSMutableArray array];
    if (error) *error = nil;
    if (![data length] || [data length] > 1024 * 1024) {
        if (error) *error = @"import is empty or too large";
        return nil;
    }
    if (cfg_parse_subscription([data bytes], [data length], parsed,
                                sizeof parsed / sizeof parsed[0], &count) != CFG_OK ||
        count == 0) {
        if (error) *error = @"no supported server links found";
        return nil;
    }
    for (size_t i = 0; i < count; ++i) {
        NSString *scheme = parsed[i].proto == VL_PROTO_VLESS ? @"vless" :
                           parsed[i].proto == VL_PROTO_TROJAN ? @"trojan" :
                           parsed[i].proto == VL_PROTO_HYSTERIA2 ? @"hysteria2" : nil;
        if (!scheme) continue;
        NSString *link = SenkoNativeLinkFromParsed(&parsed[i]);
        if (!link) continue;
        SenkoServer *server = SenkoNativeServerFromParsed(&parsed[i], link, (int)i);
        if (server) [servers addObject:server];
    }
    if (![servers count]) {
        if (error) *error = @"the import contains no native-compatible servers";
        return nil;
    }
    return servers;
}

static void SenkoNativeReplyOnMain(SenkoNativeConfigReply reply, NSString *json,
                                   NSString *endpoint, NSString *error) {
    if (!reply) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        reply(json, endpoint, error);
    });
}

void SenkoNativeConfigurationForLink(NSString *link,
                                     SenkoNativeConfigReply reply) {
    NSString *raw = [link copy];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSData *data = [raw dataUsingEncoding:NSUTF8StringEncoding];
        vl_server_t server;
        char reason[128] = {0};
        char json[131072];
        NSString *error = nil;
        NSString *endpoint = nil;
        if (!data || cfg_parse_link([data bytes], &server) != CFG_OK ||
            !cfg_validate_server(&server, reason, sizeof reason)) {
            error = [NSString stringWithFormat:@"invalid server link: %s",
                     reason[0] ? reason : "bad link syntax"];
        } else {
            endpoint = SenkoNativeEndpoint(server.host, &error);
            if (endpoint && go_config_render(&server, [endpoint UTF8String],
                                             "utun", json, sizeof json) != 0)
                error = @"native core rejected this server configuration";
        }
        NSString *jsonText = error ? nil :
            [[[NSString alloc] initWithBytes:json length:strlen(json)
                                    encoding:NSUTF8StringEncoding] autorelease];
        SenkoNativeReplyOnMain(reply, jsonText, endpoint, error);
        [raw release];
    });
}

/* hysteria2 is quic over udp: a TCP connect() to the same port dials nothing
   and always times out, which is why these servers never got a ping. a real
   handshake needs the go core, but a quic server MUST answer a long-header
   packet naming a version it does not support with a Version Negotiation
   packet (RFC 9000 6.1), as long as the datagram is padded to the 1200 byte
   anti-amplification floor. that gives a real round trip without one */
#define SENKO_QUIC_PROBE_LEN 1200

static void SenkoBuildQuicProbePacket(unsigned char *out) {
    memset(out, 0, SENKO_QUIC_PROBE_LEN);
    out[0] = 0xC0;
/* 0x?a?a?a?a is reserved by RFC 9000 15 for greasing and is guaranteed to
   never name a real quic version, so a compliant server always negotiates */
    out[1] = 0x1a; out[2] = 0x2a; out[3] = 0x3a; out[4] = 0x4a;
    out[5] = 0x08; /* destination connection id length */
    arc4random_buf(out + 6, 8);
    out[14] = 0x00; /* source connection id length */
/* bytes 15.. stay zero padding up to the anti-amplification floor */
}

/* salamander obfuscation (transport/internet/finalmask/salamander): an 8 byte
   random salt, then the payload xored with blake2b256(password || salt)
   repeated over the payload. the reply comes back obfuscated the same way,
   but reachability only needs to see bytes arrive, not decode them */
static int SenkoSalamanderObfuscate(const char *password, const unsigned char *plain,
                                    size_t plain_len, unsigned char *out, size_t out_cap) {
    unsigned char keyed[136];
    unsigned char key[32];
    size_t pass_len = strlen(password);
    size_t i;
    if (out_cap < plain_len + 8 || pass_len + 8 > sizeof keyed) return -1;
    arc4random_buf(out, 8);
    memcpy(keyed, password, pass_len);
    memcpy(keyed + pass_len, out, 8);
    blake2b256(keyed, pass_len + 8, key);
    for (i = 0; i < plain_len; ++i)
        out[8 + i] = plain[i] ^ key[i % sizeof key];
    return 0;
}

static void SenkoNativeProbeHysteria2(const vl_server_t *server, NSString *endpoint,
                                      int *outMs, NSString **outError) {
    unsigned char plain[SENKO_QUIC_PROBE_LEN];
    unsigned char wire[8 + SENKO_QUIC_PROBE_LEN];
    const unsigned char *packet = plain;
    size_t packetLen = sizeof plain;
    struct sockaddr_in address;
    struct timeval timeout = {2, 500000};
    struct timeval started, finished;
    int fd, flags;

    SenkoBuildQuicProbePacket(plain);
    if (server->obfs[0] && strcmp(server->obfs, "salamander") == 0) {
        if (SenkoSalamanderObfuscate(server->obfs_password, plain, sizeof plain,
                                     wire, sizeof wire) != 0) {
            *outError = @"cannot obfuscate quic probe";
            return;
        }
        packet = wire;
        packetLen = sizeof wire;
    }

    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(server->port);
    inet_pton(AF_INET, [endpoint UTF8String], &address.sin_addr);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    flags = fd >= 0 ? fcntl(fd, F_GETFL, 0) : -1;
    if (fd < 0 || flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        *outError = @"cannot create UDP probe socket";
        if (fd >= 0) close(fd);
        return;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        *outError = [NSString stringWithFormat:@"UDP probe failed: %s", strerror(errno)];
        close(fd);
        return;
    }

    gettimeofday(&started, NULL);
    if (send(fd, packet, packetLen, 0) < 0) {
        *outError = [NSString stringWithFormat:@"UDP probe failed: %s", strerror(errno)];
        close(fd);
        return;
    }

    {
        fd_set readSet;
        unsigned char reply[64];
        int ready;
        FD_ZERO(&readSet);
        FD_SET(fd, &readSet);
        ready = select(fd + 1, &readSet, NULL, NULL, &timeout);
        if (ready > 0 && recv(fd, reply, sizeof reply, 0) > 0) {
            gettimeofday(&finished, NULL);
            *outMs = (int)((finished.tv_sec - started.tv_sec) * 1000 +
                           (finished.tv_usec - started.tv_usec) / 1000);
        } else if (ready == 0) {
            *outError = @"UDP probe timed out (quic server sent nothing back)";
        } else {
            *outError = [NSString stringWithFormat:@"UDP probe failed: %s", strerror(errno)];
        }
    }
    close(fd);
}

void SenkoNativeProbeLink(NSString *link, SenkoNativeProbeReply reply) {
    NSString *raw = [link copy];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSData *data = [raw dataUsingEncoding:NSUTF8StringEncoding];
        vl_server_t server;
        int ms = -1;
        NSString *error = nil;
        if (!data || cfg_parse_link([data bytes], &server) != CFG_OK) {
            error = @"invalid server link";
        } else {
            NSString *endpoint = SenkoNativeEndpoint(server.host, &error);
            if (endpoint && server.proto == VL_PROTO_HYSTERIA2) {
                SenkoNativeProbeHysteria2(&server, endpoint, &ms, &error);
            } else if (endpoint) {
                struct sockaddr_in address;
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                struct timeval timeout = {2, 500000};
                struct timeval started, finished;
                int flags;
                int result;
                memset(&address, 0, sizeof address);
                address.sin_family = AF_INET;
                address.sin_port = htons(server.port);
                inet_pton(AF_INET, [endpoint UTF8String], &address.sin_addr);
                flags = fd >= 0 ? fcntl(fd, F_GETFL, 0) : -1;
                if (fd < 0 || flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
                    error = @"cannot create TCP probe socket";
                    if (fd >= 0) close(fd);
                } else {
                    gettimeofday(&started, NULL);
                    result = connect(fd, (struct sockaddr *)&address, sizeof address);
                    if (result != 0 && errno == EINPROGRESS) {
                        fd_set writeSet;
                        int ready;
                        FD_ZERO(&writeSet);
                        FD_SET(fd, &writeSet);
                        ready = select(fd + 1, NULL, &writeSet, NULL, &timeout);
                        if (ready > 0) {
                            int soError = 0;
                            socklen_t soLength = sizeof soError;
                            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &soLength);
                            result = soError;
                        } else {
                            result = ready == 0 ? ETIMEDOUT : errno;
                        }
                    }
                    gettimeofday(&finished, NULL);
                    if (result == 0)
                        ms = (int)((finished.tv_sec - started.tv_sec) * 1000 +
                                   (finished.tv_usec - started.tv_usec) / 1000);
                    else
                        error = [NSString stringWithFormat:@"TCP probe failed: %s",
                                 strerror(result)];
                    close(fd);
                }
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (reply) reply(ms, error);
        });
        [raw release];
    });
}
