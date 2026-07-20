#ifndef SENKO_APP_COMMON_H
#define SENKO_APP_COMMON_H

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import "control_client.h"

#define SENKO_SOCK @"/var/tmp/senkod.sock"
#define SENKO_VERSION @"v1.0.0-stable"
#define SENKO_HIDE_LINKS_KEY @"SenkoHideServerLinks"
#define SENKO_PINNED_SUB_URL_KEY @"SenkoPinnedSubscriptionURL"
#define SENKO_AWG_PROFILE_KEY @"SenkoAWGProfilePath"
#define SENKO_AWG_PROFILE_PATH @"/var/mobile/Library/Preferences/Senko/amneziawg.conf"
#define SENKO_SELECTED_BACKEND_KEY @"SenkoSelectedBackend"

typedef NS_ENUM(NSInteger, SenkoBackendKind) {
    SenkoBackendServer = 0,
    SenkoBackendAmneziaWG = 1,
    SenkoBackendNone = 2
};

NSString *SenkoAboutAppReport(void);

@class EditServerVC;
@class FileImportVC;
@class EditSubscriptionVC;
@class EditAWGVC;
@class SettingsVC;
@class MainVC;
@class AboutVC;
@class LogsVC;

@protocol FileImportDelegate
- (void)fileImportVCDidCancel:(FileImportVC *)vc;
- (void)fileImportVC:(FileImportVC *)vc didPickPath:(NSString *)path;
@end

@protocol EditServerDelegate
- (void)editServerVC:(EditServerVC *)vc saveLink:(NSString *)link index:(int)idx;
@end

@protocol EditSubscriptionDelegate
- (void)editSubscriptionVC:(EditSubscriptionVC *)vc
          saveSubWithIndex:(int)idx
                      name:(NSString *)name
                       url:(NSString *)url;
@end

@protocol EditAWGDelegate
- (void)editAWGVC:(EditAWGVC *)vc saveConfig:(NSString *)config;
@end

/* public class shells for cross-file types */
@interface MainVC : UIViewController
@end

@interface SettingsVC : UIViewController
@end

@interface AboutVC : UIViewController
@end

@interface LogsVC : UIViewController
@end

@interface EditServerVC : UIViewController
- (id)initWithLink:(NSString *)link index:(int)idx delegate:(id<EditServerDelegate>)delegate;
@end

@interface EditAWGVC : UIViewController
- (id)initWithConfig:(NSString *)config delegate:(id<EditAWGDelegate>)delegate;
@end

@interface EditSubscriptionVC : UIViewController
- (id)initWithSub:(SenkoSub *)sub delegate:(id<EditSubscriptionDelegate>)delegate;
@end

@interface FileImportVC : UIViewController
- (id)initWithPath:(NSString *)path delegate:(id<FileImportDelegate>)delegate;
@end

#endif
