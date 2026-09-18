#import <Foundation/Foundation.h>

@class SenkoServer;

/* local catalog storage is used when a stock app has no control socket */
NSArray *SenkoNativeLoadServers(void);
void SenkoNativeSaveServers(NSArray *servers);
void SenkoNativeAttachCachedLinks(NSArray *servers);
void SenkoNativeCacheServerLink(SenkoServer *server, NSString *link);
int SenkoNativeNextServerIndex(NSArray *servers);
SenkoServer *SenkoNativeServerFromLink(NSString *link, int index, NSString **error);
NSArray *SenkoNativeServersFromContent(NSData *data, NSString **error);

typedef void (^SenkoNativeConfigReply)(NSString *json, NSString *endpoint,
                                       NSString *error);
void SenkoNativeConfigurationForLink(NSString *link,
                                     SenkoNativeConfigReply reply);

typedef void (^SenkoNativeProbeReply)(int milliseconds, NSString *error);
void SenkoNativeProbeLink(NSString *link, SenkoNativeProbeReply reply);
