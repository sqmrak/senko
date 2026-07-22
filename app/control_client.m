#import "control_client.h"

#import <sys/socket.h>
#import <sys/un.h>
#import <sys/wait.h>
#import <fcntl.h>
#import <unistd.h>
#import <string.h>
#import <errno.h>
#import <spawn.h>

extern char **environ;

@implementation SenkoServer
- (void)dealloc {
    [security release];
    [proto release];
    [net release];
    [host release];
    [remark release];
    [super dealloc];
}
@end

@implementation SenkoSub
- (void)dealloc {
    [name release];
    [url release];
    [super dealloc];
}
@end

@implementation SenkoControl

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
                (llen >= 4 && memcmp(ln, "SRV ", 4) == 0) ||
                (llen >= 4 && memcmp(ln, "SUB ", 4) == 0) ||
                (llen >= 8 && memcmp(ln, "SUBMETA ", 8) == 0) ||
                (llen >= 8 && memcmp(ln, "SECTION ", 8) == 0) ||
                (llen >= 6 && memcmp(ln, "FDATA ", 6) == 0);
            if (!stream) return 1; /* stop on terminal records */
        }
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

/* let mobile clients open the socket */
static int senkoCtlAuth(int fd, NSString *sockPath) {
    NSString *tok = senkoLoadCtlToken(sockPath);
    if (![tok length]) return 0;
    char line[96];
    int n = snprintf(line, sizeof line, "AUTH %s\n", [tok UTF8String]);
    if (n <= 0 || (size_t)n >= sizeof line) return -1;
    if (write_all_fd(fd, line, (size_t)n) != 0) return -1;
    set_rcv_timeout(fd, 1500);
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

/* probe the daemon before auth */
    BOOL is_status = [cmd hasPrefix:@"STATUS"];
    if (!is_status) {
        if (senkoCtlAuth(fd, _sockPath) != 0) {
            NSString *tok = senkoLoadCtlToken(_sockPath);
            if ([tok length]) {
                close(fd);
                return nil;
            }
        }
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
    int (*done_fn)(const char *, size_t) = is_tunnel ? tunnel_reply_complete : reply_complete;

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

- (void)sendCommand:(NSString *)cmd timeoutMs:(int)timeoutMs reply:(void (^)(NSString *))done {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSString *reply = [self blockingSend:cmd timeoutMs:timeoutMs];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (done) done(reply);
        });
    });
}

- (void)probeDaemon:(void (^)(BOOL))done {
    [self sendCommand:@"STATUS" timeoutMs:1500 reply:^(NSString *reply) {
        BOOL up = (reply != nil && [reply hasPrefix:@"STATE "]);
        if (done) done(up);
    }];
}

/* show helper errors */
- (void)kickDaemon:(void (^)(BOOL, NSString *))done {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        const char *path = "/usr/bin/senko-kick";
        BOOL ok = NO;
        NSString *detail = nil;
        if (access(path, X_OK) != 0) {
            detail = @"senko-kick missing, reinstall package";
        } else {
            pid_t pid = 0;
            char *argv[] = { (char *)path, NULL };
            int rc = posix_spawn(&pid, path, NULL, NULL, argv, environ);
            if (rc != 0) {
                detail = @"cannot start senko-kick";
            } else {
                int st = 0;
                while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
                if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
                    ok = YES;
                    detail = @"daemon started";
                } else {
                    int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
                    detail = [NSString stringWithFormat:@"daemon start failed (%d)", code];
                }
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (done) done(ok, detail);
        });
    });
}

- (void)ensureDaemon:(void (^)(BOOL, NSString *))done {
    [self probeDaemon:^(BOOL up) {
        if (up) {
            if (done) done(YES, nil);
            return;
        }
/* kick once and poll */
        [self kickDaemon:^(BOOL kicked, NSString *detail) {
            if (!kicked) {
                NSString *msg = detail ? detail : @"daemon offline";
                if (done)
                    done(NO, [msg stringByAppendingString:@" (see /var/log/senko-kick.log)"]);
                return;
            }
            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                BOOL up2 = NO;
                for (int i = 0; i < 12 && !up2; ++i) {
                    NSString *r = [self blockingSend:@"STATUS" timeoutMs:800];
                    if (r && [r hasPrefix:@"STATE "]) up2 = YES;
                    else usleep(250000);
                }
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (up2) {
                        if (done) done(YES, detail ? detail : @"daemon started");
                    } else if (done) {
                        done(NO, @"daemon still offline (see /var/log/senko-kick.log)");
                    }
                });
            });
        }];
    }];
}

static BOOL tokenIsProto(NSString *s) {
    return [s isEqualToString:@"vless"] || [s isEqualToString:@"socks5"] ||
           [s isEqualToString:@"http"] || [s isEqualToString:@"https"];
}

static BOOL tokenIsNet(NSString *s) {
    return [s isEqualToString:@"tcp"] || [s isEqualToString:@"ws"] ||
           [s isEqualToString:@"grpc"] || [s isEqualToString:@"http"] ||
           [s isEqualToString:@"xhttp"];
}

static BOOL tokenIsSecurity(NSString *s) {
    return [s isEqualToString:@"none"] || [s isEqualToString:@"tls"] ||
           [s isEqualToString:@"reality"] || [s isEqualToString:@"unknown"];
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
    BOOL newLayout = [t count] >= 10 &&
        tokenIsProto([t objectAtIndex:4]) &&
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
            if ([ln hasPrefix:@"SECTION "]) {
                NSArray *t = [ln componentsSeparatedByString:@" "];
                for (NSUInteger i = 1; i < [t count]; ++i)
                    [order addObject:[NSNumber numberWithInt:[[t objectAtIndex:i] intValue]]];
                continue;
            }
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

- (void)statusState:(void (^)(NSString *))done {
    [self sendCommand:@"STATUS" reply:^(NSString *reply) {
        NSString *state = nil;
        if ([reply hasPrefix:@"STATE "]) {
            state = [[reply substringFromIndex:6]
                     stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        }
        if (done) done(state);
    }];
}

- (void)connectIndex:(int)idx reply:(void (^)(NSString *))done {
/* leave room for four failovers */
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

- (void)pingIndex:(int)idx reply:(void (^)(int))done {
/* keep two tcp samples under the control timeout */
    [self sendCommand:[NSString stringWithFormat:@"PING %d", idx]
            timeoutMs:5000
                reply:^(NSString *reply) {
        int ms = -1;
        if ([reply hasPrefix:@"PONG "]) {
            NSArray *t = [[reply stringByTrimmingCharactersInSet:
                           [NSCharacterSet whitespaceAndNewlineCharacterSet]]
                          componentsSeparatedByString:@" "];
            if ([t count] >= 3) ms = [[t objectAtIndex:2] intValue];
        }
        if (done) done(ms);
    }];
}

- (void)runAWGHelper:(NSArray *)args reply:(void (^)(NSString *))done {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        const char *path = "/usr/bin/senko-kick";
        NSMutableArray *argvData = [NSMutableArray array];
        [argvData addObject:[NSData dataWithBytes:path length:strlen(path) + 1]];
        for (NSString *arg in args) {
            const char *s = [arg UTF8String];
            if (!s) continue;
            [argvData addObject:[NSData dataWithBytes:s length:strlen(s) + 1]];
        }
        char *argv[5] = { NULL, NULL, NULL, NULL, NULL };
        NSUInteger count = [argvData count];
        if (count > 4) count = 4;
        for (NSUInteger i = 0; i < count; ++i) argv[i] = (char *)[[argvData objectAtIndex:i] bytes];
        char output[192] = {0};
        int pipefd[2] = {-1, -1};
        pid_t pid = 0;
        BOOL ok = pipe(pipefd) == 0;
        if (ok) {
            posix_spawn_file_actions_t fa;
            posix_spawn_file_actions_init(&fa);
            posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
            posix_spawn_file_actions_addclose(&fa, pipefd[0]);
            posix_spawn_file_actions_addclose(&fa, pipefd[1]);
            ok = posix_spawn(&pid, path, &fa, NULL, argv, environ) == 0;
            posix_spawn_file_actions_destroy(&fa);
            close(pipefd[1]);
            if (ok) {
                ssize_t n = read(pipefd[0], output, sizeof output - 1);
                if (n > 0) output[n] = '\0';
                int st = 0;
                while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            }
            close(pipefd[0]);
        }
        NSString *status = output[0] ? [NSString stringWithUTF8String:output] : nil;
        dispatch_async(dispatch_get_main_queue(), ^{ if (done) done(status); });
    });
}

- (void)startAWGAtPath:(NSString *)path reply:(void (^)(NSString *))done {
    if (![path length]) { if (done) done(nil); return; }
    [self runAWGHelper:[NSArray arrayWithObjects:@"--awg", path, nil] reply:done];
}

- (void)stopAWG:(void (^)(NSString *))done {
    [self runAWGHelper:[NSArray arrayWithObject:@"--awg-stop"] reply:done];
}

- (void)awgStatus:(void (^)(NSString *))done {
    [self runAWGHelper:[NSArray arrayWithObject:@"--awg-status"] reply:done];
}

- (void)probeAWGAtPath:(NSString *)path reply:(void (^)(NSString *))done {
    if (![path length]) { if (done) done(nil); return; }
    [self runAWGHelper:[NSArray arrayWithObjects:@"--awg-probe", path, nil] reply:done];
}

- (void)validateAWGAtPath:(NSString *)path reply:(void (^)(NSString *))done {
    if (![path length]) { if (done) done(nil); return; }
    [self runAWGHelper:[NSArray arrayWithObjects:@"--awg-validate", path, nil] reply:done];
}

- (void)updatePackageAtPath:(NSString *)path reply:(void (^)(NSString *))done {
    [self updatePackageAtPath:path progress:nil reply:done];
}

- (void)updatePackageAtPath:(NSString *)path
                   progress:(void (^)(NSString *))progress
                      reply:(void (^)(NSString *))done {
    if (![path length]) {
        if (done) done(@"UPDATE ERR empty path");
        return;
    }
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        const char *bin = "/usr/bin/senko-kick";
        if (access(bin, X_OK) != 0) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (done) done(@"UPDATE ERR senko-kick missing");
            });
            return;
        }
/* keep the path for spawn */
        NSString *pathCopy = [[path copy] autorelease];
        const char *p = [pathCopy fileSystemRepresentation];
        if (!p) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (done) done(@"UPDATE ERR bad path encoding");
            });
            return;
        }
        char *argv[] = { (char *)bin, (char *)"--update", (char *)p, NULL };
        int pipefd[2] = { -1, -1 };
        NSMutableString *last = [NSMutableString string];
        NSMutableString *lineBuf = [NSMutableString string];
        BOOL sawTerminal = NO;
        pid_t pid = 0;
        int spawn_errno = 0;
        BOOL ok = pipe(pipefd) == 0;
        if (!ok) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (done) done(@"UPDATE ERR pipe failed");
            });
            return;
        }
/* use a mobile-readable stderr path */
        const char *errlog = "/tmp/senko-update.log";
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
        if (posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, errlog,
                                             O_WRONLY | O_CREAT | O_APPEND, 0666) != 0) {
            posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null",
                                             O_WRONLY, 0);
        }
        posix_spawn_file_actions_addclose(&fa, pipefd[0]);
        posix_spawn_file_actions_addclose(&fa, pipefd[1]);
        int prc = posix_spawn(&pid, bin, &fa, NULL, argv, environ);
        posix_spawn_file_actions_destroy(&fa);
        close(pipefd[1]);
        ok = (prc == 0);
        if (!ok) spawn_errno = prc;
        int exitSt = -1;
        if (ok) {
            char buf[256];
            for (;;) {
                ssize_t n = read(pipefd[0], buf, sizeof buf);
                if (n <= 0) break;
                NSString *chunk = [[NSString alloc] initWithBytes:buf
                                                           length:(NSUInteger)n
                                                         encoding:NSUTF8StringEncoding];
                if (!chunk) {
/* use a fallback path for bad utf8 */
                    chunk = [[NSString alloc] initWithBytes:buf
                                                     length:(NSUInteger)n
                                                   encoding:NSISOLatin1StringEncoding];
                }
                if (!chunk) continue;
                [lineBuf appendString:chunk];
                [chunk release];
                for (;;) {
                    NSRange nl = [lineBuf rangeOfString:@"\n"];
                    if (nl.location == NSNotFound) break;
                    NSString *line = [lineBuf substringToIndex:nl.location];
                    [lineBuf deleteCharactersInRange:NSMakeRange(0, nl.location + 1)];
                    NSString *trim = [line stringByTrimmingCharactersInSet:
                                      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                    if (![trim length]) continue;
                    if ([trim hasPrefix:@"UPDATE OK"] || [trim hasPrefix:@"UPDATE ERR"]) {
                        [last setString:trim];
                        sawTerminal = YES;
                    }
                    if (progress) {
                        dispatch_async(dispatch_get_main_queue(), ^{
                            progress(trim);
                        });
                    }
                }
            }
            int st = 0;
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            if (WIFEXITED(st))
                exitSt = WEXITSTATUS(st);
        }
        close(pipefd[0]);
        if ([lineBuf length]) {
            NSString *trim = [lineBuf stringByTrimmingCharactersInSet:
                              [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([trim length]) {
                if ([trim hasPrefix:@"UPDATE OK"] || [trim hasPrefix:@"UPDATE ERR"]) {
                    [last setString:trim];
                    sawTerminal = YES;
                }
                if (progress) {
                    dispatch_async(dispatch_get_main_queue(), ^{ progress(trim); });
                }
            }
        }
        NSString *status = nil;
        if (sawTerminal && [last length]) {
            status = [[last copy] autorelease];
        } else if (!ok) {
            status = [NSString stringWithFormat:@"UPDATE ERR spawn failed (%d)", spawn_errno];
        } else if (exitSt != 0) {
            status = [NSString stringWithFormat:@"UPDATE ERR helper exit %d", exitSt];
        } else {
            status = @"UPDATE ERR no status from helper";
        }
        dispatch_async(dispatch_get_main_queue(), ^{ if (done) done(status); });
    });
}

@end
