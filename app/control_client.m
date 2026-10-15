#import "control_client.h"
#include "../common/senko_paths.h"
#include "../daemon/core/b64.h"
#include "../daemon/core/control.h"

#import <sys/socket.h>
#import <sys/un.h>
#import <fcntl.h>
#import <unistd.h>
#import <string.h>
#import <errno.h>
#import <stdio.h>
#import <sys/stat.h>

@implementation SenkoServer
- (void)dealloc {
    [security release];
    [proto release];
    [net release];
    [host release];
    [remark release];
    [link release];
    [dupIndexes release];
    [super dealloc];
}
@end

@implementation SenkoSub
- (void)dealloc {
    [name release];
    [url release];
    [header release];
    [description release];
    [supportURL release];
    [super dealloc];
}
@end

@implementation SenkoRule
- (void)dealloc {
    [action release];
    [type release];
    [value release];
    [super dealloc];
}
@end

@implementation SenkoRoutingProfile
- (void)dealloc {
    [name release];
    [super dealloc];
}
@end

@implementation SenkoRoutingEntry
- (void)dealloc {
    [action release];
    [entry release];
    [super dealloc];
}
@end

@implementation SenkoGeoState
- (void)dealloc {
    [message release];
    [super dealloc];
}
@end

@implementation SenkoDiagFact
- (void)dealloc {
    [key release];
    [value release];
    [super dealloc];
}
@end

@implementation SenkoCheckStage
- (void)dealloc {
    [name release];
    [super dealloc];
}
@end

@implementation SenkoControl

NSString *SenkoControlStateFromReply(NSString *reply, long *uptime) {
    if (uptime) *uptime = 0;
    for (NSString *line in [reply componentsSeparatedByString:@"\n"]) {
        NSData *data = [line dataUsingEncoding:NSUTF8StringEncoding];
        ctl_state_t state;
        long age;
        if (ctl_parse_state([data bytes], [data length], &state, &age) != CTL_OK)
            continue;
        if (uptime) *uptime = age;
        return [NSString stringWithUTF8String:ctl_state_name(state)];
    }
    return nil;
}

- (id)initWithSocketPath:(NSString *)path {
    if ((self = [super init])) {
        _sockPath = [path copy];
    }
    return self;
}

- (void)dealloc {
    [_sockPath release];
    [super dealloc];
}

static void set_rcv_timeout(int fd, int ms) {
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

/* read until a terminal line */
static int reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        if (llen > 0) {
            const char *ln = buf + start;
            int stream =
                (llen >= 5 && memcmp(ln, "STAT ", 5) == 0) ||
                (llen >= 4 && memcmp(ln, "SET ", 4) == 0) ||
                (llen >= 5 && memcmp(ln, "RULE ", 5) == 0) ||
                (llen >= 5 && memcmp(ln, "DIAG ", 5) == 0) ||
                (llen >= 4 && memcmp(ln, "SRV ", 4) == 0) ||
                (llen >= 4 && memcmp(ln, "SUB ", 4) == 0) ||
                (llen >= 8 && memcmp(ln, "SUBMETA ", 8) == 0) ||
                (llen >= 8 && memcmp(ln, "SUBINFO ", 8) == 0) ||
                (llen >= 7 && memcmp(ln, "SUBHDR ", 7) == 0) ||
                (llen >= 8 && memcmp(ln, "SECTION ", 8) == 0) ||
                (llen >= 6 && memcmp(ln, "FDATA ", 6) == 0);
            if (!stream) return 1; /* stop on terminal records */
        }
        start = i + 1;
    }
    return 0;
}

/* LIST streams many records and ends with LISTEND. the generic predicate above
   stops at the first line it does not recognise, and the daemon can push a
   STATE or PONG event into the middle of the stream, which truncated the reply
   and made a freshly added subscription appear only after a restart */
static int list_reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        const char *ln = buf + start;
        if ((llen >= 8 && memcmp(ln, "LISTEND ", 8) == 0) ||
            (llen >= 4 && memcmp(ln, "ERR ", 4) == 0))
            return 1;
        start = i + 1;
    }
    return 0;
}

/* RULES streams one record per rule and closes with RULEEND */
static int rules_reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        const char *ln = buf + start;
        if ((llen >= 8 && memcmp(ln, "RULEEND ", 8) == 0) ||
            (llen >= 4 && memcmp(ln, "ERR ", 4) == 0))
            return 1;
        start = i + 1;
    }
    return 0;
}

/* ROUTING closes with ROUTINGEND and ROUTINGENTRIES with RENTRYEND */
static int routing_reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        const char *ln = buf + start;
        if ((llen >= 10 && memcmp(ln, "ROUTINGEND", 10) == 0) ||
            (llen >= 9 && memcmp(ln, "RENTRYEND", 9) == 0) ||
            (llen >= 4 && memcmp(ln, "ERR ", 4) == 0))
            return 1;
        start = i + 1;
    }
    return 0;
}

/* DIAG streams one fact per line and closes with DIAGEND */
static int diag_reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        const char *ln = buf + start;
        if ((llen >= 7 && memcmp(ln, "DIAGEND", 7) == 0) ||
            (llen >= 4 && memcmp(ln, "ERR ", 4) == 0))
            return 1;
        start = i + 1;
    }
    return 0;
}

/* FETCH, LOGS and every other blob reply ends with FDEND */
static int blob_reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        const char *ln = buf + start;
        if ((llen >= 6 && memcmp(ln, "FDEND ", 6) == 0) ||
            (llen >= 4 && memcmp(ln, "ERR ", 4) == 0))
            return 1;
        start = i + 1;
    }
    return 0;
}

static int native_config_reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        const char *ln = buf + start;
        if ((llen >= 8 && memcmp(ln, "NCFGEND ", 8) == 0) ||
            (llen >= 4 && memcmp(ln, "ERR ", 4) == 0))
            return 1;
        start = i + 1;
    }
    return 0;
}

/* amneziawg answers with AWG lines closed by AWGEND */
static int awg_reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        const char *ln = buf + start;
        size_t llen = i - start;
        if ((llen >= 7 && memcmp(ln, "AWGEND ", 7) == 0) ||
            (llen >= 4 && memcmp(ln, "ERR ", 4) == 0))
            return 1;
        start = i + 1;
    }
    return 0;
}

/* wait for the final tunnel state */
static int tunnel_reply_complete(const char *buf, size_t len) {
    size_t start = 0;
    int terminal = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != '\n') continue;
        size_t llen = i - start;
        if (llen >= 4 && memcmp(buf + start, "ERR ", 4) == 0)
            return 1;
        if (llen >= 6 && memcmp(buf + start, "STATE ", 6) == 0) {
            const char *st = buf + start + 6;
            size_t slen = llen - 6;
            if (slen >= 10 && memcmp(st, "connecting", 10) == 0) {
            } else if ((slen >= 9 && memcmp(st, "connected", 9) == 0) ||
                       (slen >= 5 && memcmp(st, "error", 5) == 0) ||
                       (slen >= 4 && memcmp(st, "idle", 4) == 0)) {
                terminal = 1;
            }
        }
        start = i + 1;
    }
    return terminal;
}

/* keep the token beside the socket */
static NSString *senkoCtlTokenPath(NSString *sockPath) {
    if (![sockPath length]) return @"/var/tmp/senkod.token";
    if ([sockPath hasSuffix:@".sock"])
        return [[sockPath substringToIndex:[sockPath length] - 5]
                stringByAppendingString:@".token"];
    return [sockPath stringByAppendingString:@".token"];
}

static NSString *senkoLoadCtlToken(NSString *sockPath) {
    NSString *path = senkoCtlTokenPath(sockPath);
    NSError *err = nil;
    NSString *raw = [NSString stringWithContentsOfFile:path
                                              encoding:NSUTF8StringEncoding
                                                 error:&err];
    if (![raw length]) return nil;
    return [raw stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static int write_all_fd(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t left = len;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w <= 0) return -1;
        p += w;
        left -= (size_t)w;
    }
    return 0;
}

/* let mobile clients open the socket. senkod answers AUTH only between two
   commands, so a client that waits less than its own command timeout reports
   a daemon busy connecting as one that did not answer */
static int senkoCtlAuth(int fd, NSString *sockPath, int timeoutMs,
                        char *refusal, size_t refusalCap) {
    NSString *tok = senkoLoadCtlToken(sockPath);
    if (![tok length]) return 0;
    char line[96];
    int n = snprintf(line, sizeof line, "AUTH %s\n", [tok UTF8String]);
    if (n <= 0 || (size_t)n >= sizeof line) return -1;
    if (write_all_fd(fd, line, (size_t)n) != 0) return -1;
    set_rcv_timeout(fd, timeoutMs);
    char buf[128];
    size_t tot = 0;
    while (tot + 1 < sizeof buf) {
        ssize_t r = read(fd, buf + tot, sizeof buf - 1 - tot);
        if (r > 0) {
            tot += (size_t)r;
            buf[tot] = '\0';
            if (memchr(buf, '\n', tot)) break;
            continue;
        }
        break;
    }
    if (tot >= 3 && memcmp(buf, "OK ", 3) == 0) return 0;
    if (tot >= 2 && memcmp(buf, "OK", 2) == 0) return 0;
/* a refusal names its reason ("too many control connections"), which the
   caller shows instead of a daemon that did not answer */
    if (tot >= 4 && memcmp(buf, "ERR ", 4) == 0 && refusal && refusalCap)
        snprintf(refusal, refusalCap, "%s", buf);
    return -1;
}

- (NSString *)blockingSend:(NSString *)cmd timeoutMs:(int)timeoutMs {
    const char *path = [_sockPath fileSystemRepresentation];
    if (!path) return nil;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return nil;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof addr.sun_path - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return nil;
    }

    char refusal[128] = "";
    if (senkoCtlAuth(fd, _sockPath, timeoutMs > 0 ? timeoutMs : 2000,
                     refusal, sizeof refusal) != 0) {
        close(fd);
        return refusal[0] ? [NSString stringWithUTF8String:refusal] : nil;
    }

    NSString *line = [cmd hasSuffix:@"\n"] ? cmd : [cmd stringByAppendingString:@"\n"];
    NSData *out = [line dataUsingEncoding:NSUTF8StringEncoding];
    const char *p = [out bytes];
    size_t left = [out length];
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w <= 0) { close(fd); return nil; }
        p += w; left -= (size_t)w;
    }

/* use the timeout for a dead daemon */
    int is_tunnel = ([cmd hasPrefix:@"CONNECT "] || [cmd isEqualToString:@"DISCONNECT"] ||
                     [cmd isEqualToString:@"DISCONNECT\n"]);
    int is_list = [cmd hasPrefix:@"LIST"];
    int is_rules = [cmd hasPrefix:@"RULES"];
    int is_diag = [cmd hasPrefix:@"DIAG"];
    int is_blob = [cmd hasPrefix:@"LOGS"] || [cmd hasPrefix:@"FETCH "];
    int (*done_fn)(const char *, size_t) = reply_complete;
    if (is_tunnel) done_fn = tunnel_reply_complete;
    else if (is_list) done_fn = list_reply_complete;
    else if (is_rules) done_fn = rules_reply_complete;
    else if (is_diag) done_fn = diag_reply_complete;
    else if ([cmd isEqualToString:@"ROUTING"] || [cmd hasPrefix:@"ROUTINGENTRIES "])
        done_fn = routing_reply_complete;
    else if ([cmd hasPrefix:@"NATIVE_CONFIG "])
        done_fn = native_config_reply_complete;
    else if (is_blob) done_fn = blob_reply_complete;
    else if ([cmd hasPrefix:@"AWG "]) done_fn = awg_reply_complete;

    NSMutableData *acc = [NSMutableData data];
    char buf[4096];
    set_rcv_timeout(fd, timeoutMs > 0 ? timeoutMs : 2000);
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r > 0) {
            [acc appendBytes:buf length:(NSUInteger)r];
            if (done_fn([acc bytes], [acc length])) break;
            continue;
        }
        if (r == 0) break;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        break;
    }
    close(fd);

    if ([acc length] == 0) return nil;
    NSString *s = [[[NSString alloc] initWithData:acc encoding:NSUTF8StringEncoding] autorelease];
    return s;
}

- (NSString *)blockingSend:(NSString *)cmd {
    return [self blockingSend:cmd timeoutMs:2000];
}

- (void)sendCommand:(NSString *)cmd reply:(void (^)(NSString *))done {
    [self sendCommand:cmd timeoutMs:2000 reply:done];
}

- (void)deviceHWID:(void (^)(NSString *))done {
/* two seconds is a window a daemon under a live tunnel routinely misses */
    [self sendCommand:@"HWID" timeoutMs:5000 reply:^(NSString *reply) {
        NSString *value = nil;
        if ([reply hasPrefix:@"OK "]) {
            value = [[reply substringFromIndex:3] stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (![value length]) value = nil;
        }
        if (done) done(value);
    }];
}

/* FDATA lines carry base64 chunks and FDEND closes the blob */
static NSString *senkoDecodeBlobReply(NSString *reply) {
    if (![reply length]) return nil;
    NSMutableData *raw = [NSMutableData data];
    BOOL sawEnd = NO;
    for (NSString *ln in [reply componentsSeparatedByString:@"\n"]) {
        if ([ln hasPrefix:@"FDEND "]) { sawEnd = YES; break; }
        if (![ln hasPrefix:@"FDATA "]) continue;
        const char *encoded = [[ln substringFromIndex:6] UTF8String];
        if (!encoded) return nil;
        size_t encoded_len = strlen(encoded);
        size_t cap = b64_decoded_maxlen(encoded_len);
        if (cap == 0) continue;
        NSMutableData *chunk = [NSMutableData dataWithLength:cap];
        size_t got = 0;
        if (b64_decode(encoded, encoded_len, [chunk mutableBytes], cap, &got) != 0)
            return nil;
        [chunk setLength:got];
        [raw appendData:chunk];
    }
    if (!sawEnd) return nil;
    NSString *text = [[[NSString alloc] initWithData:raw
                                            encoding:NSUTF8StringEncoding] autorelease];
    if (!text)
        text = [[[NSString alloc] initWithData:raw
                                      encoding:NSISOLatin1StringEncoding] autorelease];
    return text;
}

/* the dump is the same SET lines the verb accepts, so the screen can hand one
   straight back instead of owning a second encoding. SETEND closes it */
- (void)daemonSettings:(void (^)(NSDictionary *))done {
    [self sendCommand:@"SETTINGS" timeoutMs:3000 reply:^(NSString *reply) {
        if (![reply length]) {
            if (done) done(nil);
            return;
        }
        NSMutableDictionary *values = [NSMutableDictionary dictionary];
        BOOL sawEnd = NO;
        for (NSString *ln in [reply componentsSeparatedByString:@"\n"]) {
            if ([ln hasPrefix:@"SETEND"]) { sawEnd = YES; break; }
            if (![ln hasPrefix:@"SET "]) continue;
            NSString *pair = [ln substringFromIndex:4];
            NSRange sp = [pair rangeOfString:@" "];
            if (sp.location == NSNotFound) continue;
            NSString *key = [pair substringToIndex:sp.location];
            NSString *value = [[pair substringFromIndex:sp.location + 1]
                stringByTrimmingCharactersInSet:
                    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([key length]) [values setObject:value forKey:key];
        }
        if (done) done(sawEnd ? values : nil);
    }];
}

- (void)setSetting:(NSString *)key value:(NSString *)value
             reply:(void (^)(NSString *))done {
    if (![key length] || ![value length]) {
        if (done) done(@"ERR bad setting");
        return;
    }
    [self sendCommand:[NSString stringWithFormat:@"SET %@ %@", key, value]
            timeoutMs:3000
                reply:done];
}

/* RULE <index> <action> <type> <hits> <value>. the value is the last field and
   never carries a space, because the daemon refuses a rule that has one */
static SenkoRule *parseRULE(NSString *line) {
    NSArray *t = [line componentsSeparatedByString:@" "];
    if ([t count] < 6) return nil;
    SenkoRule *r = [[[SenkoRule alloc] init] autorelease];
    r->index = [[t objectAtIndex:1] intValue];
    r->action = [[t objectAtIndex:2] copy];
    r->type = [[t objectAtIndex:3] copy];
    r->hits = (unsigned long long)[[t objectAtIndex:4] longLongValue];
    r->value = [[t objectAtIndex:5] copy];
    return r;
}

- (void)listRules:(void (^)(NSArray *))done {
    [self sendCommand:@"RULES" timeoutMs:3000 reply:^(NSString *reply) {
        if (![reply length]) {
            if (done) done(nil);
            return;
        }
        NSMutableArray *rules = [NSMutableArray array];
        BOOL sawEnd = NO;
        for (NSString *ln in [reply componentsSeparatedByString:@"\n"]) {
            if ([ln hasPrefix:@"RULEEND "]) { sawEnd = YES; break; }
            if (![ln hasPrefix:@"RULE "]) continue;
            SenkoRule *r = parseRULE(ln);
            if (r) [rules addObject:r];
        }
        if (done) done(sawEnd ? rules : nil);
    }];
}

- (void)addRuleAction:(NSString *)action type:(NSString *)type
                value:(NSString *)value reply:(void (^)(NSString *))done {
    if (![action length] || ![type length] || ![value length]) {
        if (done) done(@"ERR bad rule");
        return;
    }
    [self sendCommand:[NSString stringWithFormat:@"SET rule %@ %@ %@",
                                                 action, type, value]
            timeoutMs:3000
                reply:done];
}

- (void)deleteRuleIndex:(int)index reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"DELRULE %d", index]
            timeoutMs:3000
                reply:done];
}

/* ROUTING <sub> <active> <global> <rules> <unsupported> <geo tags> <updated> <name>.
   the name is last because it may hold spaces */
static SenkoRoutingProfile *parseROUTING(NSString *line) {
    NSArray *t = [line componentsSeparatedByString:@" "];
    if ([t count] < 9) return nil;
    SenkoRoutingProfile *p = [[[SenkoRoutingProfile alloc] init] autorelease];
    p->index = [[t objectAtIndex:1] intValue];
    p->active = [[t objectAtIndex:2] intValue] != 0;
    p->globalProxy = [[t objectAtIndex:3] intValue] != 0;
    p->rules = [[t objectAtIndex:4] intValue];
    p->unsupported = [[t objectAtIndex:5] intValue];
    p->geoTags = [[t objectAtIndex:6] intValue];
    p->updated = (unsigned long long)[[t objectAtIndex:7] longLongValue];
    p->name = [[[t subarrayWithRange:NSMakeRange(8, [t count] - 8)]
                componentsJoinedByString:@" "] copy];
    return p;
}

- (void)listRoutingProfiles:(void (^)(NSArray *, int))done {
    [self sendCommand:@"ROUTING" timeoutMs:3000 reply:^(NSString *reply) {
        if (![reply length]) {
            if (done) done(nil, -1);
            return;
        }
        NSMutableArray *profiles = [NSMutableArray array];
        BOOL sawEnd = NO;
        int active = -1;
        for (NSString *ln in [reply componentsSeparatedByString:@"\n"]) {
            if ([ln hasPrefix:@"ROUTINGEND "]) {
                sawEnd = YES;
                active = [[ln substringFromIndex:11] intValue];
                break;
            }
            if (![ln hasPrefix:@"ROUTING "]) continue;
            SenkoRoutingProfile *p = parseROUTING(ln);
            if (p) [profiles addObject:p];
        }
        if (done) done(sawEnd ? profiles : nil, active);
    }];
}

- (void)setRoutingProfileIndex:(int)index reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"ROUTINGSET %@",
                       index < 0 ? @"off" : [NSString stringWithFormat:@"%d", index]]
            timeoutMs:3000
                reply:done];
}

/* RENTRY <action> <site|ip> <supported> <entry>. the entry is last, spaces and all */
- (void)listRoutingEntriesIndex:(int)index done:(void (^)(NSArray *, int))done {
    [self sendCommand:[NSString stringWithFormat:@"ROUTINGENTRIES %d", index]
            timeoutMs:3000
                reply:^(NSString *reply) {
        if (![reply length]) {
            if (done) done(nil, 0);
            return;
        }
        NSMutableArray *entries = [NSMutableArray array];
        BOOL sawEnd = NO;
        int total = 0;
        for (NSString *ln in [reply componentsSeparatedByString:@"\n"]) {
            if ([ln hasPrefix:@"RENTRYEND "]) {
                sawEnd = YES;
                total = [[ln substringFromIndex:10] intValue];
                break;
            }
            if (![ln hasPrefix:@"RENTRY "]) continue;
            NSArray *t = [ln componentsSeparatedByString:@" "];
            if ([t count] < 5) continue;
            SenkoRoutingEntry *e = [[[SenkoRoutingEntry alloc] init] autorelease];
            e->action = [[t objectAtIndex:1] copy];
            e->isIP = [[t objectAtIndex:2] isEqualToString:@"ip"];
            e->supported = [[t objectAtIndex:3] intValue] != 0;
            e->entry = [[[t subarrayWithRange:NSMakeRange(4, [t count] - 4)]
                         componentsJoinedByString:@" "] copy];
            [entries addObject:e];
        }
        if (done) done(sawEnd ? entries : nil, total);
    }];
}

/* GEO <busy> <stamp> <missing> <result> <message> */
- (void)geoState:(void (^)(SenkoGeoState *))done {
    [self sendCommand:@"GEO" timeoutMs:3000 reply:^(NSString *reply) {
        NSString *ln = [[reply componentsSeparatedByString:@"\n"] objectAtIndex:0];
        NSArray *t = [ln componentsSeparatedByString:@" "];
        if (![ln hasPrefix:@"GEO "] || [t count] < 5) {
            if (done) done(nil);
            return;
        }
        SenkoGeoState *g = [[[SenkoGeoState alloc] init] autorelease];
        g->busy = [[t objectAtIndex:1] intValue] != 0;
        g->stamp = (unsigned long long)[[t objectAtIndex:2] longLongValue];
        g->missing = [[t objectAtIndex:3] intValue];
        g->result = [[t objectAtIndex:4] intValue];
        g->message = [[[t subarrayWithRange:NSMakeRange(5, [t count] - 5)]
                       componentsJoinedByString:@" "] copy];
        if (done) done(g);
    }];
}

- (void)updateGeo:(void (^)(NSString *))done {
    [self sendCommand:@"GEOUPDATE" timeoutMs:3000 reply:done];
}

- (void)removeRival:(void (^)(NSString *))done {
    [self sendCommand:@"VLESSCORE REMOVE" timeoutMs:5000 reply:done];
}

- (void)daemonDiagnostics:(void (^)(NSArray *))done {
    [self sendCommand:@"DIAG" timeoutMs:5000 reply:^(NSString *reply) {
        if (![reply length]) {
            if (done) done(nil);
            return;
        }
        NSMutableArray *facts = [NSMutableArray array];
        BOOL sawEnd = NO;
        for (NSString *ln in [reply componentsSeparatedByString:@"\n"]) {
            if ([ln hasPrefix:@"DIAGEND"]) { sawEnd = YES; break; }
            if (![ln hasPrefix:@"DIAG "]) continue;
            NSString *rest = [ln substringFromIndex:5];
            NSRange sp = [rest rangeOfString:@" "];
            if (sp.location == NSNotFound) continue;
            SenkoDiagFact *fact = [[[SenkoDiagFact alloc] init] autorelease];
            fact->key = [[rest substringToIndex:sp.location] copy];
            fact->value = [[[rest substringFromIndex:sp.location + 1]
                stringByTrimmingCharactersInSet:
                    [NSCharacterSet whitespaceAndNewlineCharacterSet]] copy];
            [facts addObject:fact];
        }
        if (done) done(sawEnd ? facts : nil);
    }];
}

/* the stage lines arrive before the verdict, so one pass over the reply yields
   both. an older daemon sends none and the caller still gets its result */
- (void)checkIndex:(int)idx mode:(NSString *)mode
            stages:(void (^)(NSArray *, int, NSString *))done {
    NSArray *safeMode = [NSArray arrayWithObjects:@"tcp", @"real", @"proxy", @"tunnel", @"handshake", nil];
    if (![safeMode containsObject:mode]) {
        if (done) done(nil, -1, @"unknown check type");
        return;
    }
    [self sendCommand:[NSString stringWithFormat:@"CHECK %@ %d stages", mode, idx]
            timeoutMs:12000 reply:^(NSString *reply) {
        NSMutableArray *stages = [NSMutableArray array];
        int ms = -1;
        NSString *error = nil;
        for (NSString *raw in [reply componentsSeparatedByString:@"\n"]) {
            NSString *ln = [raw stringByTrimmingCharactersInSet:
                            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([ln hasPrefix:@"STAGE "]) {
                NSString *rest = [ln substringFromIndex:6];
                NSRange first = [rest rangeOfString:@" "];
                if (first.location == NSNotFound) continue;
                NSString *okText = [rest substringToIndex:first.location];
                NSString *after = [rest substringFromIndex:first.location + 1];
                NSRange second = [after rangeOfString:@" "];
                if (second.location == NSNotFound) continue;
                SenkoCheckStage *stage = [[[SenkoCheckStage alloc] init] autorelease];
                stage->ok = [okText intValue] != 0;
                stage->ms = [[after substringToIndex:second.location] intValue];
                stage->name = [[after substringFromIndex:second.location + 1] copy];
                [stages addObject:stage];
                continue;
            }
            if ([ln hasPrefix:@"PONG "]) {
                NSArray *parts = [ln componentsSeparatedByString:@" "];
                if ([parts count] >= 3) ms = [[parts objectAtIndex:2] intValue];
                continue;
            }
            if ([ln hasPrefix:@"ERR "]) error = [ln substringFromIndex:4];
        }
        if (ms < 0 && ![error length]) error = @"the daemon did not answer";
        if (done) done(stages, ms, ms >= 0 ? nil : error);
    }];
}

- (void)tunnelRoutes:(void (^)(NSString *, NSString *))done {
    [self sendCommand:@"FWCONF" timeoutMs:6000 reply:^(NSString *reply) {
        NSMutableString *text = [NSMutableString string];
        BOOL sawEnd = NO;
        NSString *error = nil;
        for (NSString *raw in [reply componentsSeparatedByString:@"\n"]) {
            NSString *ln = [raw stringByTrimmingCharactersInSet:
                            [NSCharacterSet newlineCharacterSet]];
            if ([ln hasPrefix:@"FWEND"]) { sawEnd = YES; break; }
            if ([ln hasPrefix:@"FWLINE "]) {
                [text appendString:[ln substringFromIndex:7]];
                [text appendString:@"\n"];
                continue;
            }
            if ([ln hasPrefix:@"ERR "]) error = [ln substringFromIndex:4];
        }
        if (done) done(sawEnd ? text : nil,
                       sawEnd ? nil : (error ?: @"the daemon did not answer"));
    }];
}

- (void)flushTarget:(NSString *)what reply:(void (^)(NSString *))done {
    NSArray *known = [NSArray arrayWithObjects:@"dns", @"bypass", @"rules", @"config", nil];
    if (![known containsObject:what]) { if (done) done(@"ERR unknown flush target"); return; }
    [self sendCommand:[NSString stringWithFormat:@"FLUSH %@", what]
            timeoutMs:6000 reply:done];
}

- (void)resetDeviceHWID:(void (^)(NSString *, NSString *))done {
    [self sendCommand:@"HWIDRESET" timeoutMs:6000 reply:^(NSString *reply) {
        if ([reply hasPrefix:@"OK "]) {
            NSString *value = [[reply substringFromIndex:3] stringByTrimmingCharactersInSet:
                               [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([value length]) { if (done) done(value, nil); return; }
        }
        NSString *error = [reply hasPrefix:@"ERR "] ? [reply substringFromIndex:4] : reply;
        if (done) done(nil, [error length] ? error
                                           : @"the daemon did not answer");
    }];
}

- (void)daemonLogTail:(void (^)(NSString *))done {
    [self sendCommand:@"LOGS" timeoutMs:6000 reply:^(NSString *reply) {
        if (done) done(senkoDecodeBlobReply(reply));
    }];
}

- (void)importContent:(NSData *)data reply:(void (^)(NSString *))done {
    if (![data length]) {
        if (done) done(@"ERR nothing to import");
        return;
    }
    NSString *stage = @SENKO_IMPORT_STAGE;
    NSString *dir = [stage stringByDeletingLastPathComponent];
    NSError *err = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:&err] ||
        ![data writeToFile:stage atomically:YES]) {
        if (done) done(@"ERR could not stage the import file");
        return;
    }
    [self sendCommand:@"IMPORT" timeoutMs:10000 reply:^(NSString *reply) {
/* the daemon removes the file once it has read it; clean up when it never did */
        [[NSFileManager defaultManager] removeItemAtPath:stage error:nil];
        if (done) done(reply);
    }];
}

- (void)clearManualServers:(void (^)(NSString *))done {
    [self sendCommand:@"CLEARMANUAL" timeoutMs:5000 reply:done];
}

- (void)exportBackup:(void (^)(NSString *))done {
    [self sendCommand:@"EXPORT" timeoutMs:5000 reply:done];
}

- (void)restoreBackup:(void (^)(NSString *))done {
    [self sendCommand:@"RESTORE" timeoutMs:5000 reply:done];
}

- (void)sendCommand:(NSString *)cmd timeoutMs:(int)timeoutMs reply:(void (^)(NSString *))done {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        NSString *reply = [self blockingSend:cmd timeoutMs:timeoutMs];
        dispatch_async(dispatch_get_main_queue(), ^{
            if ([cmd isEqualToString:@"STATUS"]) _trafficKnown = NO;
            NSMutableString *clean = reply ? [NSMutableString string] : nil;
            for (NSString *line in [reply componentsSeparatedByString:@"\n"]) {
                if ([line hasPrefix:@"STAT "]) {
                    uint64_t up, down;
                    NSData *data = [line dataUsingEncoding:NSUTF8StringEncoding];
                    if (ctl_parse_stat([data bytes], [data length], &up, &down) == CTL_OK) {
                        _trafficUp = up;
                        _trafficDown = down;
                        _trafficKnown = YES;
                    } else {
                        _trafficKnown = NO;
                        NSLog(@"senko: invalid traffic counters");
                    }
                } else if ([line length]) {
                    [clean appendFormat:@"%@\n", line];
                }
            }
            if (done) done(clean);
        });
        [pool drain];
    });
}

- (void)probeDaemon:(void (^)(BOOL))done {
    [self sendCommand:@"STATUS" timeoutMs:1500 reply:^(NSString *reply) {
        BOOL up = SenkoControlStateFromReply(reply, NULL) != nil;
        if (done) done(up);
    }];
}

/* the last line senkod or launchd wrote. the log is root:mobile 0640, so the
   app can still read why the daemon is down when the socket cannot say */
static NSString *SenkoSystemLogLastLine(void) {
    NSFileHandle *fh = [NSFileHandle fileHandleForReadingAtPath:@SENKO_SYSTEM_LOG];
    if (!fh) return nil;
    unsigned long long size = [fh seekToEndOfFile];
    unsigned long long want = size > 4096 ? 4096 : size;
    [fh seekToFileOffset:size - want];
    NSData *slice = [fh readDataOfLength:(NSUInteger)want];
    [fh closeFile];
    NSString *text = [[[NSString alloc] initWithData:slice
                                            encoding:NSUTF8StringEncoding] autorelease];
    NSArray *lines = [text componentsSeparatedByString:@"\n"];
    for (NSInteger i = (NSInteger)[lines count] - 1; i >= 0; --i) {
        NSString *line = [[lines objectAtIndex:i] stringByTrimmingCharactersInSet:
                          [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([line length]) return line;
    }
    return nil;
}

/* launchd owns senkod (KeepAlive, 3 s throttle) and the app has no root to
   start it, so all that is left to do is to say what is missing */
static NSString *SenkoDaemonDownText(void) {
    static const char *bins[] = { "/usr/bin/senkod", SENKO_USR_BIN "/senkod",
                                  "/var/jb/usr/bin/senkod", NULL };
    static const char *plists[] = { "/Library/LaunchDaemons/com.senko.senkod.plist",
                                    "/var/jb/Library/LaunchDaemons/com.senko.senkod.plist",
                                    NULL };
    BOOL haveBin = NO, havePlist = NO;
    for (int i = 0; bins[i] && !haveBin; ++i) haveBin = access(bins[i], F_OK) == 0;
    for (int i = 0; plists[i] && !havePlist; ++i) havePlist = access(plists[i], F_OK) == 0;
    if (!haveBin) return @"senkod is missing: reinstall the package";
    if (!havePlist) return @"the senkod launch daemon is missing: reinstall the package";
    NSString *last = SenkoSystemLogLastLine();
    if ([last length])
        return [NSString stringWithFormat:@"senkod is not running, last log line: %@", last];
    return @"senkod is not running and its log is empty";
}

- (void)ensureDaemon:(void (^)(BOOL, NSString *))done {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
/* launchd may be inside its restart throttle, or senkod still loading its
   config on an old device */
        BOOL up = NO;
        for (int i = 0; i < 24 && !up; ++i) {
            NSString *r = [self blockingSend:@"STATUS" timeoutMs:800];
            up = r && ([r hasPrefix:@"STATE "] ||
                       [r rangeOfString:@"\nSTATE "].location != NSNotFound);
            if (!up) usleep(250000);
        }
        NSString *detail = up ? nil : [SenkoDaemonDownText() retain];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (done) done(up, detail);
            [detail release];
        });
        [pool drain];
    });
}

static BOOL tokenIsProto(NSString *s) {
    return [s isEqualToString:@"vless"] || [s isEqualToString:@"socks5"] ||
           [s isEqualToString:@"http"] || [s isEqualToString:@"https"] ||
           [s isEqualToString:@"trojan"] || [s isEqualToString:@"shadowsocks"] ||
           [s isEqualToString:@"ss"] || [s isEqualToString:@"hysteria2"] ||
           [s isEqualToString:@"vmess"];
}

static BOOL tokenIsNet(NSString *s) {
    return [s isEqualToString:@"tcp"] || [s isEqualToString:@"ws"] ||
           [s isEqualToString:@"grpc"] || [s isEqualToString:@"http"] ||
           [s isEqualToString:@"xhttp"] || [s isEqualToString:@"quic"];
}

static BOOL tokenIsSecurity(NSString *s) {
    return [s isEqualToString:@"none"] || [s isEqualToString:@"tls"] ||
           [s isEqualToString:@"reality"] || [s isEqualToString:@"unknown"] ||
           [s hasPrefix:@"aes-"] || [s hasPrefix:@"chacha20"] || [s isEqualToString:@"aead"];
}

/* parse old server rows */
static SenkoServer *parseSRV(NSString *line) {
    NSArray *t = [line componentsSeparatedByString:@" "];
    if ([t count] < 7) return nil;
    if (![[t objectAtIndex:0] isEqualToString:@"SRV"]) return nil;
    SenkoServer *sv = [[[SenkoServer alloc] init] autorelease];
    sv->index    = [[t objectAtIndex:1] intValue];
    sv->selected = [[t objectAtIndex:2] intValue] != 0;
    sv->group    = [[t objectAtIndex:3] intValue];
    NSUInteger remarkStart = 7;
    BOOL unsupported = [[t objectAtIndex:7] isEqualToString:@"0"];
    /* a placeholder row names its foreign protocol ("tuic") in the proto slot */
    BOOL newLayout = [t count] >= 10 &&
        (tokenIsProto([t objectAtIndex:4]) || unsupported) &&
        tokenIsNet([t objectAtIndex:5]) &&
        tokenIsSecurity([t objectAtIndex:6]) &&
        ([[t objectAtIndex:7] isEqualToString:@"0"] ||
         [[t objectAtIndex:7] isEqualToString:@"1"]) &&
        [[t objectAtIndex:9] intValue] > 0;
    if (newLayout) {
        sv->proto    = [[t objectAtIndex:4] copy];
        sv->net      = [[t objectAtIndex:5] copy];
        sv->security = [[t objectAtIndex:6] copy];
        sv->supported = [[t objectAtIndex:7] intValue] != 0;
        sv->host     = [[t objectAtIndex:8] copy];
        sv->port     = [[t objectAtIndex:9] intValue];
        remarkStart = 10;
    } else {
        sv->proto    = [@"vless" copy];
        sv->net      = [@"tcp" copy];
        sv->security = [[t objectAtIndex:4] copy];
        sv->supported = YES;
        sv->host     = [[t objectAtIndex:5] copy];
        sv->port     = [[t objectAtIndex:6] intValue];
    }
    if ([t count] > remarkStart) {
        NSRange rest = NSMakeRange(remarkStart, [t count] - remarkStart);
        sv->remark = [[[t subarrayWithRange:rest] componentsJoinedByString:@" "] copy];
    } else {
        sv->remark = [@"" copy];
    }
    return sv;
}

static SenkoSub *parseSUB(NSString *line) {
    NSArray *t = [line componentsSeparatedByString:@" "];
    if ([t count] < 3) return nil;
    if (![[t objectAtIndex:0] isEqualToString:@"SUB"]) return nil;
    SenkoSub *s = [[[SenkoSub alloc] init] autorelease];
    s->index = [[t objectAtIndex:1] intValue];
    s->name = [[t objectAtIndex:2] copy];
    if ([t count] > 3) {
        NSRange rest = NSMakeRange(3, [t count] - 3);
        s->url = [[[t subarrayWithRange:rest] componentsJoinedByString:@" "] copy];
    } else {
        s->url = [@"" copy];
    }
    return s;
}

- (void)listCatalog:(void (^)(NSArray *, NSArray *, NSArray *))done {
    [self sendCommand:@"LIST" timeoutMs:5000 reply:^(NSString *reply) {
        if (!reply) { if (done) done(nil, nil, nil); return; }
        NSMutableArray *srvs = [NSMutableArray array];
        NSMutableArray *subs = [NSMutableArray array];
        NSMutableArray *order = [NSMutableArray array];
        NSInteger expectedServers = -1;
        NSArray *lines = [reply componentsSeparatedByString:@"\n"];
        for (NSString *ln in lines) {
            if ([ln length] == 0) continue;
            if ([ln hasPrefix:@"LISTEND "]) {
                expectedServers = [[ln substringFromIndex:8] intValue];
                continue;
            }
            if ([ln hasPrefix:@"SUB "]) {
                SenkoSub *s = parseSUB(ln);
                if (s) [subs addObject:s];
                continue;
            }
            if ([ln hasPrefix:@"SUBMETA "]) {
                NSArray *t = [ln componentsSeparatedByString:@" "];
                if ([t count] >= 3) {
                    int idx = [[t objectAtIndex:1] intValue];
                    for (SenkoSub *s in subs) {
                        if (s->index != idx) continue;
                        s->expire = (unsigned long long)[[t objectAtIndex:2] longLongValue];
                        break;
                    }
                }
                continue;
            }
            if ([ln hasPrefix:@"SUBINFO "]) {
                NSArray *t = [ln componentsSeparatedByString:@" "];
                if ([t count] >= 7) {
                    int idx = [[t objectAtIndex:1] intValue];
                    for (SenkoSub *s in subs) {
                        if (s->index != idx) continue;
                        s->upload = (unsigned long long)[[t objectAtIndex:2] longLongValue];
                        s->download = (unsigned long long)[[t objectAtIndex:3] longLongValue];
                        s->total = (unsigned long long)[[t objectAtIndex:4] longLongValue];
                        NSString *description = [[t objectAtIndex:5]
                            stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
                        NSString *support = [[t objectAtIndex:6]
                            stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
                        s->description = [(description && ![description isEqualToString:@"-"]) ? description : @"" copy];
                        s->supportURL = [(support && ![support isEqualToString:@"-"]) ? support : @"" copy];
                        break;
                    }
                }
                continue;
            }
            if ([ln hasPrefix:@"SUBHDR "]) {
                NSArray *t = [ln componentsSeparatedByString:@" "];
                if ([t count] >= 3) {
                    int idx = [[t objectAtIndex:1] intValue];
                    NSString *encoded = [[t subarrayWithRange:NSMakeRange(2, [t count] - 2)]
                                           componentsJoinedByString:@" "];
                    NSString *header = [encoded isEqualToString:@"-"]
                        ? @""
                        : [encoded stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
                    for (SenkoSub *s in subs) {
                        if (s->index != idx) continue;
                        s->header = [(header ? header : @"") copy];
                        break;
                    }
                }
                continue;
            }
            if ([ln hasPrefix:@"SECTION "]) {
                NSArray *t = [ln componentsSeparatedByString:@" "];
                for (NSUInteger i = 1; i < [t count]; ++i)
                    [order addObject:[NSNumber numberWithInt:[[t objectAtIndex:i] intValue]]];
                continue;
            }
            if (![ln hasPrefix:@"SRV "]) continue; /* skip interleaved events */
            SenkoServer *sv = parseSRV(ln);
            if (sv) [srvs addObject:sv];
        }
        if (expectedServers < 0 || expectedServers != (NSInteger)[srvs count]) {
            if (done) done(nil, nil, nil);
            return;
        }
        if (done) done(srvs, subs, order);
    }];
}

- (void)listServers:(void (^)(NSArray *))done {
    [self listCatalog:^(NSArray *servers, NSArray *subs, NSArray *order) {
        (void)subs;
        (void)order;
        if (done) done(servers);
    }];
}

- (void)serverLinkIndex:(int)idx reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"GETSRV %d", idx]
            timeoutMs:5000
                reply:^(NSString *reply) {
        NSString *link = nil;
        for (NSString *part in [reply componentsSeparatedByString:@"\n"]) {
            if (![part hasPrefix:@"LINK "]) continue;
            NSRange first = [part rangeOfString:@" "];
            NSRange second = first.location == NSNotFound
                ? NSMakeRange(NSNotFound, 0)
                : [part rangeOfString:@" " options:0
                                range:NSMakeRange(first.location + 1,
                                                   [part length] - first.location - 1)];
            if (second.location != NSNotFound)
                link = [part substringFromIndex:second.location + 1];
        }
        if (done) done(link);
    }];
}

- (void)nativeConfigurationIndex:(int)idx
                            reply:(void (^)(NSString *, NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"NATIVE_CONFIG %d", idx]
            timeoutMs:10000
                reply:^(NSString *reply) {
        if (!reply) {
            if (done) done(nil, @"daemon did not return a native VPN configuration");
            return;
        }
        NSMutableData *jsonData = [NSMutableData data];
        NSUInteger expected = NSNotFound;
        for (NSString *line in [reply componentsSeparatedByString:@"\n"]) {
            if ([line hasPrefix:@"ERR "]) {
                if (done) done(nil, [line substringFromIndex:4]);
                return;
            }
            if ([line hasPrefix:@"NCFG "]) {
                NSString *encoded = [line substringFromIndex:5];
                NSData *ascii = [encoded dataUsingEncoding:NSUTF8StringEncoding];
                size_t cap = ascii ? b64_decoded_maxlen([ascii length]) : 0;
                NSMutableData *chunk = cap ? [NSMutableData dataWithLength:cap] : nil;
                size_t got = 0;
                if (!chunk || b64_decode([ascii bytes], [ascii length],
                                          [chunk mutableBytes], cap, &got) != 0) {
                    if (done) done(nil, @"native VPN configuration is corrupt");
                    return;
                }
                [chunk setLength:got];
                [jsonData appendData:chunk];
            } else if ([line hasPrefix:@"NCFGEND "]) {
                expected = (NSUInteger)[[line substringFromIndex:8] longLongValue];
            }
        }
        if (expected == NSNotFound || expected != [jsonData length]) {
            if (done) done(nil, @"native VPN configuration is truncated");
            return;
        }
        NSString *json = [[[NSString alloc] initWithData:jsonData
                                                  encoding:NSUTF8StringEncoding]
                           autorelease];
        if (!json || ![json length]) {
            if (done) done(nil, @"native VPN configuration is not UTF-8");
            return;
        }
        if (done) done(json, nil);
    }];
}

- (void)statusStateWithUptime:(void (^)(NSString *, long))done {
    [self sendCommand:@"STATUS" reply:^(NSString *reply) {
        long uptime = 0;
        NSString *state = SenkoControlStateFromReply(reply, &uptime);
        if (done) done(state, uptime);
    }];
}

- (void)traffic:(void (^)(BOOL, uint64_t, uint64_t))done {
    [self sendCommand:@"STATUS" reply:^(NSString *reply) {
        (void)reply;
        /* the daemon counts the running backend, vless or amneziawg, and the
           caller already knows the tunnel is up */
        if (done) done(_trafficKnown, _trafficUp, _trafficDown);
    }];
}

- (void)statusState:(void (^)(NSString *))done {
    [self statusStateWithUptime:^(NSString *state, long uptime) {
        (void)uptime;
        if (done) done(state);
    }];
}

- (void)connectIndex:(int)idx reply:(void (^)(NSString *))done {
/* wait for the selected server handshake */
    [self sendCommand:[NSString stringWithFormat:@"CONNECT %d", idx]
            timeoutMs:45000
                reply:done];
}

- (void)disconnectReply:(void (^)(NSString *))done {
    [self sendCommand:@"DISCONNECT" timeoutMs:10000 reply:done];
}

- (void)addServerLink:(NSString *)link reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"ADDSRV %@", link] reply:done];
}

- (void)replaceServerIndex:(int)idx link:(NSString *)link
                     reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"REPLACESRV %d %@", idx, link]
                reply:done];
}

- (void)addSubscriptionURL:(NSString *)url name:(NSString *)name
                     reply:(void (^)(NSString *))done {
    NSString *safeURL = [url stringByAddingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
    NSString *safeName = [name stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
    safeName = [safeName stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
    if (!safeURL || ![safeURL length] || ![safeName length]) {
        if (done) done(nil);
        return;
    }
    [self sendCommand:[NSString stringWithFormat:@"ADDSUB %@ %@", safeURL, safeName]
                reply:done];
}

- (void)replaceSubscriptionIndex:(int)idx name:(NSString *)name url:(NSString *)url
                           header:(NSString *)header reply:(void (^)(NSString *))done {
    NSString *safeURL = [url stringByAddingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
    NSString *safeName = [name stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
    safeName = [safeName stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
    NSString *safeHeader = [header ? header : @""
                            stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
    safeHeader = [safeHeader stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
    safeHeader = [safeHeader stringByAddingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
    safeHeader = [safeHeader stringByReplacingOccurrencesOfString:@"+" withString:@"%2B"];
    if (!safeURL || ![safeURL length] || ![safeName length] || !safeHeader) {
        if (done) done(nil);
        return;
    }
    if (![safeHeader length]) safeHeader = @"-";
    [self sendCommand:[NSString stringWithFormat:@"REPLACESUB %d %@ %@ %@",
                       idx, safeURL, safeHeader, safeName] reply:done];
}

- (void)deleteServerIndex:(int)idx reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"DELSRV %d", idx] reply:done];
}

- (void)deleteSubIndex:(int)idx reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"DELSUB %d", idx] reply:done];
}

- (void)refreshSubIndex:(int)idx reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"REFRESH %d", idx]
            timeoutMs:20000
                reply:done];
}

- (void)setSubscriptionHeader:(int)idx header:(NSString *)header
                         reply:(void (^)(NSString *))done {
    NSString *value = [header ? header : @""
                       stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
    value = [value stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
    NSString *encoded = [value stringByAddingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
    if (!encoded) { if (done) done(nil); return; }
    encoded = [encoded stringByReplacingOccurrencesOfString:@"+" withString:@"%2B"];
    if (![encoded length]) encoded = @"-";
    [self sendCommand:[NSString stringWithFormat:@"SETSUBHDR %d %@", idx, encoded]
                reply:done];
}

- (void)moveSection:(int)sectionId toPosition:(int)position
              reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"MOVESECTION %d %d", sectionId, position]
                reply:done];
}

- (void)moveManualServerIndex:(int)idx toPosition:(int)position
                        reply:(void (^)(NSString *))done {
    [self sendCommand:[NSString stringWithFormat:@"MOVEMANUAL %d %d", idx, position]
                reply:done];
}

- (void)checkIndex:(int)idx mode:(NSString *)mode
              reply:(void (^)(int, NSString *))done {
    NSArray *safeMode = [NSArray arrayWithObjects:@"tcp", @"real", @"proxy", @"tunnel", @"handshake", nil];
    if (![safeMode containsObject:mode]) { if (done) done(-1, @"unknown check type"); return; }
    NSString *command = [mode isEqualToString:@"tcp"]
        ? [NSString stringWithFormat:@"PING %d", idx]
        : [NSString stringWithFormat:@"CHECK %@ %d", mode, idx];
    [self sendCommand:command
            timeoutMs:12000 reply:^(NSString *reply) {
        int ms = -1;
        NSString *error = nil;
        /* STAT is broadcast independently of a check. on a busy ios 6 daemon
           it can arrive before PONG, so checking only the first reply line
           turned a completed tcp check into a timeout on screen. */
        for (NSString *raw in [reply componentsSeparatedByString:@"\n"]) {
            NSString *line = [raw stringByTrimmingCharactersInSet:
                              [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([line hasPrefix:@"PONG "]) {
                NSArray *parts = [line componentsSeparatedByString:@" "];
                if ([parts count] >= 3) ms = [[parts objectAtIndex:2] intValue];
            } else if ([line hasPrefix:@"ERR "]) {
                error = [line substringFromIndex:4];
            }
        }
        if (done) done(ms, ms >= 0 ? nil : (error ? error : reply));
    }];
}

/* senkod runs the amneziawg tunnel itself and answers with AWG lines closed
   by AWGEND. a probe waits for the server's handshake and a stop for the
   tunnel thread, which may be inside a dns lookup, hence the long timeout */
/* a status poll that got no reply returns nil: the poller reports the daemon
   itself, and an invented "error" line here raised a connection failed alert
   about amneziawg on every device whose senkod was simply not running */
- (void)awgRequest:(NSString *)verb path:(NSString *)path
       silenceIsError:(BOOL)silenceIsError reply:(void (^)(NSString *))done {
    [self awgRequest:verb path:path silenceIsError:silenceIsError
              uptime:^(NSString *status, long up) {
        (void)up;
        if (done) done(status);
    }];
}

/* up is the AWGUP line of a connected tunnel, -1 when the reply has none */
- (void)awgRequest:(NSString *)verb path:(NSString *)path
       silenceIsError:(BOOL)silenceIsError uptime:(void (^)(NSString *, long))done {
    NSString *cmd = [path length] ? [NSString stringWithFormat:@"AWG %@ %@", verb, path]
                                  : [NSString stringWithFormat:@"AWG %@", verb];
    [self sendCommand:cmd timeoutMs:20000 reply:^(NSString *reply) {
        NSMutableString *out = [NSMutableString string];
        NSString *error = nil;
        long up = -1;
        for (NSString *line in [reply componentsSeparatedByString:@"\n"]) {
            if ([line hasPrefix:@"AWG "])
                [out appendFormat:@"%@\n", [line substringFromIndex:4]];
            else if ([line hasPrefix:@"AWGUP "])
                up = atol([[line substringFromIndex:6] UTF8String]);
            else if ([line hasPrefix:@"ERR "])
                error = [line substringFromIndex:4];
        }
        NSString *status = [out length] ? out : nil;
        if (!status && error) status = [NSString stringWithFormat:@"error %@", error];
        else if (!status && !reply && silenceIsError) status = @"error senkod did not answer";
        if (done) done(status, up);
    }];
}

- (void)startAWGAtPath:(NSString *)path reply:(void (^)(NSString *))done {
    if (![path length]) { if (done) done(nil); return; }
    [self awgRequest:@"START" path:path silenceIsError:YES reply:done];
}

- (void)stopAWG:(void (^)(NSString *))done {
    [self awgRequest:@"STOP" path:nil silenceIsError:YES reply:done];
}

- (void)awgStatus:(void (^)(NSString *))done {
    [self awgRequest:@"STATUS" path:nil silenceIsError:NO reply:done];
}

- (void)awgStatusWithUptime:(void (^)(NSString *, long))done {
    [self awgRequest:@"STATUS" path:nil silenceIsError:NO uptime:done];
}

- (void)probeAWGAtPath:(NSString *)path reply:(void (^)(NSString *))done {
    if (![path length]) { if (done) done(nil); return; }
    [self awgRequest:@"PROBE" path:path silenceIsError:YES reply:done];
}

- (void)validateAWGAtPath:(NSString *)path reply:(void (^)(NSString *))done {
    if (![path length]) { if (done) done(nil); return; }
    [self awgRequest:@"VALIDATE" path:path silenceIsError:YES reply:done];
}

- (void)updatePackageAtPath:(NSString *)path reply:(void (^)(NSString *))done {
    [self updatePackageAtPath:path progress:nil reply:done];
}

/* dpkg runs in senkod --update, detached from senkod because the update
   stops senkod halfway through. its lines land in SENKO_UPDATE_LOG, which
   senkod truncated before answering, so the file is read from the start */
#define SENKO_UPDATE_WAIT_SECONDS 300

- (void)updatePackageAtPath:(NSString *)path
                   progress:(void (^)(NSString *))progress
                      reply:(void (^)(NSString *))done {
    if (![path length] || [path rangeOfCharacterFromSet:
                           [NSCharacterSet whitespaceAndNewlineCharacterSet]].location != NSNotFound) {
        if (done) done(@"UPDATE ERR the package path is empty or has spaces");
        return;
    }
    NSString *cmd = [NSString stringWithFormat:@"UPDATE %@", path];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        NSString *reply = [self blockingSend:cmd timeoutMs:5000];
        NSString *status = nil;
        if (!reply) {
            status = @"UPDATE ERR senkod did not answer";
        } else if (![reply hasPrefix:@"OK "] &&
                   [reply rangeOfString:@"\nOK "].location == NSNotFound) {
            NSRange err = [reply rangeOfString:@"ERR "];
            NSString *why = err.location != NSNotFound
                ? [[reply substringFromIndex:err.location + 4] stringByTrimmingCharactersInSet:
                   [NSCharacterSet whitespaceAndNewlineCharacterSet]]
                : @"unexpected answer";
            status = [NSString stringWithFormat:@"UPDATE ERR %@", why];
        }
        unsigned long long offset = 0;
        NSMutableString *pending = [NSMutableString string];
        for (int tick = 0; !status && tick < SENKO_UPDATE_WAIT_SECONDS * 2; ++tick) {
            NSAutoreleasePool *tickPool = [[NSAutoreleasePool alloc] init];
            NSFileHandle *fh = [NSFileHandle fileHandleForReadingAtPath:@SENKO_UPDATE_LOG];
            if (fh) {
                [fh seekToFileOffset:offset];
                NSData *chunk = [fh readDataToEndOfFile];
                [fh closeFile];
                offset += [chunk length];
                NSString *text = [[NSString alloc] initWithData:chunk encoding:NSUTF8StringEncoding];
                if (!text)
                    text = [[NSString alloc] initWithData:chunk encoding:NSISOLatin1StringEncoding];
                if (text) [pending appendString:text];
                [text release];
            }
            for (;;) {
                NSRange nl = [pending rangeOfString:@"\n"];
                if (nl.location == NSNotFound) break;
                NSString *line = [[pending substringToIndex:nl.location]
                                  stringByTrimmingCharactersInSet:
                                  [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                [pending deleteCharactersInRange:NSMakeRange(0, nl.location + 1)];
                if (![line length]) continue;
                if ([line hasPrefix:@"UPDATE OK"] || [line hasPrefix:@"UPDATE ERR"])
                    status = [line copy];
                if (progress)
                    dispatch_async(dispatch_get_main_queue(), ^{ progress(line); });
                if (status) break;
            }
            [tickPool drain];
            if (!status) usleep(500000);
            else [status autorelease];
        }
        if (!status)
            status = [NSString stringWithFormat:@"UPDATE ERR no result after %d s, see %s",
                      SENKO_UPDATE_WAIT_SECONDS, SENKO_UPDATE_LOG];
        [status retain];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (done) done(status);
            [status release];
        });
        [pool drain];
    });
}

@end
