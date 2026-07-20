#ifndef SENKO_MAIN_VC_PRIV_H
#define SENKO_MAIN_VC_PRIV_H

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CFNetwork/CFNetwork.h>
#import <ifaddrs.h>
#import <arpa/inet.h>
#import <dlfcn.h>
#import <unistd.h>
#include <math.h>
#include <objc/message.h>
#import "control_client.h"
#import "qr_scan.h"
#import "ui_theme.h"
#import "boykisser_field.h"
#import "bubble_field.h"
#import "themes_vc.h"
#import "server_cell.h"
#import "main_layout.h"
#import "update_install.h"
#import "meow.h"
#import "app_common.h"

@interface MainVC () <UITableViewDataSource, UITableViewDelegate,
                      UIActionSheetDelegate, UIAlertViewDelegate,
                      QRScanDelegate, EditSubscriptionDelegate,
                      FileImportDelegate, EditAWGDelegate> {
    SenkoControl *_ctl;
    UIButton     *_connectBtn;
    UIButton     *_pingAllBtn;
    UILabel      *_statusLabel;
    UITableView  *_table;
    NSMutableArray *_servers;
    NSMutableArray *_subs;
    NSMutableArray *_sections;
    NSMutableSet *_collapsedSubs;
    NSString     *_state;
    NSString     *_lastErr;
    int           _selectedSrvIdx;
    int           _menuSubIdx;
    BOOL          _busy;
    CAGradientLayer *_bgGrad;
    UIView            *_statusWashHost;
    CALayer           *_statusWash;
    CAGradientLayer *_btnBody;
    SenkoBoykisserField *_boyField;
    SenkoBubbleField  *_bubbleField;
    UIImageView       *_misidePattern;
    UIImageView       *_misideLogo;
    UIImageView       *_frutigerBg;
    UIImageView       *_ios26Bg;
    BOOL               _ios26BgLight;
    NSMutableDictionary *_serverStatus;
    NSInteger     _checkGeneration;
    NSInteger     _catalogGeneration;
    CGFloat       _listHeaderProgress;
    BOOL          _headerSnapAnimating;
    CGSize        _laidChromeSize;
    NSString     *_laidStatusKey;
    UIActionSheet *_actionSheet;
    NSString      *_pendingUpdatePath;
    SenkoBackendKind _selectedBackend;
    SenkoBackendKind _activeBackend;
}

- (void)dealloc;
- (void)themeDidChange:(NSNotification *)n;
- (void)styleListWell;
- (void)loadView;
- (void)viewDidAppear:(BOOL)animated;
- (void)viewWillDisappear:(BOOL)animated;
- (void)layoutMainChromeGeometry;
- (void)layoutMainChrome;
- (void)viewDidLayoutSubviews;
- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)io duration:(NSTimeInterval)duration;
- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io;
- (void)viewDidLoad;
- (void)viewWillAppear:(BOOL)animated;
- (void)ensureDaemonThenRefresh;
- (void)settingsPressed;
- (void)bringMainChromeToFront;
- (void)layoutWallpaperStack;
- (void)layoutStatusGlow;
- (void)ensureStatusWash;
- (void)layoutMisideChrome;
- (void)syncMisideDecor;
- (void)syncBoykisserField;
- (void)syncFrutigerDecor;
- (void)syncIos26Decor;
- (void)syncBubbleField;
- (NSString *)backgroundStatusKey;
- (void)applyBackgroundForCurrentState:(BOOL)animated;
- (void)addPressed;
- (void)actionSheet:(UIActionSheet *)sheet didDismissWithButtonIndex:(NSInteger)idx;
- (SenkoSub *)subscriptionByIndex:(int)subIdx;
- (void)editSubscriptionIndex:(int)subIdx;
- (void)pinSubscriptionIndex:(int)subIdx;
- (void)editSubscriptionVC:(EditSubscriptionVC *)vc saveSubWithIndex:(int)idx name:(NSString *)name url:(NSString *)url;
- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)idx;
- (BOOL)isNativeAWGText:(NSString *)s;
- (void)importAWGText:(NSString *)text;
- (void)importText:(NSString *)s;
- (void)importFileAtPath:(NSString *)path;
- (void)addSubscriptionURL:(NSString *)url name:(NSString *)name;
- (int)trailingIntOf:(NSString *)reply;
- (NSString *)nameFromURL:(NSString *)url;
- (void)promptManualLink;
- (void)promptSubscription;
- (void)promptImportFile;
- (void)fileImportVCDidCancel:(FileImportVC *)vc;
- (void)fileImportVC:(FileImportVC *)vc didPickPath:(NSString *)path;
- (void)presentUpdateForPath:(NSString *)path;
- (void)alertView:(UIAlertView *)av clickedButtonAtIndex:(NSInteger)idx;
- (void)openScanner;
- (void)qrScanner:(QRScanVC *)s didDecode:(NSString *)text;
- (void)qrScannerDidCancel:(QRScanVC *)s;
- (void)applyCatalog:(NSArray *)servers subs:(NSArray *)subs;
- (void)rebuildSections;
- (void)setListHeaderProgress:(CGFloat)progress animated:(BOOL)animated;
- (SenkoServer *)serverAtIndexPath:(NSIndexPath *)ip;
- (void)refresh;
- (void)setToggleBusy:(BOOL)busy;
- (void)syncSelectionFromDaemon;
- (void)reconcileSelectionAfterListKeeping:(SenkoServer *)anchor;
- (BOOL)isTunnelActive;
- (BOOL)isServerSelectionLocked;
- (void)applyServerListLock;
- (void)setLastErr:(NSString *)msg;
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv;
- (void)scrollViewDidScroll:(UIScrollView *)scrollView;
- (void)snapListHeader;
- (void)scrollViewDidEndDragging:(UIScrollView *)scrollView willDecelerate:(BOOL)decelerate;
- (void)scrollViewDidEndDecelerating:(UIScrollView *)scrollView;
- (void)sectionToggleTapped:(UIButton *)button;
- (NSString *)awgProfilePath;
- (BOOL)hasAWGProfile;
- (BOOL)isManualSection:(NSInteger)section;
- (NSInteger)awgRowOffsetInSection:(NSInteger)section;
- (BOOL)isAWGRowAtIndexPath:(NSIndexPath *)ip;
- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s;
- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip;
- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s;
- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s;
- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip;
- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip;
- (BOOL)tableView:(UITableView *)tv canEditRowAtIndexPath:(NSIndexPath *)ip;
- (UITableViewCellEditingStyle)tableView:(UITableView *)tv editingStyleForRowAtIndexPath:(NSIndexPath *)ip;
- (void)tableView:(UITableView *)tv commitEditingStyle:(UITableViewCellEditingStyle)style forRowAtIndexPath:(NSIndexPath *)ip;
- (void)subRefreshTapped:(UIButton *)btn;
- (void)subPingTapped:(UIButton *)btn;
- (void)subMenuTapped:(UIButton *)btn;
- (void)awgRefreshTapped:(UIButton *)btn;
- (void)awgPingTapped:(UIButton *)btn;
- (void)refreshSubscriptionIndex:(int)pos;
- (void)refreshPressed;
- (void)pingPressed;
- (void)startPingSweep;
- (void)reloadServerRowForIndex:(int)serverIndex;
- (void)checkStatusOfServerAtIndex:(NSUInteger)idx generation:(NSInteger)gen;
- (void)pingServersInSub:(int)subIdx;
- (void)pingIndexList:(NSArray *)idxs at:(NSUInteger)i generation:(NSInteger)gen;
- (void)startStatusChecks;
- (void)applyState;
- (void)forceTunnelCleanupWithReason:(NSString *)reason;
- (void)togglePressed;
- (void)toggleAfterAWGCheck;
- (void)editAWGProfile;
- (void)editAWGVC:(EditAWGVC *)vc saveConfig:(NSString *)config;
- (void)startSavedAWGProfile;
- (void)removeSavedAWGProfile;
- (void)showAWGMenu;

BOOL SenkoServerIdentityEqual(SenkoServer *a, SenkoServer *b);

@end

#endif
