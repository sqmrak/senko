#import <Foundation/Foundation.h>
#include <stdint.h>

/* start fails with this domain and code when the system has no usable packet
   tunnel extension for the app. the profile is gone by then and +available
   answers NO until the next build, so the caller moves the tunnel to the
   daemon backend */
extern NSString * const SenkoNativeVPNErrorDomain;
enum { SenkoNativeVPNErrorProviderMissing = 2 };

/* the app talks to NetworkExtension through runtime lookup so the armv7/iOS 5
   build never links or sends a selector that did not exist there */
@interface SenkoNativeVPN : NSObject {
    id _manager;
}

/* NO on devices that cannot host the extension and on a device where the
   system refused it for this build */
+ (BOOL)available;
- (void)startWithConfiguration:(NSString *)json
                   serverAddress:(NSString *)serverAddress
                    completion:(void (^)(NSError *error))completion;
- (void)stopWithCompletion:(void (^)(NSError *error))completion;
/* Settings lists a profile the system cannot run as "needs an update", and
   nothing but the app that made it can remove it */
- (void)removeInvalidProfiles;
/* connectedDate is nil unless status is connected. it comes from the
   extension's own connection object, so it survives this process being
   suspended and answers the same question the daemon's own clock answers
   on the jailbroken build */
- (void)status:(void (^)(NSInteger status, NSDate *connectedDate))completion;
/* counters come from the packet tunnel extension, not senkod */
- (void)traffic:(void (^)(BOOL known, uint64_t up, uint64_t down))completion;
@end
