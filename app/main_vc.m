#import "main_vc_priv.h"
#import "crash_report.h"

#import <fcntl.h>
#import <unistd.h>

/* the jailbreak build keeps the injected badge; clear the marker on launch
   in case an older build (or the user's own toggle) left it turned off */
static void SenkoDisableInjectedStatusBadge(void) {
    unlink(SENKO_VPN_BADGE_OFF_PATH);
}

@implementation MainVC

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [_boyField stop];
    [_boyField release];
    [_bubbleField stop];
    [_bubbleField release];
    [_misidePattern release];
    [_frutigerBg release];
    [_frutigerBgName release];
    [_ios26Bg release];
    _actionSheet.delegate = nil;
    [_actionSheet release];
    [_menuSubChoices release];
    [_ctl release];
    [_nativeVPN release];
    [_servers release];
    [_subs release];
    [_sectionOrder release];
    [_sections release];
    [_rowName release];
    [_collapsedSubs release];
    [_state release];
    [_lastErr release];
    [_lastAlertErr release];
    [_serverStatus release];
    [_pingingSubs release];
    [_pingQueue release];
    [_autoQueue release];
    [_autoResults release];
    [_pickerFilter release];
    [_pickerQuery release];
    [_chipKeys release];
    [_busyOverlay removeFromSuperview];
    [_busyOverlay release];
    [_pendingUpdatePath release];
    [_pendingInsecureURL release];
    [_sectionDragSnapshot removeFromSuperview];
    [_sectionDragSnapshot release];
    /* a drag that never ended still owns the header it was dragging */
    [_sectionDragHeader release];
    _sectionDragHeader = nil;
    [_uptimeTimer invalidate];
    _uptimeTimer = nil;
    [_statusTimer invalidate];
    _statusTimer = nil;
    [_sheet removeFromSuperview];
    [_sheet release];
    [NSObject cancelPreviousPerformRequestsWithTarget:_emptyState];
    [_emptyState removeFromSuperview];
    [_emptyState release];
    [_deviceHWID release];
    [_laidStatusKey release];
    [super dealloc];
}

- (void)styleTable {
    _table.backgroundColor = [UIColor clearColor];
    _table.separatorStyle = UITableViewCellSeparatorStyleNone;
    if ([_table respondsToSelector:@selector(setBackgroundView:)])
        _table.backgroundView = nil;
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    SenkoCrashTheme([SenkoThemeCurrentId() UTF8String]);
    self.view.backgroundColor = kBG;
    [self applyBackgroundForCurrentState:NO];
    SenkoThemeSfxPrepare();
    [self syncBoykisserField];
    [self syncMisideDecor];
    [self syncFrutigerDecor];
    [self syncIos26Decor];
    [self syncBubbleField];
    [self layoutWallpaperStack];
    [_sheet dismiss];
    [_home applyTheme];
    [_picker applyTheme];
    [self styleTable];
    [self applyState];
    [_table reloadData];
    [_emptyState applyTheme];
    [self.view setNeedsLayout];
    [self layoutMainChrome];
}

- (void)languageDidChange:(NSNotification *)n {
    (void)n;
    [_home relocalize];
    [_picker relocalize];
    [self rebuildSections];
    [_table reloadData];
    [self applyState];
}

- (void)loadView {
    UIView *v = [[[UIView alloc] initWithFrame:[[UIScreen mainScreen] bounds]] autorelease];
    v.backgroundColor = kBG;
    v.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.view = v;
    _bgGrad = AddVGradient(v, kBG, kBGBot);
    [self applyBackgroundForCurrentState:NO];
    if (SenkoThemeIsMiside())
        [self syncMisideDecor];
    if (SenkoThemeIsFrutigeraero())
        [self syncFrutigerDecor];
    if (SenkoThemeIsIos26())
        [self syncIos26Decor];
    [self layoutWallpaperStack];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    SenkoCrashScreen(_pickerShown ? "server list" : "home");
/* the retry budget is spent per appearance, not for the life of the process:
   a daemon that was still starting up when it ran out left the plate reading
   "not available yet" for the rest of the session even after the daemon came
   up, since nothing else ever asked again */
    if (![_deviceHWID length]) _hwidRetries = 0;
    [self requestDeviceHWID];
    [self applyState];
    [self syncUptimeTicker];
    [self syncBoykisserField];
    [self syncBubbleField];
    [_boyField setPaused:NO];
    [_bubbleField setPaused:NO];
    [_home->power setRunning:!_pickerShown];
    [self startStatusHeartbeat];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    /* the ticker holds a strong reference to this controller, so it must not
       outlive the screen being on top */
    [_uptimeTimer invalidate];
    _uptimeTimer = nil;
    [self stopStatusHeartbeat];
    [_sheet dismiss];
    [_boyField setPaused:YES];
    [_bubbleField setPaused:YES];
    [_home->power setRunning:NO];
    (void)animated;
}

- (void)layoutMainChrome {
    if (_layingOutChrome) return;
    _layingOutChrome = YES;
    CGRect b = self.view.bounds;
    if (!CGRectEqualToRect(_home.frame, b)) _home.frame = b;
    if (!CGRectEqualToRect(_picker.bounds, CGRectMake(0, 0, b.size.width, b.size.height))) {
        CGAffineTransform t = _picker.transform;
        _picker.transform = CGAffineTransformIdentity;
        _picker.frame = b;
        _picker.transform = t;
    }
    if (_boyField && !CGSizeEqualToSize(_boyField.bounds.size, b.size))
        _boyField.frame = b;
    if (_bubbleField && !CGSizeEqualToSize(_bubbleField.bounds.size, b.size))
        _bubbleField.frame = b;
    CGSize sz = b.size;
    NSString *key = [self backgroundStatusKey];
    BOOL sizeChanged = !CGSizeEqualToSize(sz, _laidChromeSize);
    BOOL statusChanged = !(_laidStatusKey && [key isEqualToString:_laidStatusKey]);
    if (sizeChanged || statusChanged) {
        _laidChromeSize = sz;
        [_laidStatusKey release];
        _laidStatusKey = [key copy];
        [self applyBackgroundForCurrentState:NO];
        if (SenkoThemeIsMiside())
            [self layoutMisideChrome];
        else if (SenkoThemeIsFrutigeraero())
            [self syncFrutigerDecor];
        else if (SenkoThemeIsIos26())
            [self syncIos26Decor];
        else
            [self layoutWallpaperStack];
    }
    [self syncEmptyState];
    [self layoutBusyOverlay];
    _layingOutChrome = NO;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutMainChrome];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io {
    (void)io;
    if (_boyField && SenkoThemeIsBoykisser())
        [self syncBoykisserField];
}

- (void)chromeButtonDown:(UIView *)v { SenkoPressPop(v, YES); }
- (void)chromeButtonUp:(UIView *)v { SenkoPressPop(v, NO); }

- (void)viewDidLoad {
    [super viewDidLoad];
    _ctl = [[SenkoControl alloc] initWithSocketPath:SENKO_SOCK];
    _nativeVPN = [[SenkoNativeVPN alloc] init];
    SenkoDisableInjectedStatusBadge();
    _selectedSrvIdx = -1;
    _menuSubIdx = -1;
    _subscriptionMutationBusy = NO;
    _catalogLoaded = NO;
    _selectedBackend = [[NSUserDefaults standardUserDefaults] integerForKey:SENKO_SELECTED_BACKEND_KEY];
    if (_selectedBackend != SenkoBackendAmneziaWG)
        _selectedBackend = SenkoBackendServer;
    _activeBackend = SenkoBackendNone;
    _state = [@"idle" copy];
    _serverStatus = [[NSMutableDictionary alloc] init];
    _pingingSubs = [[NSMutableSet alloc] init];
    _checkGeneration = 0;
    _catalogGeneration = 0;
    _sections = [[NSMutableArray alloc] init];
    _servers = [[NSMutableArray alloc] init];
    _subs = [[NSMutableArray alloc] init];
    _collapsedSubs = [[NSMutableSet alloc] init];
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:self selector:@selector(themeDidChange:)
               name:SenkoThemeDidChangeNotification object:nil];
    [nc addObserver:self selector:@selector(languageDidChange:)
               name:SenkoLanguageDidChangeNotification object:nil];
    /* core animation drops layer animations when the app is backgrounded, so
       the orbit has to be reinstalled on the way back. the notify centre passes
       the notification, which -applyState does not take */
    [nc addObserver:self selector:@selector(appDidBecomeActive:)
               name:UIApplicationDidBecomeActiveNotification object:nil];
    [nc addObserver:self selector:@selector(appWillResignActive:)
               name:UIApplicationWillResignActiveNotification object:nil];
    [nc addObserver:self selector:@selector(widgetToggleRequested:)
               name:SENKO_WIDGET_TOGGLE_NOTIFICATION object:nil];

    SenkoCrashTheme([SenkoThemeCurrentId() UTF8String]);

    _home = [[[SenkoHomeView alloc] initWithFrame:self.view.bounds] autorelease];
    [_home->gear addTarget:self action:@selector(settingsPressed)
          forControlEvents:UIControlEventTouchUpInside];
    [_home->stats addTarget:self action:@selector(statsPressed)
           forControlEvents:UIControlEventTouchUpInside];
    [_home->serverCard addTarget:self action:@selector(showServerPicker)
                forControlEvents:UIControlEventTouchUpInside];
    _connectBtn = _home->power;
    [_connectBtn addTarget:self action:@selector(togglePressed)
          forControlEvents:UIControlEventTouchUpInside];
    _connectBtn.accessibilityLabel = SenkoLocalizedText(@"Connect");
    /* the detail line is the label the rest of the controller writes progress
       into, so every SetStatusDefault caller keeps working unchanged */
    _statusLabel = _home->detail;
    SetStatusDefault(_statusLabel, @"");
    [self.view addSubview:_home];

    BOOL pad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
    _table = [[[UITableView alloc] initWithFrame:CGRectZero
                                           style:UITableViewStylePlain] autorelease];
    _table.dataSource = self;
    _table.delegate = self;
    [self styleTable];
    _table.rowHeight = pad ? 72.0f : 64.0f;
    _table.sectionHeaderHeight = 56.0f;
    _table.delaysContentTouches = NO;
    _table.canCancelContentTouches = YES;
    _table.showsVerticalScrollIndicator = YES;
    if ([_table respondsToSelector:@selector(setEstimatedRowHeight:)])
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(_table, @selector(setEstimatedRowHeight:), 0.0f);
    if ([_table respondsToSelector:@selector(setEstimatedSectionHeaderHeight:)])
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(_table, @selector(setEstimatedSectionHeaderHeight:), 0.0f);
    if ([_table respondsToSelector:@selector(setSectionHeaderTopPadding:)])
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(_table, @selector(setSectionHeaderTopPadding:), 0.0f);
    UILongPressGestureRecognizer *rowHold = [[[UILongPressGestureRecognizer alloc]
                                              initWithTarget:self action:@selector(rowLongPressed:)] autorelease];
    rowHold.minimumPressDuration = 0.5;
    [_table addGestureRecognizer:rowHold];

    _picker = [[[SenkoServerPicker alloc] initWithFrame:self.view.bounds
                                                  table:_table
                                               delegate:self] autorelease];
    _picker.hidden = YES;
    [self.view addSubview:_picker];

    _emptyState = [[SenkoEmptyStateView alloc] initWithFrame:CGRectZero];
    _emptyState.hidden = YES;
    [_emptyState->pasteButton addTarget:self action:@selector(emptyStatePastePressed)
                       forControlEvents:UIControlEventTouchUpInside];
    [_emptyState->scanButton addTarget:self action:@selector(emptyStateScanPressed)
                      forControlEvents:UIControlEventTouchUpInside];
    [_emptyState->hwidTap addTarget:self action:@selector(emptyStateCopyHWID)
                   forControlEvents:UIControlEventTouchUpInside];
    [_picker addSubview:_emptyState];

    if (SenkoThemeIsBoykisser())
        [self syncBoykisserField];
    if (SenkoThemeIsFrutigeraero())
        [self syncBubbleField];
    [self bringMainChromeToFront];
    [self applyState];
}

- (void)widgetToggleRequested:(NSNotification *)note {
    (void)note;
    [self togglePressed];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    /* the quick connect switch lives in settings and can change while they
       are up, and so can the sort order */
    [self rebuildSections];
    [self layoutMainChrome];
    [_table reloadData];
    [_table layoutIfNeeded];
    [self syncEmptyState];
    [self applyState];
    if (!_catalogLoaded)
        [self showBusyOverlay:SenkoLocalizedText(@"Loading servers and subscriptions...")];
    [self ensureDaemonThenRefresh];
}

- (void)ensureDaemonThenRefresh {
    /* probing during a live tunnel can overwrite its status with stale state */
    BOOL quiet = [self isTunnelActive];
    [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
        if (!up) {
            if (quiet) {
                [self refresh];
                return;
            }
            _catalogLoaded = YES;
            [self hideBusyOverlay];
            [self setLastErr:detail ? detail : @"daemon offline"];
            [_state release];
            _state = [@"error" copy];
            [self applyState];
            return;
        }
        /* an already-running daemon has no startup detail to show, and the
           label already holds whatever was correct before this screen
           appeared: overwriting it with the idle placeholder here flashed
           "idle" over a valid detail for the round trip refresh needs to
           confirm nothing changed */
        if (!quiet && detail && [detail length])
            SetStatusRefresh(_statusLabel, detail);
        [self refresh];
    }];
}


- (void)appDidBecomeActive:(NSNotification *)n {
    (void)n;
    [self applyState];
/* nothing polled the daemon while the app was away, so the screen was still
   showing the state the last user action left behind. the tunnel outlives the
   app, and coming back is the first chance to find out what it is doing */
    [self startStatusHeartbeat];
    [self ensureDaemonThenRefresh];
/* the daemon may have come up while the app was suspended, so give the hwid
   plate a fresh retry budget instead of leaving it on whatever ran out before
   backgrounding */
    if (![_deviceHWID length]) _hwidRetries = 0;
    [self requestDeviceHWID];
}

/* timers do not fire while the app is suspended, and one left scheduled fires
   immediately on the way back, before the daemon socket is reachable again */
- (void)appWillResignActive:(NSNotification *)n {
    (void)n;
    [self stopStatusHeartbeat];
}

- (void)presentInNavigation:(UIViewController *)root {
    [self dismissCurrentActionSheetAnimated:YES];
    UINavigationController *nav = [[[UINavigationController alloc]
                                    initWithRootViewController:root] autorelease];
    if ([nav respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(nav, @selector(setEdgesForExtendedLayout:), 0);
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    [self presentViewController:nav animated:YES completion:nil];
}

- (void)settingsPressed {
    [self presentInNavigation:[[[SettingsVC alloc] init] autorelease]];
}

- (void)statsPressed {
    [self presentInNavigation:[[[StatsVC alloc] init] autorelease]];
}

- (void)showServerPicker {
    if (_pickerShown) return;
    [self dismissCurrentActionSheetAnimated:YES];
    _pickerShown = YES;
    SenkoCrashScreen("server list");
    [self rebuildSections];
    [_table reloadData];
    [_table setContentOffset:CGPointZero animated:NO];
    _picker.transform = CGAffineTransformIdentity;
    _picker.frame = self.view.bounds;
    _picker.hidden = NO;
    [self bringMainChromeToFront];
    [self syncEmptyState];
    CGFloat lift = floorf(self.view.bounds.size.height * 0.08f);
    _picker.alpha = 0.0f;
    _picker.transform = CGAffineTransformMakeTranslation(0.0f, lift);
    [_home->power setRunning:NO];
    SenkoAnimate(0.26, ^{
        _picker.alpha = 1.0f;
        _picker.transform = CGAffineTransformIdentity;
        _home.alpha = 0.0f;
    }, NULL);
}

- (void)hideServerPicker {
    if (!_pickerShown) return;
    _pickerShown = NO;
    SenkoCrashScreen("home");
    [_sheet dismiss];
    [_picker clearQuery];
    if (_table.editing) [_table setEditing:NO animated:NO];
    if ([_pickerQuery length]) {
        [_pickerQuery release];
        _pickerQuery = nil;
        [self rebuildSections];
        [_table reloadData];
    }
    [_home->power setRunning:YES];
    CGFloat lift = floorf(self.view.bounds.size.height * 0.08f);
    SenkoAnimate(0.22, ^{
        _picker.alpha = 0.0f;
        _picker.transform = CGAffineTransformMakeTranslation(0.0f, lift);
        _home.alpha = 1.0f;
    }, ^(BOOL finished) {
        (void)finished;
/* a reopen during the fade owns the picker now */
        if (_pickerShown) return;
        _picker.hidden = YES;
        _picker.transform = CGAffineTransformIdentity;
    });
}

@end
