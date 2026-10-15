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
#include <limits.h>
#include <objc/message.h>
#import "control_client.h"
#import "native_vpn.h"
#import "qr_scan.h"
#import "ui_theme.h"
#import "boykisser_field.h"
#import "bubble_field.h"
#import "themes_vc.h"
#import "server_cell.h"
#import "home_view.h"
#import "server_picker.h"
#import "server_sheet.h"
#import "update_install.h"
#import "meow.h"
#import "text_prompt.h"
#import "empty_state.h"
#import "app_common.h"

@interface MainVC () <UITableViewDataSource, UITableViewDelegate,
                      UIActionSheetDelegate, UIAlertViewDelegate,
                      QRScanDelegate, EditSubscriptionDelegate,
                      FileImportDelegate, EditAWGDelegate,
                      EditServerDelegate, SenkoServerSheetDelegate,
                      SenkoServerPickerDelegate> {
    SenkoControl *_ctl;
    SenkoNativeVPN *_nativeVPN;
    /* both live in the view hierarchy, which owns them */
    SenkoHomeView *_home;
    SenkoServerPicker *_picker;
    BOOL          _pickerShown;
    /* the group chip and the search text narrow what the list shows. nil is
       every group, -1 is Manual, -2 the amneziawg profile alone */
    NSNumber     *_pickerFilter;
    NSString     *_pickerQuery;
    NSArray      *_chipKeys;
    BOOL          _awgRowShown;
    UIControl    *_connectBtn;
    SenkoServerSheet *_sheet;
    UILabel      *_statusLabel;
    UITableView  *_table;
    BOOL          _pinnedSyncQueued;
    NSMutableArray *_servers;
    NSMutableArray *_subs;
    NSMutableArray *_sectionOrder;
    NSMutableArray *_sections;
    /* what each row actually shows: the remark with the section's shared banner
       removed, counted when two nodes still land on the same name */
    NSMutableDictionary *_rowName;
    NSMutableSet *_collapsedSubs;
    NSString     *_state;
    NSString     *_lastErr;
    NSString     *_lastAlertErr;
    int           _selectedSrvIdx;
    int           _menuSubIdx;
    BOOL          _busy;
    BOOL          _subscriptionMutationBusy;
    BOOL          _isRefreshingCatalog;
    BOOL          _catalogLoaded;
/* a dim veil with a spinner over the whole screen while a subscription
   refresh or a ping sweep is in flight, so the wait reads as "working" and
   not as a frozen list */
    UIView       *_busyOverlay;
    CAGradientLayer *_bgGrad;
    /* quick connect probes the catalog on its own queue, so a list refresh or a
       manual ping sweep never cancels the probe a pending connect waits on */
    NSArray      *_autoQueue;
    NSMutableArray *_autoResults;
    NSUInteger    _autoNext;
    NSUInteger    _autoPending;
    NSUInteger    _autoDone;
    NSUInteger    _autoGeneration;
    /* the server backend reports age; amneziawg starts from the first connected
       reply, and both tick from this monotonic instant between polls */
    long          _tunnelUptime;
    CFTimeInterval _tunnelUptimeAt;
    /* whether the two above describe the tunnel now on screen. without it a
       zero age and "no age reported yet" are the same value */
    BOOL          _tunnelUptimeKnown;
    NSTimer      *_uptimeTimer;
    /* the daemon owns the tunnel, and nothing else asks it what happened: a
       tunnel that came up, dropped or was switched outside this screen left the
       card showing whatever the last user action had put there until the app
       was launched again. this ticks while the screen is on top */
    NSTimer      *_statusTimer;
    /* each poll has two replies. a slow older poll must not repaint state after
       a newer one has already described the daemon */
    NSUInteger    _tunnelStateGeneration;
    SenkoBoykisserField *_boyField;
    SenkoBubbleField  *_bubbleField;
    UIImageView       *_misidePattern;
    UIImageView       *_frutigerBg;
    NSString          *_frutigerBgName; /* the photo on screen, to tell a swap */
    UIImageView       *_ios26Bg;
    BOOL               _ios26BgLight;
    NSMutableDictionary *_serverStatus;
    NSInteger     _checkGeneration;
    NSInteger     _catalogGeneration;
    CGSize        _laidChromeSize;
    NSString     *_laidStatusKey;
    NSMutableSet  *_pingingSubs;
    NSArray       *_pingQueue;
    NSUInteger     _pingNext;
    NSUInteger     _pingPending;
    NSUInteger     _pingCompleted;
    int            _pingSubIndex;
    UIActionSheet *_actionSheet;
    NSArray       *_menuSubChoices;
    NSString      *_pendingUpdatePath;
    NSString      *_pendingInsecureURL;
    int            _dragSection;
    int            _sectionDragOrigin;
    BOOL           _sectionDragSending;
    UIView        *_sectionDragSnapshot;
    UIView        *_sectionDragHeader;
    CGFloat        _sectionDragGrabOffset;
    BOOL           _sectionDragActive;
    SenkoBackendKind _selectedBackend;
    SenkoBackendKind _activeBackend;
    SenkoEmptyStateView *_emptyState;
    NSString      *_deviceHWID;
    int            _hwidRetries;
    BOOL           _layingOutChrome;
}

- (void)dealloc;
- (void)themeDidChange:(NSNotification *)n;
- (void)loadView;
- (void)viewDidAppear:(BOOL)animated;
- (void)viewWillDisappear:(BOOL)animated;
- (void)layoutMainChrome;
- (NSString *)stateHeadline;
- (void)showServerPicker;
- (void)hideServerPicker;
/* chips, the auto row and the home server card follow the catalog */
- (void)syncPickerChrome;
- (void)syncHomeServerCard;
- (void)statsPressed;
- (void)openSheetForServer:(SenkoServer *)server;
- (void)serverSheet:(SenkoServerSheet *)sheet
    didChooseAction:(NSString *)action
        serverIndex:(int)index;
- (void)viewDidLayoutSubviews;
- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io;
- (void)viewDidLoad;
- (void)viewWillAppear:(BOOL)animated;
- (void)ensureDaemonThenRefresh;
- (void)refreshNativeCatalog;
- (void)appDidBecomeActive:(NSNotification *)n;
- (void)settingsPressed;
- (void)bringMainChromeToFront;
- (void)layoutWallpaperStack;
- (void)layoutMisideChrome;
- (void)syncMisideDecor;
- (void)syncBoykisserField;
- (void)syncFrutigerDecor;
- (void)syncIos26Decor;
- (void)syncBubbleField;
- (NSString *)backgroundStatusKey;
- (void)applyBackgroundForCurrentState:(BOOL)animated;
- (void)addPressed;
- (void)dismissCurrentActionSheetAnimated:(BOOL)animated;
- (void)actionSheet:(UIActionSheet *)sheet didDismissWithButtonIndex:(NSInteger)idx;
- (SenkoSub *)subscriptionByIndex:(int)subIdx;
- (void)editSubscriptionIndex:(int)subIdx;
- (void)pinSubscriptionIndex:(int)subIdx;
- (void)editSubscriptionVC:(EditSubscriptionVC *)vc saveSubWithIndex:(int)idx name:(NSString *)name url:(NSString *)url header:(NSString *)header;
- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)idx;
- (BOOL)isNativeAWGText:(NSString *)s;
- (BOOL)looksLikeAmneziaBundle:(NSData *)body;
- (BOOL)importAmneziaBundleData:(NSData *)body;
- (void)importAWGText:(NSString *)text;
- (void)importText:(NSString *)s;
- (void)importContentData:(NSData *)data;
- (void)addNativeServers:(NSArray *)servers successText:(NSString *)successText;
- (void)importFileAtPath:(NSString *)path;
- (void)addSubscriptionURL:(NSString *)url name:(NSString *)name;
- (int)trailingIntOf:(NSString *)reply;
- (NSString *)nameFromURL:(NSString *)url;
- (BOOL)promptTextWithTitle:(NSString *)title
                    message:(NSString *)message
                   keyboard:(UIKeyboardType)keyboard
                    handler:(void (^)(NSString *text))handler;
- (void)promptSubscription;
- (void)pasteFromClipboard;
- (void)confirmInsecureSubscriptionURL:(NSString *)url;
- (void)promptImportFile;
- (void)fileImportVCDidCancel:(FileImportVC *)vc;
- (void)fileImportVC:(FileImportVC *)vc didPickPath:(NSString *)path;
- (void)presentUpdateForPath:(NSString *)path;
- (void)alertView:(UIAlertView *)av clickedButtonAtIndex:(NSInteger)idx;
- (void)openScanner;
- (void)qrScanner:(QRScanVC *)s didDecode:(NSString *)text;
- (void)qrScannerDidCancel:(QRScanVC *)s;
- (void)applyCatalog:(NSArray *)servers subs:(NSArray *)subs order:(NSArray *)order;
- (void)rebuildSections;
- (void)syncEmptyState;
/* where the empty panel may be drawn without covering what the list has already
   put on screen. CGRectZero when there is no room for it at all */
- (CGRect)emptyStateFrame;
- (void)emptyStatePastePressed;
- (void)emptyStateScanPressed;
- (void)emptyStateCopyHWID;
- (void)requestDeviceHWID;
- (NSArray *)collapsedRows:(NSArray *)rows names:(NSMutableDictionary *)rowNames;
- (NSArray *)sortedRows:(NSArray *)rows;
- (NSNumber *)bestPingForServer:(SenkoServer *)sv;
- (NSArray *)connectCandidatesForServerIndex:(int)index;
- (SenkoServer *)serverAtIndexPath:(NSIndexPath *)ip;
- (void)refresh;
/* the status half of -refresh on its own: the catalog is expensive to list and
   does not change on its own, the tunnel state does */
- (void)refreshTunnelState;
- (void)refreshTunnelStateRedrawing:(BOOL)always;
- (void)nativeStatusWithReply:(void (^)(NSString *state, long uptime))done;
- (void)startStatusHeartbeat;
- (void)stopStatusHeartbeat;
- (void)setToggleBusy:(BOOL)busy;
- (void)syncSelectionFromDaemon;
- (void)reconcileSelectionAfterListKeeping:(SenkoServer *)anchor;
- (BOOL)isTunnelActive;
- (BOOL)isServerSelectionLocked;
- (BOOL)isListMutationLocked;
- (void)applyServerListLock;
- (void)setLastErr:(NSString *)msg;
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv;
- (void)sectionToggleTapped:(UIButton *)button;
- (void)moveSectionAtIndex:(NSInteger)from toIndex:(NSInteger)to;
- (void)sectionLongPressed:(UILongPressGestureRecognizer *)gesture;
- (void)rowLongPressed:(UILongPressGestureRecognizer *)gesture;
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
- (void)selectServerIndex:(int)index;
- (SenkoServer *)serverByIndex:(int)index;
- (NSString *)homeDetailText;
- (void)showSortMenu;
- (void)sortMenuPicked:(NSInteger)index;
- (void)applyPickerFilterTo:(NSMutableArray *)secs;
- (void)reloadPickerList;
- (void)syncUptimeTicker;
- (void)editServerVC:(EditServerVC *)vc saveLink:(NSString *)link index:(int)idx;
- (BOOL)tableView:(UITableView *)tv canEditRowAtIndexPath:(NSIndexPath *)ip;
- (UITableViewCellEditingStyle)tableView:(UITableView *)tv editingStyleForRowAtIndexPath:(NSIndexPath *)ip;
- (void)tableView:(UITableView *)tv commitEditingStyle:(UITableViewCellEditingStyle)style forRowAtIndexPath:(NSIndexPath *)ip;
- (BOOL)tableView:(UITableView *)tv canMoveRowAtIndexPath:(NSIndexPath *)ip;
- (void)tableView:(UITableView *)tv moveRowAtIndexPath:(NSIndexPath *)from
      toIndexPath:(NSIndexPath *)to;
- (void)subRefreshTapped:(UIButton *)btn;
- (void)subPingTapped:(UIButton *)btn;
- (void)subMenuTapped:(UIButton *)btn;
- (void)awgRefreshTapped:(UIButton *)btn;
- (void)awgPingTapped:(UIButton *)btn;
- (void)refreshSubscriptionIndex:(int)pos failures:(NSMutableArray *)failures;
- (void)refreshPressed;
- (void)showBusyOverlay:(NSString *)text;
- (void)hideBusyOverlay;
- (void)layoutBusyOverlay;
- (void)pingPressed;
- (void)serverPingTapped:(UIButton *)button;
- (void)startPingSweep;
- (void)updateSubscriptionPingButtons:(NSSet *)subIndexes;
- (void)reloadServerRowForIndex:(int)serverIndex;
- (void)pingServersInSub:(int)subIdx;
- (void)beginBoundedPing:(NSArray *)idxs subIndex:(int)subIdx generation:(NSInteger)gen;
- (void)launchBoundedPingsForGeneration:(NSInteger)gen;
- (void)finishBoundedPing;
- (void)startStatusChecks;
- (void)applyState;
- (void)forceTunnelCleanupWithReason:(NSString *)reason;
- (void)togglePressed;
- (void)toggleAfterAWGCheck;
- (void)connectSelectedServer;
- (void)connectFastestServer;
- (NSArray *)autoProbeCandidates;
- (void)launchAutoProbes:(NSUInteger)generation;
- (void)finishAutoProbes;
- (SenkoSub *)subscriptionForServer:(SenkoServer *)server;
- (NSString *)sourceNameForServer:(SenkoServer *)server;
- (NSString *)nameForServer:(SenkoServer *)server;
- (void)chooseServerIndex:(int)index;
- (void)switchActiveServerIndex:(int)idx;
- (void)connectTryingCandidates:(NSArray *)candidates offset:(NSUInteger)offset
                           reply:(void (^)(NSString *reply))replyBlock;
- (void)editAWGProfile;
- (void)editAWGVC:(EditAWGVC *)vc saveConfig:(NSString *)config;
- (void)startSavedAWGProfile;
- (void)removeSavedAWGProfile;
- (BOOL)hasManualServers;
- (void)showManualMenu;
- (void)confirmClearManual;
- (void)clearManualServers;

BOOL SenkoServerIdentityEqual(SenkoServer *a, SenkoServer *b);

@end

#endif
