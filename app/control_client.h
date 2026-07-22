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

/* one subscription row */
@interface SenkoSub : NSObject {
@public
    int      index;
    NSString *name;
    NSString *url;
    unsigned long long expire;
}
@end

@interface SenkoControl : NSObject {
    NSString *_sockPath;
}

- (id)initWithSocketPath:(NSString *)path;

- (void)sendCommand:(NSString *)cmd reply:(void (^)(NSString *reply))done;
- (void)sendCommand:(NSString *)cmd timeoutMs:(int)timeoutMs reply:(void (^)(NSString *reply))done;

/* check daemon status */
- (void)probeDaemon:(void (^)(BOOL up))done;
/* start the root daemon helper */
- (void)kickDaemon:(void (^)(BOOL ok, NSString *detail))done;
/* start the helper and check again */
- (void)ensureDaemon:(void (^)(BOOL up, NSString *detail))done;

/* parse servers and subscriptions */
- (void)listCatalog:(void (^)(NSArray *servers, NSArray *subs, NSArray *order))done;
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
- (void)moveSection:(int)sectionId toPosition:(int)position
              reply:(void (^)(NSString *reply))done;
- (void)moveManualServerIndex:(int)idx toPosition:(int)position
                        reply:(void (^)(NSString *reply))done;
- (void)pingIndex:(int)idx reply:(void (^)(int ms))done;

/* start awg through control */
- (void)startAWGAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)stopAWG:(void (^)(NSString *status))done;
- (void)awgStatus:(void (^)(NSString *status))done;
- (void)probeAWGAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)validateAWGAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)updatePackageAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
/* pass update stages to the ui */
- (void)updatePackageAtPath:(NSString *)path
                   progress:(void (^)(NSString *line))progress
                      reply:(void (^)(NSString *status))done;

@end
