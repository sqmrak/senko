#import "main_vc_priv.h"
#import "crash_report.h"

#import <fcntl.h>

/* the injected status bar badge is retired: it never earned its keep against
   an outright wrong build gate (it ran on the jailbreak build, which has no
   other vpn indicator, and skipped the stock build, which actually needed
   it). writing the marker senkostatus already understands turns it off for
   both without touching the substrate hook itself */
static void SenkoDisableInjectedStatusBadge(void) {
    int fd = open(SENKO_VPN_BADGE_OFF_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    (void)write(fd, "removed\n", 8);
    close(fd);
}

@implementation MainVC

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [_boyField stop];
    [_boyField release];
    [_bubbleField stop];
    [_bubbleField release];
    [_statusWash release];
    _statusWash = nil;
    [_statusWashHost release];
    _statusWashHost = nil;
    [_misidePattern release];
    [_frutigerBg release];
    [_ios26Bg release];
    _actionSheet.delegate = nil;
    [_actionSheet release];
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
    [_revealedRows release];
    [super dealloc];
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
    [self styleHeaderTitle:_ui.title];
    [_sheet dismiss];
    SenkoHomeStyleChrome(&_ui);
    if (_statusLabel) {
        _statusLabel.backgroundColor = [UIColor clearColor];
        _statusLabel.layer.borderWidth = 0;
        _statusLabel.layer.cornerRadius = 0;
        [self applyState];
    }
    [self styleListWell];
    if (_table) {
        _table.backgroundColor = [UIColor clearColor];
        _table.separatorStyle = UITableViewCellSeparatorStyleNone;
        _table.separatorColor = SenkoThemeIsLight()
            ? [UIColor colorWithWhite:0 alpha:0.12f]
            : [UIColor colorWithWhite:1 alpha:0.16f];
        if ([_table respondsToSelector:@selector(setBackgroundView:)])
            _table.backgroundView = nil;
    }
    [_table reloadData];
    [_emptyState applyTheme];
    [self.view setNeedsLayout];
    [self layoutMainChrome];
}

- (void)styleListWell {
    UIView *well = [self.view viewWithTag:SenkoHomeTagWell];
    if (!well) return;
    if (SenkoThemeIsIos16()) {
        SenkoStyleIos16ListWell(well);
        return;
    }
    SenkoRemoveFrost(well);
    well.backgroundColor = [UIColor clearColor];
    well.layer.borderWidth = 0;
    well.layer.borderColor = [UIColor clearColor].CGColor;
    well.layer.shadowOpacity = 0;
    for (CALayer *layer in well.layer.sublayers) {
        if (![layer.name isEqualToString:@"wellGrad"] ||
            ![layer isKindOfClass:[CAGradientLayer class]])
            continue;
        layer.hidden = NO;
        CAGradientLayer *wg = (CAGradientLayer *)layer;
        if (SenkoThemeIsBoykisser()) {
/* the veil lifts the list off the wallpaper, so it follows the ground it sits
   on instead of washing a dark screen white */
            UIColor *veil = SenkoThemeIsLight()
                ? [UIColor colorWithRed:1.00 green:0.94 blue:0.97 alpha:0.35]
                : [UIColor colorWithRed:0.09 green:0.05 blue:0.075 alpha:0.32];
            wg.colors = [NSArray arrayWithObjects:(id)veil.CGColor, (id)veil.CGColor, nil];
        } else if (SenkoThemeIsFrutigeraero()) {
            UIColor *veil = [UIColor colorWithWhite:1 alpha:0.28];
            wg.colors = [NSArray arrayWithObjects:(id)veil.CGColor, (id)veil.CGColor, nil];
        } else if (SenkoThemeIsIos26()) {
            wg.colors = [NSArray arrayWithObjects:
                         (id)[UIColor clearColor].CGColor,
                         (id)[UIColor clearColor].CGColor, nil];
            wg.hidden = YES;
        } else if (SenkoThemeIsMiside()) {
            wg.colors = [NSArray arrayWithObjects:
                         (id)[UIColor clearColor].CGColor,
                         (id)[UIColor clearColor].CGColor, nil];
            wg.hidden = YES;
        } else {
            wg.colors = [NSArray arrayWithObjects:(id)kBG.CGColor, (id)kBGBot.CGColor, nil];
        }
    }
}


/* the wordmark reads from the leading edge in a geometric bold; each theme only
   picks the size and the ink */
- (void)styleHeaderTitle:(UILabel *)title {
    if (![title isKindOfClass:[UILabel class]]) return;
    BOOL pad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
    BOOL compact = (!pad && SenkoViewBounds(self.view).size.height <= 568.0f);
    CGFloat size = pad ? 28.0f : (compact ? 22.0f : 25.0f);
/* the classic header centres the wordmark, and this runs again on every theme
   change, so it has to agree with the layout rather than reset it */
    title.textAlignment = SenkoClassicHomeEnabled() ? NSTextAlignmentCenter
                                                    : NSTextAlignmentLeft;
    title.backgroundColor = [UIColor clearColor];
    title.shadowColor = nil;
    title.shadowOffset = CGSizeZero;
    title.font = SenkoFontDisplay(size);
    title.textColor = kInk;
}

- (UIButton *)makeHeaderButton:(SEL)action tag:(NSInteger)tag {
    UIButton *b = [UIButton buttonWithType:UIButtonTypeCustom];
    b.tag = tag;
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    [b addTarget:self action:@selector(chromeButtonDown:)
        forControlEvents:UIControlEventTouchDown];
    [b addTarget:self action:@selector(chromeButtonUp:)
        forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                         UIControlEventTouchCancel];
    return b;
}

- (void)chromeButtonDown:(UIView *)v { SenkoPressPop(v, YES); }
- (void)chromeButtonUp:(UIView *)v { SenkoPressPop(v, NO); }

- (void)loadView {
    UIView *v = [[[UIView alloc] initWithFrame:[[UIScreen mainScreen] bounds]] autorelease];
    v.backgroundColor = kBG;
    v.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.view = v;
    _bgGrad = AddVGradient(v, kBG, kBGBot);
    [self ensureStatusWash];
    [self applyBackgroundForCurrentState:NO];
    if (SenkoThemeIsBoykisser())
        [self syncBoykisserField];
    if (SenkoThemeIsMiside())
        [self syncMisideDecor];
    if (SenkoThemeIsFrutigeraero()) {
        [self syncFrutigerDecor];
        [self syncBubbleField];
    }
    if (SenkoThemeIsIos26())
        [self syncIos26Decor];
    [self layoutWallpaperStack];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    SenkoCrashScreen("server list");
/* the id used to be asked for only while the empty list was on screen, so a
   catalog that never arrived meant it was never asked for at all */
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
    (void)animated;
}

- (void)layoutMainChromeGeometry {
    /* the whole screen changes shape at once during a rotation, and letting
       uikit interpolate each piece from its old frame is what made the card,
       the pill and the rows slide past each other on the way round */
    BOOL animating = [UIView areAnimationsEnabled];
    if (_rotating && animating) [UIView setAnimationsEnabled:NO];
    if (SenkoClassicHomeEnabled())
        SenkoHomeLayoutClassic(self.view, &_ui, _listHeaderProgress);
    else
        SenkoHomeLayout(self.view, &_ui, _listHeaderProgress);
    [self layoutStatusGlow];
    /* a narrow orientation can leave no room below the list and hide this
       panel. always resync it here, otherwise returning to a larger layout
       leaves the empty catalog blank until a later catalog refresh. */
    [self syncEmptyState];
    if (_boyField && !CGSizeEqualToSize(_boyField.bounds.size, self.view.bounds.size))
        _boyField.frame = self.view.bounds;
    if (_bubbleField && !CGSizeEqualToSize(_bubbleField.bounds.size, self.view.bounds.size))
        _bubbleField.frame = self.view.bounds;
    if (_frutigerBg && !_frutigerBg.hidden)
        _frutigerBg.frame = self.view.bounds;
    if (_ios26Bg && !_ios26Bg.hidden)
        _ios26Bg.frame = self.view.bounds;
    if (_rotating && animating) [UIView setAnimationsEnabled:YES];
}

- (void)layoutMainChrome {
    if (_layingOutChrome) return;
    _layingOutChrome = YES;
    [self layoutMainChromeGeometry];
    CGSize sz = self.view.bounds.size;
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
    _layingOutChrome = NO;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutMainChrome];
}

- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)io
                                         duration:(NSTimeInterval)duration {
    (void)io; (void)duration;
    _rotating = YES;
    [self.view setNeedsLayout];
    [self layoutMainChrome];
}

/* ios 8 replaced the rotation callbacks with this one, and the old pair is not
   called there at all */
- (void)viewWillTransitionToSize:(CGSize)size
       withTransitionCoordinator:(id)coordinator {
    struct objc_super sup = { self, [UIViewController class] };
    if ([[UIViewController class] instancesRespondToSelector:_cmd])
        ((void (*)(struct objc_super *, SEL, CGSize, id))objc_msgSendSuper)(
            &sup, _cmd, size, coordinator);
    _rotating = YES;
    [self.view setNeedsLayout];
    if ([coordinator respondsToSelector:
            @selector(animateAlongsideTransition:completion:)]) {
        ((void (*)(id, SEL, id, id))objc_msgSend)(
            coordinator, @selector(animateAlongsideTransition:completion:), nil,
            ^(id context) {
                (void)context;
                [self finishRotation];
            });
        return;
    }
    [self performSelector:@selector(finishRotation) withObject:nil afterDelay:0.4];
}

- (void)finishRotation {
    if (!_rotating) return;
    _rotating = NO;
    _listHeaderProgress = 0.0f;
    /* ios 5 can enter viewDidLayoutSubviews again while this callback is
       forcing layout. leave the next pass to UIKit, which already owns the
       rotation transaction */
    [self.view setNeedsLayout];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io {
    (void)io;
    /* the spacer the collapse is measured against is a different height in the
       new orientation, so the stored progress belongs to a screen that is gone.
       the rows are not reloaded: they are already on screen, and replaying
       their entrance animation is what made a rotation flicker */
    [self finishRotation];
    if (_boyField && SenkoThemeIsBoykisser())
        [self syncBoykisserField];
}

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
    _listHeaderProgress = 0.0f;
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
    /* core animation drops layer animations when the app is backgrounded, so
       the connecting pulse has to be reinstalled on the way back. the notify
       centre passes the notification, which -applyState does not take */
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(appDidBecomeActive:)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(appWillResignActive:)
                                                 name:UIApplicationWillResignActiveNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(widgetToggleRequested:)
                                                 name:SENKO_WIDGET_TOGGLE_NOTIFICATION
                                               object:nil];

    _revealedRows = [[NSMutableSet alloc] init];
    SenkoCrashTheme([SenkoThemeCurrentId() UTF8String]);

    UILabel *title = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    title.tag = SenkoHomeTagTitle;
    title.text = @"Senko";
    [self styleHeaderTitle:title];
    [self.view addSubview:title];

    UIButton *gear = [self makeHeaderButton:@selector(settingsPressed)
                                         tag:SenkoHomeTagGear];
    [self.view addSubview:gear];
    UIButton *plus = [self makeHeaderButton:@selector(addPressed)
                                         tag:SenkoHomeTagPlus];
    [self.view addSubview:plus];

    _statusCard = SenkoHomeBuildStatusCard(self.view);

    _connectBtn = [UIButton buttonWithType:UIButtonTypeCustom];
    _connectBtn.tag = SenkoHomeTagConnect;
    _connectBtn.contentHorizontalAlignment = UIControlContentHorizontalAlignmentCenter;
    _connectBtn.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
    _connectBtn.titleLabel.textAlignment = NSTextAlignmentCenter;
    [_connectBtn addTarget:self action:@selector(togglePressed)
          forControlEvents:UIControlEventTouchUpInside];
    [_connectBtn addTarget:self action:@selector(chromeButtonDown:)
          forControlEvents:UIControlEventTouchDown];
    [_connectBtn addTarget:self action:@selector(chromeButtonUp:)
          forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                           UIControlEventTouchCancel];
    [_statusCard addSubview:_connectBtn];

    _pingAllBtn = [UIButton buttonWithType:UIButtonTypeCustom];
    _pingAllBtn.accessibilityLabel = SenkoLocalizedText(@"Check servers");
    [_pingAllBtn addTarget:self action:@selector(pingPressed)
          forControlEvents:UIControlEventTouchUpInside];
    [_pingAllBtn addTarget:self action:@selector(chromeButtonDown:)
          forControlEvents:UIControlEventTouchDown];
    [_pingAllBtn addTarget:self action:@selector(chromeButtonUp:)
          forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                           UIControlEventTouchCancel];
    [_statusCard addSubview:_pingAllBtn];

    /* the detail line is the same label the rest of the app writes progress
       into, so every SetStatusDefault caller keeps working unchanged */
    _statusLabel = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    _statusLabel.backgroundColor = [UIColor clearColor];
    _statusLabel.numberOfLines = 2;
    _statusLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    _statusLabel.adjustsFontSizeToFitWidth = YES;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    _statusLabel.minimumFontSize = 10.0f;
#pragma clang diagnostic pop
    SetStatusDefault(_statusLabel, @"idle");
    [_statusCard addSubview:_statusLabel];

    _ui.title = title;
    _ui.gear = gear;
    _ui.plus = plus;
    _ui.card = _statusCard;
    _ui.connect = _connectBtn;
    _ui.check = _pingAllBtn;
    _ui.detail = _statusLabel;
    _ui.background = _bgGrad;
    SenkoHomeStyleChrome(&_ui);
    SenkoHomeApplyStatus(&_ui, @"idle", SenkoLocalizedText(@"Disconnected"), NO);

    UIView *well = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    well.tag = SenkoHomeTagWell;
    well.layer.masksToBounds = YES;
    CAGradientLayer *wellG = [CAGradientLayer layer];
    wellG.name = @"wellGrad";
    [well.layer insertSublayer:wellG atIndex:0];
    [self.view addSubview:well];
    _ui.well = well;
    [self styleListWell];

    BOOL pad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
    _table = [[[UITableView alloc] initWithFrame:CGRectZero
                                            style:UITableViewStylePlain] autorelease];
    _table.dataSource = self;
    _table.delegate = self;
    _table.backgroundColor = [UIColor clearColor];
    if ([_table respondsToSelector:@selector(setBackgroundView:)])
        _table.backgroundView = nil;
    _table.separatorStyle = UITableViewCellSeparatorStyleNone;
    _table.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.12f]
        : [UIColor colorWithWhite:1 alpha:0.16f];
    _table.rowHeight = pad ? 100.0f : 92.0f;
    _table.sectionHeaderHeight = pad ? 60.0f : 52.0f;
    _table.delaysContentTouches = NO;
    _table.canCancelContentTouches = YES;
    _table.showsVerticalScrollIndicator = YES;
    if ([_table respondsToSelector:@selector(setEstimatedRowHeight:)])
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(_table, @selector(setEstimatedRowHeight:), 0.0f);
    if ([_table respondsToSelector:@selector(setEstimatedSectionHeaderHeight:)])
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(_table, @selector(setEstimatedSectionHeaderHeight:), 0.0f);
    if ([_table respondsToSelector:@selector(setSectionHeaderTopPadding:)])
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(_table, @selector(setSectionHeaderTopPadding:), 0.0f);
    UILongPressGestureRecognizer *rowDrag = [[[UILongPressGestureRecognizer alloc]
                                              initWithTarget:self action:@selector(rowLongPressed:)] autorelease];
    rowDrag.minimumPressDuration = 0.55;
    [_table addGestureRecognizer:rowDrag];
    [self.view addSubview:_table];
    _ui.table = _table;

    _emptyState = [[SenkoEmptyStateView alloc] initWithFrame:CGRectZero];
    _emptyState.hidden = YES;
    [_emptyState->pasteButton addTarget:self action:@selector(emptyStatePastePressed)
                       forControlEvents:UIControlEventTouchUpInside];
    [_emptyState->scanButton addTarget:self action:@selector(emptyStateScanPressed)
                      forControlEvents:UIControlEventTouchUpInside];
    [_emptyState->hwidTap addTarget:self action:@selector(emptyStateCopyHWID)
                   forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:_emptyState];
    /* the list scrolls under the card now, so the card has to sit above it */
    [self bringMainChromeToFront];
}

- (void)widgetToggleRequested:(NSNotification *)note {
    (void)note;
    [self togglePressed];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    /* the sort order lives in defaults and can change while settings are up */
    [self rebuildSections];
    [self layoutMainChrome];
    [_table reloadData];
    [_table layoutIfNeeded];
    [self syncEmptyState];
    if (!_catalogLoaded)
        [self showBusyOverlay:SenkoLocalizedText(@"Loading servers and subscriptions...")];
    [self ensureDaemonThenRefresh];
}

- (void)ensureDaemonThenRefresh {
#if SENKO_STOCK_NATIVE
    [self refreshNativeCatalog];
    return;
#else
    /* probing during a live tunnel can overwrite its status with stale state */
    BOOL quiet = [self isTunnelActive];
    [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
        if (!up) {
            if (quiet) {
                [self refresh];
                return;
            }
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
#endif
}


- (void)appDidBecomeActive:(NSNotification *)n {
    (void)n;
    [self applyState];
/* nothing polled the daemon while the app was away, so the card was still
   showing the state the last user action left behind. the tunnel outlives the
   app, and coming back is the first chance to find out what it is doing */
    [self startStatusHeartbeat];
#if SENKO_STOCK_NATIVE
    [self refreshNativeCatalog];
#else
    [self ensureDaemonThenRefresh];
#endif
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

- (void)settingsPressed {
    [self dismissCurrentActionSheetAnimated:YES];
    SettingsVC *s = [[[SettingsVC alloc] init] autorelease];
    UINavigationController *nav = [[[UINavigationController alloc]
                                    initWithRootViewController:s] autorelease];
    if ([nav respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(nav, @selector(setEdgesForExtendedLayout:), 0);
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    [self presentViewController:nav animated:YES completion:nil];
}

@end
