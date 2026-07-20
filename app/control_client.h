#import <Foundation/Foundation.h>

@interface SenkoServer : NSObject {
@public
    int      index;
    BOOL     selected;
    int      group;
    NSString *proto;
    NSString *net;
    NSString *security;
    BOOL     supported;
    NSString *host;
    int      port;
    NSString *remark;
}
@end

/* one sub line from catalog */
@interface SenkoSub : NSObject {
@public
    int      index;
    NSString *name;
    NSString *url;
}
@end

@interface SenkoControl : NSObject {
    NSString *_sockPath;
}

- (id)initWithSocketPath:(NSString *)path;

- (void)sendCommand:(NSString *)cmd reply:(void (^)(NSString *reply))done;
- (void)sendCommand:(NSString *)cmd timeoutMs:(int)timeoutMs reply:(void (^)(NSString *reply))done;

/* 1 if status gets a reply */
- (void)probeDaemon:(void (^)(BOOL up))done;
/* setuid kick; mobile ui cannot start root daemons */
- (void)kickDaemon:(void (^)(BOOL ok, NSString *detail))done;
/* kick if status fails, then recheck */
- (void)ensureDaemon:(void (^)(BOOL up, NSString *detail))done;

/* parse list catalog into servers + subs */
- (void)listCatalog:(void (^)(NSArray *servers, NSArray *subs))done;
- (void)listServers:(void (^)(NSArray *servers))done;
- (void)serverLinkIndex:(int)idx reply:(void (^)(NSString *link))done;

- (void)statusState:(void (^)(NSString *state))done;
- (void)connectIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)disconnectReply:(void (^)(NSString *reply))done;
- (void)addServerLink:(NSString *)link reply:(void (^)(NSString *reply))done;
- (void)addSubscriptionURL:(NSString *)url name:(NSString *)name
                     reply:(void (^)(NSString *reply))done;
- (void)deleteServerIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)deleteSubIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)refreshSubIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)pingIndex:(int)idx reply:(void (^)(int ms))done;

/* start awg via ctl; no shell argv */
- (void)startAWGAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)stopAWG:(void (^)(NSString *status))done;
- (void)awgStatus:(void (^)(NSString *status))done;
- (void)probeAWGAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)validateAWGAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)updatePackageAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
/* forward update stage lines for progress */
- (void)updatePackageAtPath:(NSString *)path
                   progress:(void (^)(NSString *line))progress
                      reply:(void (^)(NSString *status))done;

@end
