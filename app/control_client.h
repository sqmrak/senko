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

@interface SenkoSub : NSObject {
@public
    int      index;
    NSString *name;
    NSString *url;
    NSString *header;
    unsigned long long expire;
    unsigned long long upload;
    unsigned long long download;
    unsigned long long total;
    NSString *description;
    NSString *supportURL;
}
@end

@interface SenkoControl : NSObject {
    NSString *_sockPath;
}

- (id)initWithSocketPath:(NSString *)path;

- (void)sendCommand:(NSString *)cmd reply:(void (^)(NSString *reply))done;
- (void)sendCommand:(NSString *)cmd timeoutMs:(int)timeoutMs reply:(void (^)(NSString *reply))done;

- (void)probeDaemon:(void (^)(BOOL up))done;
- (void)kickDaemon:(void (^)(BOOL ok, NSString *detail))done;
- (void)ensureDaemon:(void (^)(BOOL up, NSString *detail))done;

- (void)listCatalog:(void (^)(NSArray *servers, NSArray *subs, NSArray *order))done;
- (void)listServers:(void (^)(NSArray *servers))done;
- (void)serverLinkIndex:(int)idx reply:(void (^)(NSString *link))done;

- (void)statusState:(void (^)(NSString *state))done;
/* seconds the tunnel has been up, 0 when the daemon reports none */
- (void)statusStateWithUptime:(void (^)(NSString *state, long uptime))done;
- (void)connectIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)disconnectReply:(void (^)(NSString *reply))done;
- (void)addServerLink:(NSString *)link reply:(void (^)(NSString *reply))done;
- (void)addSubscriptionURL:(NSString *)url name:(NSString *)name
                     reply:(void (^)(NSString *reply))done;
- (void)deleteServerIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)deleteSubIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)refreshSubIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)setSubscriptionHeader:(int)idx header:(NSString *)header
                         reply:(void (^)(NSString *reply))done;
- (void)moveSection:(int)sectionId toPosition:(int)position
              reply:(void (^)(NSString *reply))done;
- (void)moveManualServerIndex:(int)idx toPosition:(int)position
                        reply:(void (^)(NSString *reply))done;
- (void)pingIndex:(int)idx reply:(void (^)(int ms))done;
- (void)checkIndex:(int)idx mode:(NSString *)mode reply:(void (^)(int ms, NSString *error))done;
/* the device id the daemon sends to subscription panels as x-hwid */
- (void)deviceHWID:(void (^)(NSString *hwid))done;
/* the daemon reads its own log, because the ui is not allowed to on every
   jailbreak */
- (void)daemonLogTail:(void (^)(NSString *text))done;
/* hand raw pasted or picked content to the daemon parser */
- (void)importContent:(NSData *)data reply:(void (^)(NSString *reply))done;
- (void)clearManualServers:(void (^)(NSString *reply))done;
- (void)exportBackup:(void (^)(NSString *reply))done;
- (void)restoreBackup:(void (^)(NSString *reply))done;

- (void)startAWGAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)stopAWG:(void (^)(NSString *status))done;
- (void)awgStatus:(void (^)(NSString *status))done;
- (void)probeAWGAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)validateAWGAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)updatePackageAtPath:(NSString *)path reply:(void (^)(NSString *status))done;
- (void)updatePackageAtPath:(NSString *)path
                   progress:(void (^)(NSString *line))progress
                      reply:(void (^)(NSString *status))done;

@end
