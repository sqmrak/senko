#ifndef SENKO_APP_COMMON_H
#define SENKO_APP_COMMON_H

#import <Foundation/Foundation.h>
#import "ios_compat.h"
#import "control_client.h"

#define SENKO_SOCK @"/var/tmp/senkod.sock"
#define SENKO_VERSION @"v2.1.0-stable"
#define SENKO_CLASSIC_HOME_KEY @"SenkoClassicHome"
#define SENKO_PINNED_SUB_URL_KEY @"SenkoPinnedSubscriptionURL"
#define SENKO_AWG_PROFILE_KEY @"SenkoAWGProfilePath"
#define SENKO_AWG_PROFILE_PATH @"/var/mobile/Library/Preferences/Senko/amneziawg.conf"
#define SENKO_SELECTED_BACKEND_KEY @"SenkoSelectedBackend"
#define SENKO_WIDGET_TOGGLE_NOTIFICATION @"SenkoWidgetToggleNotification"
/* springboard reads this marker instead of the app defaults, which live in a
   domain it cannot see. present means the injected status bar badge stays off */
#define SENKO_VPN_BADGE_OFF_PATH "/var/mobile/Library/Preferences/com.senko.status.off"
#define SENKO_LANGUAGE_KEY @"SenkoLanguage"
/* 0 keeps the order the daemon stores, 1 sorts by name, 2 by latency */
#define SENKO_SERVER_SORT_KEY @"SenkoServerSort"
/* the developer section is a property of this device, not of the profile, so it
   lives in the app defaults and stays out of the config backup */
#define SENKO_DEV_MENU_KEY @"SenkoDeveloperMenu"

typedef enum {
    SenkoSortManual = 0,
    SenkoSortName,
    SenkoSortPing
} SenkoServerSort;

typedef NS_ENUM(NSInteger, SenkoLanguage) {
    SenkoLanguageEnglish = 0,
    SenkoLanguageRussian = 1,
    SenkoLanguageChinese = 2
};

typedef NS_ENUM(NSInteger, SenkoBackendKind) {
    SenkoBackendServer = 0,
    SenkoBackendAmneziaWG = 1,
    SenkoBackendNone = 2
};

/* the frame counter overlay. live glass costs real frames on an iphone 4, and
   "it feels slow" is not something a theme change can be judged against */
#define SENKO_FPS_OVERLAY_KEY @"SenkoDeveloperFPSOverlay"

/* the developer section: five taps on the version line in About turn it on,
   five taps on its own header turn it off again */
BOOL SenkoDevMenuEnabled(void);
void SenkoSetDevMenuEnabled(BOOL enabled);
/* count a tap and answer how many are still needed, 0 once it fired. the
   counter is shared so the About line and the settings header behave alike */
int SenkoDevMenuTapsLeft(void);
void SenkoDevMenuResetTaps(void);

/* first and last four characters of a secret, for a screen whose whole point is
   being photographed and sent to someone. a value too short to keep any middle
   is replaced outright rather than half shown */
NSString *SenkoMaskSecret(NSString *value);

/* the frame counter. it draws in its own window above everything, so it
   survives a screen push and does not change any layout it measures */
BOOL SenkoFPSOverlayEnabled(void);
void SenkoFPSOverlaySetEnabled(BOOL enabled);

NSString *SenkoAboutAppReport(void);
/* one place for the shop address, so the about screen can print it, copy it and
   open it without three copies of the same string drifting apart */
NSString *SenkoSponsorURL(void);
/* the home screen before the list was rebuilt: a dome connect button with the
   check and status pills under it, instead of the status card. everything below
   the hero area is the current build, so the list keeps its sections, its empty
   state and the server sheet */
BOOL SenkoClassicHomeEnabled(void);
void SenkoSetClassicHomeEnabled(BOOL enabled);

/* the device id the daemon actually sends, read from the file both processes
   share. nil until the daemon has written one */
NSString *SenkoSharedDeviceHWID(void);
extern NSString * const SenkoLanguageDidChangeNotification;
BOOL SenkoLanguageIsRussian(void);
BOOL SenkoLanguageIsChinese(void);
void SenkoSetLanguage(SenkoLanguage language);
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
