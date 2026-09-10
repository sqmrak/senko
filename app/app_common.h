#ifndef SENKO_APP_COMMON_H
#define SENKO_APP_COMMON_H

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import "control_client.h"

#define SENKO_SOCK @"/var/tmp/senkod.sock"
#define SENKO_VERSION @"v09102026-patch1-nightly"
#define SENKO_HIDE_LINKS_KEY @"SenkoHideServerLinks"
#define SENKO_PINNED_SUB_URL_KEY @"SenkoPinnedSubscriptionURL"
#define SENKO_AWG_PROFILE_KEY @"SenkoAWGProfilePath"
#define SENKO_AWG_PROFILE_PATH @"/var/mobile/Library/Preferences/Senko/amneziawg.conf"
#define SENKO_SELECTED_BACKEND_KEY @"SenkoSelectedBackend"
#define SENKO_VPN_BADGE_KEY @"SenkoVPNStatusBarBadge"
/* springboard reads this marker instead of the app defaults, which live in a
   domain it cannot see. present means the badge is turned off */
#define SENKO_VPN_BADGE_OFF_PATH "/var/mobile/Library/Preferences/com.senko.vpnicon.off"
#define SENKO_LANGUAGE_KEY @"SenkoLanguage"
/* 0 keeps the order the daemon stores, 1 sorts by name, 2 by latency */
#define SENKO_SERVER_SORT_KEY @"SenkoServerSort"

typedef enum {
    SenkoSortManual = 0,
    SenkoSortName,
    SenkoSortPing
} SenkoServerSort;

typedef NS_ENUM(NSInteger, SenkoBackendKind) {
    SenkoBackendServer = 0,
    SenkoBackendAmneziaWG = 1,
    SenkoBackendNone = 2
};

NSString *SenkoAboutAppReport(void);
/* one place for the shop address, so the about screen can print it, copy it and
   open it without three copies of the same string drifting apart */
NSString *SenkoSponsorURL(void);
/* the springboard badge is a user choice because on some firmwares the
   status bar drops the wifi glyph to make room for it */
BOOL SenkoVPNBadgeEnabled(void);
void SenkoVPNBadgeSetEnabled(BOOL enabled);

/* the device id the daemon actually sends, read from the file both processes
   share. nil until the daemon has written one */
NSString *SenkoSharedDeviceHWID(void);
extern NSString * const SenkoLanguageDidChangeNotification;
BOOL SenkoLanguageIsRussian(void);
void SenkoSetLanguage(BOOL russian);
NSString *SenkoLanguageName(void);
NSString *SenkoLocalizedText(NSString *text);
NSString *SenkoHumanReadableError(NSString *text);
NSString *SenkoRedactSecrets(NSString *text);
void SenkoLocalizationInstall(void);
void SenkoRelocalizeAllWindows(void);

@class EditServerVC;
@class FileImportVC;
@class EditSubscriptionVC;
@class EditAWGVC;
@class SettingsVC;
@class MainVC;
@class AboutVC;
@class LogsVC;
@class SubscriptionInfoVC;

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
                       url:(NSString *)url
                    header:(NSString *)header;
@end

@protocol EditAWGDelegate
- (void)editAWGVC:(EditAWGVC *)vc saveConfig:(NSString *)config;
@end

@interface MainVC : UIViewController
@end

@interface SettingsVC : UIViewController <UITableViewDataSource, UITableViewDelegate,
                                          FileImportDelegate, EditServerDelegate,
                                          UIAlertViewDelegate>
@end

@interface AboutVC : UIViewController
@end

@interface LogsVC : UIViewController
@end

@interface SubscriptionInfoVC : UITableViewController
- (id)initWithSubscription:(SenkoSub *)sub;
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
