#import <Foundation/Foundation.h>

/* the app talks to NetworkExtension through runtime lookup so the armv7/iOS 5
   build never links or sends a selector that did not exist there */
@interface SenkoNativeVPN : NSObject {
    id _manager;
}

+ (BOOL)available;
- (void)startWithConfiguration:(NSString *)json
                   serverAddress:(NSString *)serverAddress
                    completion:(void (^)(NSError *error))completion;
- (void)stopWithCompletion:(void (^)(NSError *error))completion;
/* connectedDate is nil unless status is connected. it comes from the
   extension's own connection object, so it survives this process being
   suspended and answers the same question the daemon's own clock answers
   on the jailbroken build */
- (void)status:(void (^)(NSInteger status, NSDate *connectedDate))completion;
@end
