#import "main_vc_priv.h"

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
    [_misideLogo release];
    [_frutigerBg release];
    [_ios26Bg release];
    [_actionSheet release];
    [_ctl release];
    [_servers release];
    [_subs release];
    [_sectionOrder release];
    [_sections release];
    [_collapsedSubs release];
    [_state release];
    [_lastErr release];
    [_serverStatus release];
    [_pingingSubs release];
    [_pendingUpdatePath release];
    [_sectionDragSnapshot removeFromSuperview];
    [_sectionDragSnapshot release];
    [super dealloc];
}


- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    self.view.backgroundColor = kBG;
    [self applyBackgroundForCurrentState:NO];
    SenkoThemeSfxPrepare();
    [self syncBoykisserField];
    [self syncMisideDecor];
    [self syncFrutigerDecor];
    [self syncIos26Decor];
    [self syncBubbleField];
    [self layoutWallpaperStack];
    UILabel *title = (UILabel *)[self.view viewWithTag:8001];
    if ([title isKindOfClass:[UILabel class]]) {
        BOOL isPad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
        CGFloat H = self.view.bounds.size.height;
        BOOL compact = (!isPad && H <= 568.0f);
        CGFloat titleSize = isPad ? 28.0f : (compact ? 22.0f : 26.0f);
        if (SenkoThemeIsIos16()) {
            title.font = SenkoFontTitle(titleSize);
            title.textColor = kInk;
            title.shadowColor = nil;
            title.shadowOffset = CGSizeZero;
        } else if (SenkoThemeIsFlat()) {
            title.font = [UIFont fontWithName:@"HelveticaNeue-Light" size:isPad ? 26.0f : (compact ? 19.0f : 22.0f)]
                ?: [UIFont systemFontOfSize:isPad ? 26.0f : (compact ? 19.0f : 22.0f)];
            SenkoStyleAccentLabel(title);
        } else {
            title.font = [UIFont boldSystemFontOfSize:isPad ? 26.0f : (compact ? 19.0f : 22.0f)];
            SenkoStyleAccentLabel(title);
        }
    }
    UIButton *gear = (UIButton *)[self.view viewWithTag:8002];
    if ([gear isKindOfClass:[UIButton class]])
        [gear setImage:TintedIconNamed(@"icon-settings.png", 22, kAccentBlue)
              forState:UIControlStateNormal];
    UIButton *refresh = (UIButton *)[self.view viewWithTag:8003];
    if ([refresh isKindOfClass:[UIButton class]])
        [refresh setImage:TintedIconNamed(@"icon-refresh.png", 22, kAccentBlue)
                 forState:UIControlStateNormal];
    UIButton *plus = (UIButton *)[self.view viewWithTag:8004];
    if ([plus isKindOfClass:[UIButton class]])
        SenkoStylePaperGlyph(plus);
    if (_pingAllBtn)
        StyleGlossyCapsule(_pingAllBtn, kAccentBlue, kAccentBlueLo);
    if (_connectBtn) {
        BOOL active = [self isTunnelActive];
        StyleDomeColors(_connectBtn,
                        active ? kConnOn : kIdleGrey,
                        active ? kConnOnLo : kIdleGreyLo);
    }
    if (_statusLabel) {
        if (SenkoThemeIsIos16())
            SenkoStyleIos16StatusPill(_statusLabel);
        else if (SenkoThemeIsFlat()) {
            _statusLabel.backgroundColor = SenkoThemeIsLight()
                ? [UIColor colorWithRed:0.94 green:0.95 blue:0.98 alpha:0.80]
                : [UIColor colorWithRed:0.16 green:0.18 blue:0.24 alpha:0.80];
            _statusLabel.layer.borderWidth = 0;
            _statusLabel.layer.borderColor = [UIColor clearColor].CGColor;
            _statusLabel.layer.cornerRadius = 8;
        } else {
            _statusLabel.backgroundColor = kCellLo;
            _statusLabel.layer.borderWidth = 0;
            _statusLabel.layer.borderColor = [UIColor clearColor].CGColor;
            _statusLabel.layer.cornerRadius = 8;
        }
        [self applyState];
    }
    [self styleListWell];
    if (_table) {
        _table.backgroundColor = [UIColor clearColor];
        _table.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
        _table.separatorColor = SenkoThemeIsLight()
            ? [UIColor colorWithWhite:0 alpha:0.12f]
            : [UIColor colorWithWhite:1 alpha:0.16f];
        if ([_table respondsToSelector:@selector(setBackgroundView:)])
            _table.backgroundView = nil;
    }
    [_table reloadData];
    [self.view setNeedsLayout];
    [self layoutMainChrome];
}

/* list tray */
- (void)styleListWell {
    UIView *well = [self.view viewWithTag:9001];
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
            UIColor *veil = [UIColor colorWithRed:1.0 green:0.94 blue:0.97 alpha:0.35];
            wg.colors = [NSArray arrayWithObjects:(id)veil.CGColor, (id)veil.CGColor, nil];
        } else if (SenkoThemeIsFrutigeraero()) {
/* use a soft glass tray */
            UIColor *veil = [UIColor colorWithWhite:1 alpha:0.28];
            wg.colors = [NSArray arrayWithObjects:(id)veil.CGColor, (id)veil.CGColor, nil];
        } else if (SenkoThemeIsIos26()) {
/* let the wallpaper show through */
            wg.colors = [NSArray arrayWithObjects:
                         (id)[UIColor clearColor].CGColor,
                         (id)[UIColor clearColor].CGColor, nil];
            wg.hidden = YES;
        } else if (SenkoThemeIsMiside()) {
/* keep the pattern visible */
            wg.colors = [NSArray arrayWithObjects:
                         (id)[UIColor clearColor].CGColor,
                         (id)[UIColor clearColor].CGColor, nil];
            wg.hidden = YES;
        } else {
/* let cells and plates carry chrome */
            wg.colors = [NSArray arrayWithObjects:(id)kBG.CGColor, (id)kBGBot.CGColor, nil];
        }
    }
}


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
/* restart overlays after a modal */
    [self syncBoykisserField];
    [self syncBubbleField];
    [_boyField setPaused:NO];
    [_bubbleField setPaused:NO];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [_boyField setPaused:YES];
    [_bubbleField setPaused:YES];
    (void)animated;
}

/* scroll only changes geometry */
- (void)layoutMainChromeGeometry {
    BOOL active = [self isTunnelActive];
    UIColor *top = active ? kConnOn : kIdleGrey;
    UIColor *bot = active ? kConnOnLo : kIdleGreyLo;
    SenkoLayoutMainContent(self.view, _bgGrad, _table, _pingAllBtn, _statusLabel,
                           _connectBtn, top, bot, _listHeaderProgress);
    [self layoutStatusGlow];
    if (_boyField && !CGSizeEqualToSize(_boyField.bounds.size, self.view.bounds.size))
        _boyField.frame = self.view.bounds;
    if (_bubbleField && !CGSizeEqualToSize(_bubbleField.bounds.size, self.view.bounds.size))
        _bubbleField.frame = self.view.bounds;
    if (_frutigerBg && !_frutigerBg.hidden)
        _frutigerBg.frame = self.view.bounds;
    if (_ios26Bg && !_ios26Bg.hidden)
        _ios26Bg.frame = self.view.bounds;
}

- (void)layoutMainChrome {
    [self layoutMainChromeGeometry];
    CGSize sz = self.view.bounds.size;
    NSString *key = [self backgroundStatusKey];
    BOOL sizeChanged = !CGSizeEqualToSize(sz, _laidChromeSize);
    BOOL statusChanged = !(_laidStatusKey && [key isEqualToString:_laidStatusKey]);
/* rebuild wallpaper on size or status change */
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
    if (SenkoThemeIsIos16() && _statusLabel)
        SenkoStyleIos16StatusPill(_statusLabel);
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutMainChrome];
}

- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)io
                                         duration:(NSTimeInterval)duration {
    (void)io; (void)duration;
    [self.view setNeedsLayout];
    [self layoutMainChrome];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io {
    (void)io;
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
/* restyle connect after rotation */
    if (_connectBtn) {
        CGAffineTransform t = _connectBtn.transform;
        _connectBtn.transform = CGAffineTransformIdentity;
        _connectBtn.bounds = CGRectMake(0, 0, 128, 128);
        BOOL active = [self isTunnelActive];
        ApplyGlossyDome(_connectBtn,
                        active ? kConnOn : kIdleGrey,
                        active ? kConnOnLo : kIdleGreyLo);
        [_connectBtn bringSubviewToFront:_connectBtn.titleLabel];
        _connectBtn.transform = t;
    }
    [self layoutMainChrome];
    if (_boyField && SenkoThemeIsBoykisser())
        [self syncBoykisserField];
    [_table reloadData];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    _ctl = [[SenkoControl alloc] initWithSocketPath:SENKO_SOCK];
    _selectedSrvIdx = -1;
    _menuSubIdx = -1;
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
    _subs = [[NSMutableArray alloc] init];
    _collapsedSubs = [[NSMutableSet alloc] init];
    _listHeaderProgress = 0.0f;
    _headerSnapAnimating = NO;
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];

    CGRect b = self.view.bounds;
    CGFloat topOffset = GetTopOffset();
    BOOL pad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
    BOOL compact = (!pad && b.size.height <= 568.0f); /* small iphone layout */
/* keep the title below the status bar */
    CGFloat headerY = (pad ? 16.0f : (compact ? 14.0f : 12.0f)) + topOffset;
    CGFloat headerH = pad ? 42.0f : (compact ? 28.0f : 34.0f);
    CGFloat headerButtonSide = pad ? 46.0f : (compact ? 32.0f : 38.0f);
    CGFloat headerButtonY = headerY + (headerH - headerButtonSide) / 2.0f;
    CGFloat headerIcon = pad ? 26.0f : (compact ? 20.0f : 22.0f);
    CGFloat headerPad = pad ? 16.0f : (compact ? 8.0f : 10.0f);
    CGFloat titleSize = pad ? 26.0f : (compact ? 19.0f : 22.0f);

    UILabel *title = [[[UILabel alloc] initWithFrame:CGRectMake(0, headerY, b.size.width, headerH)] autorelease];
    title.tag = 8001;
    title.text = @"Senko";
    title.textAlignment = NSTextAlignmentCenter;
    title.backgroundColor = [UIColor clearColor];
    if (SenkoThemeIsIos16()) {
        CGFloat ios16Title = pad ? 28.0f : (compact ? 22.0f : 26.0f);
        title.font = SenkoFontTitle(ios16Title);
        title.textColor = kInk;
        title.shadowColor = nil;
        title.shadowOffset = CGSizeZero;
    } else if (SenkoThemeIsFlat()) {
        title.font = [UIFont fontWithName:@"HelveticaNeue-Light" size:titleSize]
            ?: [UIFont systemFontOfSize:titleSize];
        SenkoStyleAccentLabel(title);
    } else {
        title.font = [UIFont boldSystemFontOfSize:titleSize];
        SenkoStyleAccentLabel(title);
    }
    title.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [self.view addSubview:title];

    UIImage *gearImg = TintedIconNamed(@"icon-settings.png", headerIcon, kAccentBlue);
    UIImage *refreshImg = TintedIconNamed(@"icon-refresh.png", headerIcon, kAccentBlue);

/* use icon buttons without plates */
    UIButton *gear = [UIButton buttonWithType:UIButtonTypeCustom];
    gear.tag = 8002;
    gear.frame = CGRectMake(headerPad, headerButtonY, headerButtonSide, headerButtonSide);
    if (gearImg) [gear setImage:gearImg forState:UIControlStateNormal];
    gear.imageView.contentMode = UIViewContentModeScaleAspectFit;
    gear.imageEdgeInsets = UIEdgeInsetsMake(6, 6, 6, 6);
    [gear addTarget:self action:@selector(settingsPressed) forControlEvents:UIControlEventTouchUpInside];
    gear.autoresizingMask = UIViewAutoresizingFlexibleRightMargin;
    [self.view addSubview:gear];

    _pingAllBtn = [UIButton buttonWithType:UIButtonTypeCustom];
    _pingAllBtn.frame = CGRectMake(12, 210 + topOffset, 96, 30);
    [_pingAllBtn setTitle:@"Ping All" forState:UIControlStateNormal];
    _pingAllBtn.autoresizingMask = UIViewAutoresizingFlexibleRightMargin;
    [_pingAllBtn addTarget:self action:@selector(pingPressed) forControlEvents:UIControlEventTouchUpInside];
/* style once before scrolling */
    StyleGlossyCapsule(_pingAllBtn, kAccentBlue, kAccentBlueLo);
    [self.view addSubview:_pingAllBtn];

    CGFloat headerGap = 8;
    CGFloat plusX = b.size.width - headerPad - headerButtonSide;
    CGFloat refreshX = plusX - headerGap - headerButtonSide;

    UIButton *refresh = [UIButton buttonWithType:UIButtonTypeCustom];
    refresh.tag = 8003;
    refresh.frame = CGRectMake(refreshX, headerButtonY, headerButtonSide, headerButtonSide);
    if (refreshImg) [refresh setImage:refreshImg forState:UIControlStateNormal];
    refresh.imageView.contentMode = UIViewContentModeScaleAspectFit;
    refresh.imageEdgeInsets = UIEdgeInsetsMake(6, 6, 6, 6);
    [refresh addTarget:self action:@selector(refreshPressed) forControlEvents:UIControlEventTouchUpInside];
    refresh.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
    [self.view addSubview:refresh];

    UIButton *plus = [UIButton buttonWithType:UIButtonTypeCustom];
    plus.frame = CGRectMake(plusX, headerButtonY, headerButtonSide, headerButtonSide);
    [plus setTitle:@"+" forState:UIControlStateNormal];
    plus.titleLabel.font = [UIFont boldSystemFontOfSize:(compact ? 24.0f : (pad ? 30.0f : 28.0f))];
    plus.titleEdgeInsets = UIEdgeInsetsMake(-2, 0, 2, 0);
/* keep plus as an accent title */
    SenkoStylePaperGlyph(plus);
    plus.tag = 8004;
    [plus addTarget:self action:@selector(addPressed) forControlEvents:UIControlEventTouchUpInside];
    plus.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
    [self.view addSubview:plus];

    CGFloat d = 128;
    _connectBtn = [UIButton buttonWithType:UIButtonTypeCustom];
    _connectBtn.tag = 8005; /* layout finds connect by tag */
    _connectBtn.frame = CGRectMake((b.size.width - d) / 2, 68 + topOffset, d, d);
    _connectBtn.titleLabel.font = [UIFont boldSystemFontOfSize:20];
    [_connectBtn setTitle:@"OFF" forState:UIControlStateNormal];
/* let layout own the frame and transform */
    _connectBtn.autoresizingMask = UIViewAutoresizingNone;
    [_connectBtn addTarget:self action:@selector(togglePressed) forControlEvents:UIControlEventTouchUpInside];
    _btnBody = ApplyGlossyDome(_connectBtn, kIdleGrey, kIdleGreyLo);
    [_connectBtn bringSubviewToFront:_connectBtn.titleLabel];
    [self.view addSubview:_connectBtn];

    _statusLabel = [[[UILabel alloc] initWithFrame:CGRectMake(116, 210 + topOffset, b.size.width - 128, 30)] autorelease];
    _statusLabel.textAlignment = NSTextAlignmentCenter;
    SetStatusDefault(_statusLabel, @"idle");
    if (SenkoThemeIsIos16()) {
        SenkoStyleIos16StatusPill(_statusLabel);
    } else if (SenkoThemeIsFlat()) {
        _statusLabel.backgroundColor = SenkoThemeIsLight()
            ? [UIColor colorWithRed:0.94 green:0.95 blue:0.98 alpha:0.80]
            : [UIColor colorWithRed:0.16 green:0.18 blue:0.24 alpha:0.80];
        _statusLabel.layer.borderColor = [UIColor clearColor].CGColor;
        _statusLabel.layer.cornerRadius = 8;
        _statusLabel.layer.masksToBounds = YES;
        _statusLabel.layer.borderWidth = 0;
    } else {
        _statusLabel.backgroundColor = kCellLo;
        _statusLabel.layer.borderColor = [UIColor clearColor].CGColor;
        _statusLabel.layer.cornerRadius = 8;
        _statusLabel.layer.masksToBounds = YES;
        _statusLabel.layer.borderWidth = 0;
    }
    _statusLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [self.view addSubview:_statusLabel];

/* list tray */
    UIView *well = [[[UIView alloc] initWithFrame:CGRectMake(0, 242 + topOffset,
                                                             b.size.width,
                                                             b.size.height - 248 - topOffset)] autorelease];
    well.tag = 9001;
    well.layer.cornerRadius = 0;
    well.layer.masksToBounds = YES;
    well.layer.borderWidth = 0;
    well.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    CAGradientLayer *wellG = [CAGradientLayer layer];
    wellG.name = @"wellGrad";
    wellG.frame = well.bounds;
    [well.layer insertSublayer:wellG atIndex:0];
    [self.view addSubview:well];
    [self styleListWell];

    CGRect tf = CGRectMake(0, 248 + topOffset, b.size.width, b.size.height - 248 - topOffset);
    _table = [[[UITableView alloc] initWithFrame:tf style:UITableViewStylePlain] autorelease];
    _table.dataSource = self;
    _table.delegate = self;
    _table.backgroundColor = [UIColor clearColor];
    if ([_table respondsToSelector:@selector(setBackgroundView:)])
        _table.backgroundView = nil;
    _table.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    _table.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.12f]
        : [UIColor colorWithWhite:1 alpha:0.16f];
    _table.rowHeight = pad ? 100.0f : 88.0f;
    _table.sectionHeaderHeight = pad ? 60.0f : 52.0f;
/* use fixed row heights */
    _table.delaysContentTouches = NO;
    _table.canCancelContentTouches = YES;
/* reduce gpu work while scrolling */
    _table.showsVerticalScrollIndicator = YES;
    if ([_table respondsToSelector:@selector(setEstimatedRowHeight:)])
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(_table, @selector(setEstimatedRowHeight:), 0.0f);
    if ([_table respondsToSelector:@selector(setEstimatedSectionHeaderHeight:)])
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(_table, @selector(setEstimatedSectionHeaderHeight:), 0.0f);
    if ([_table respondsToSelector:@selector(setSectionHeaderTopPadding:)])
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(_table, @selector(setSectionHeaderTopPadding:), 0.0f);
    _table.autoresizingMask = UIViewAutoresizingFlexibleHeight |
                              UIViewAutoresizingFlexibleLeftMargin |
                              UIViewAutoresizingFlexibleRightMargin;
    UILongPressGestureRecognizer *rowDrag = [[[UILongPressGestureRecognizer alloc]
                                              initWithTarget:self action:@selector(rowLongPressed:)] autorelease];
    rowDrag.minimumPressDuration = 0.55;
    [_table addGestureRecognizer:rowDrag];
    [self.view addSubview:_table];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self ensureDaemonThenRefresh];
}

/* restart a dead senkod */
- (void)ensureDaemonThenRefresh {
    /* skip the probe while connected */
    BOOL quiet = [self isTunnelActive];
    [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
        if (!up) {
/* keep the live strip during refresh */
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
        /* an already running daemon returns no detail, so clear the probe text */
        if (!quiet && detail && [detail length])
            SetStatusRefresh(_statusLabel, detail);
        else if (!quiet)
            SetStatusDefault(_statusLabel, @"idle");
        [self refresh];
    }];
}


- (void)settingsPressed {
    if (_actionSheet) {
        [_actionSheet dismissWithClickedButtonIndex:_actionSheet.cancelButtonIndex animated:YES];
        [_actionSheet release];
        _actionSheet = nil;
    }
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
