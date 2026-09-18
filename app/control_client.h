#import <Foundation/Foundation.h>

NSString *SenkoControlStateFromReply(NSString *reply, long *uptime);

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
    NSString *link;
/* other catalog indexes that show under this server's row because they
   reduce to the same display name (same country, different transport). nil
   when this server is not standing in for any siblings. */
    NSArray  *dupIndexes;
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

/* one routing rule as the daemon reports it: the hit counter is what tells the
   user whether a rule they wrote is doing anything */
@interface SenkoRule : NSObject {
@public
    int      index;
    NSString *action;
    NSString *type;
    NSString *value;
    unsigned long long hits;
}
@end

/* one line of the daemon's diagnostic report. the key is a dotted path and the
   value is free text, so an app older than the daemon shows a fact it has no
   title for instead of dropping it */
@interface SenkoDiagFact : NSObject {
@public
    NSString *key;
    NSString *value;
}
@end

/* one named step of a check and how long the check had been running when it
   finished. a check that stops halfway names the step it stopped at */
@interface SenkoCheckStage : NSObject {
@public
    NSString *name;
    int      ms;
    BOOL     ok;
}
@end

@interface SenkoControl : NSObject {
    NSString *_sockPath;
    uint64_t _trafficUp;
    uint64_t _trafficDown;
    BOOL _trafficKnown;
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
/* JSON rendered by the daemon for the native Network Extension provider */
- (void)nativeConfigurationIndex:(int)idx
                            reply:(void (^)(NSString *json, NSString *error))done;

- (void)statusState:(void (^)(NSString *state))done;
- (void)traffic:(void (^)(BOOL known, uint64_t up, uint64_t down))done;
/* seconds the tunnel has been up, 0 when the daemon reports none */
- (void)statusStateWithUptime:(void (^)(NSString *state, long uptime))done;
- (void)connectIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)disconnectReply:(void (^)(NSString *reply))done;
- (void)addServerLink:(NSString *)link reply:(void (^)(NSString *reply))done;
- (void)replaceServerIndex:(int)idx link:(NSString *)link
                     reply:(void (^)(NSString *reply))done;
- (void)addSubscriptionURL:(NSString *)url name:(NSString *)name
                     reply:(void (^)(NSString *reply))done;
- (void)replaceSubscriptionIndex:(int)idx name:(NSString *)name url:(NSString *)url
                           header:(NSString *)header reply:(void (^)(NSString *reply))done;
- (void)deleteServerIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)deleteSubIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)refreshSubIndex:(int)idx reply:(void (^)(NSString *reply))done;
- (void)setSubscriptionHeader:(int)idx header:(NSString *)header
                         reply:(void (^)(NSString *reply))done;
- (void)moveSection:(int)sectionId toPosition:(int)position
              reply:(void (^)(NSString *reply))done;
- (void)moveManualServerIndex:(int)idx toPosition:(int)position
                        reply:(void (^)(NSString *reply))done;
- (void)checkIndex:(int)idx mode:(NSString *)mode reply:(void (^)(int ms, NSString *error))done;
/* the device id the daemon sends to subscription panels as x-hwid */
- (void)deviceHWID:(void (^)(NSString *hwid))done;
/* the daemon reads its own log, because the ui is not allowed to on every
   jailbreak */
- (void)daemonLogTail:(void (^)(NSString *text))done;
/* hand raw pasted or picked content to the daemon parser */
- (void)importContent:(NSData *)data reply:(void (^)(NSString *reply))done;
- (void)clearManualServers:(void (^)(NSString *reply))done;
/* the daemon settings, as the key/value pairs it answers SETTINGS with. the
   daemon owns them, so the screen reads them back instead of keeping a copy in
   NSUserDefaults that a reinstall or a restore would silently disagree with */
- (void)daemonSettings:(void (^)(NSDictionary *settings))done;
- (void)setSetting:(NSString *)key value:(NSString *)value
             reply:(void (^)(NSString *reply))done;
/* routing rules. the daemon keeps them with the catalog, so the screen reads
   them back after every change instead of tracking its own order */
- (void)listRules:(void (^)(NSArray *rules))done;
- (void)addRuleAction:(NSString *)action type:(NSString *)type
                value:(NSString *)value reply:(void (^)(NSString *reply))done;
- (void)deleteRuleIndex:(int)index reply:(void (^)(NSString *reply))done;
/* which rung of every fallback ladder the daemon actually took */
- (void)daemonDiagnostics:(void (^)(NSArray *facts))done;
/* the same check the server rows run, with the stage list the daemon walked.
   stages arrive whether the check passed or failed */
- (void)checkIndex:(int)idx mode:(NSString *)mode
            stages:(void (^)(NSArray *stages, int ms, NSString *error))done;
/* the pf config pfctl loaded, or the ipfw rules the backend spawned */
- (void)firewallConfig:(void (^)(NSString *text, NSString *error))done;
/* drop one named piece of accumulated state: dns, bypass, rules or config */
- (void)flushTarget:(NSString *)what reply:(void (^)(NSString *reply))done;
/* issue a new device id, so a panel that binds to one sees a new device */
- (void)resetDeviceHWID:(void (^)(NSString *hwid, NSString *error))done;
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
