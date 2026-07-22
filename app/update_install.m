#import "update_install.h"
#import "control_client.h"
#import "ui_theme.h"

#import <QuartzCore/QuartzCore.h>

@implementation UpdateInstallVC {
    BOOL _started;
}

- (id)initWithControl:(SenkoControl *)ctl packagePath:(NSString *)path {
    if ((self = [super init])) {
        _ctl = [ctl retain];
        _path = [path copy];
        _progressFloor = 0.02f;
        _progressCap = 0.08f;
        _finished = NO;
        _ok = NO;
        _started = NO;
    }
    return self;
}

- (void)dealloc {
    [_creepTimer invalidate];
    [_creepTimer release];
    [_bgGrad release];
    [_ctl release];
    [_path release];
    [super dealloc];
}

- (void)appendLog:(NSString *)line {
    if (![line length]) return;
    NSString *cur = _log.text ? _log.text : @"";
    _log.text = [cur stringByAppendingFormat:@"%@\n", line];
    if ([_log.text length] > 2) {
        NSRange end = NSMakeRange([_log.text length] - 1, 1);
        [_log scrollRangeToVisible:end];
    }
}

- (void)setBarProgress:(float)p animated:(BOOL)animated {
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    if (p < _progressFloor) p = _progressFloor;
    [_bar setProgress:p animated:animated];
}

- (void)stopCreep {
    [_creepTimer invalidate];
    [_creepTimer release];
    _creepTimer = nil;
}

- (void)creepTick:(NSTimer *)t {
    (void)t;
    if (_finished) return;
    float cur = _bar.progress;
    if (cur >= _progressCap) return;
    float step = (_progressCap - cur) * 0.08f;
    if (step < 0.004f) step = 0.004f;
    [self setBarProgress:cur + step animated:YES];
}

- (void)startCreepTo:(float)cap {
    _progressCap = cap;
    [self stopCreep];
    _creepTimer = [[NSTimer scheduledTimerWithTimeInterval:0.12
                                                    target:self
                                                  selector:@selector(creepTick:)
                                                  userInfo:nil
                                                   repeats:YES] retain];
}

- (void)applyStage:(NSString *)stage {
    if (_finished) return;
    NSString *s = [stage lowercaseString];
    if ([s hasPrefix:@"checking"]) {
        _statusLbl.text = @"Checking package";
        _progressFloor = 0.08f;
        [self setBarProgress:0.12f animated:YES];
        [self startCreepTo:0.22f];
        [self appendLog:@"Preparing package"];
    } else if ([s hasPrefix:@"stopping"]) {
        _statusLbl.text = @"Stopping services";
        _progressFloor = 0.25f;
        [self setBarProgress:0.28f animated:YES];
        [self startCreepTo:0.38f];
        [self appendLog:@"Stopping senkod"];
    } else if ([s hasPrefix:@"installing"]) {
        _statusLbl.text = @"Installing";
        _progressFloor = 0.40f;
        [self setBarProgress:0.45f animated:YES];
/* creep bar while dpkg emits no stages */
        [self startCreepTo:0.82f];
        [self appendLog:@"Running dpkg --install"];
    } else if ([s hasPrefix:@"starting"]) {
        _statusLbl.text = @"Starting services";
        _progressFloor = 0.86f;
        [self setBarProgress:0.90f animated:YES];
        [self startCreepTo:0.95f];
        [self appendLog:@"Restarting senkod"];
    } else if ([s hasPrefix:@"done"]) {
        _statusLbl.text = @"Finishing";
        _progressFloor = 0.96f;
        [self setBarProgress:0.97f animated:YES];
        [self stopCreep];
        [self appendLog:@"Almost done"];
    } else {
        [self appendLog:stage];
    }
}

- (void)finishOk:(NSString *)version {
    _finished = YES;
    _ok = YES;
    [self stopCreep];
    [self setBarProgress:1.0f animated:YES];
    NSString *ver = [version length] ? version : @"ok";
    _statusLbl.text = [NSString stringWithFormat:@"Installed %@", ver];
    _titleLbl.text = @"Complete";
    [self appendLog:[NSString stringWithFormat:@"Done: %@", ver]];
    [self appendLog:@"Tap Close when you are ready."];
    _closeBtn.enabled = YES;
    _closeBtn.alpha = 1.0f;
    StyleGlossyCapsule(_closeBtn, kConnOn, kConnOnLo);
    [_closeBtn setTitle:@"Close" forState:UIControlStateNormal];
}

- (void)finishErr:(NSString *)msg {
    _finished = YES;
    _ok = NO;
    [self stopCreep];
    _statusLbl.text = @"Install failed";
    _titleLbl.text = @"Failed";
    [self appendLog:msg ? msg : @"unknown error"];
    [self appendLog:@"See /tmp/senko-update.log"];
    _closeBtn.enabled = YES;
    _closeBtn.alpha = 1.0f;
    StyleGlossyCapsule(_closeBtn, kIdleGrey, kIdleGreyLo);
    [_closeBtn setTitle:@"Close" forState:UIControlStateNormal];
}

- (void)closePressed {
    if (_ok) {
/* exit; running image cannot replace itself */
        exit(0);
    }
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)layoutInstall {
    if (!_titleLbl || !_plate || !_logPlate || !_closeBtn) return;

    CGRect b = SenkoViewBounds(self.view);
    if (b.size.width < 1.0f || b.size.height < 1.0f) return;

    CGFloat top = GetTopOffset() + 24.0f;
    CGFloat pad = 18.0f;
    CGFloat w = MAX(1.0f, b.size.width - pad * 2.0f);

    _bgGrad.frame = b;
    _titleLbl.frame = CGRectMake(pad, top, w, 28.0f);
    _pkgLbl.frame = CGRectMake(pad, top + 32.0f, w, 22.0f);

    _plate.frame = CGRectMake(pad, top + 70.0f, w, 72.0f);
    _bar.frame = CGRectMake(14.0f, 18.0f,
                            MAX(1.0f, _plate.bounds.size.width - 28.0f), 12.0f);
    _statusLbl.frame = CGRectMake(14.0f, 40.0f,
                                  MAX(1.0f, _plate.bounds.size.width - 28.0f), 20.0f);

    CGFloat logY = top + 156.0f;
    CGFloat btnH = 44.0f;
    CGFloat btnY = b.size.height - btnH - 24.0f;
    if (btnY < logY + 100.0f) btnY = logY + 100.0f;
    CGFloat logH = btnY - logY - 16.0f;
    if (logH < 80.0f) logH = 80.0f;

    _logPlate.frame = CGRectMake(pad, logY, w, logH);
    _log.frame = CGRectInset(_logPlate.bounds, 8.0f, 8.0f);
    _closeBtn.frame = CGRectMake(pad, btnY, w, btnH);
    StyleGlossyCapsuleLayout(_closeBtn);
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithWhite:0.06 alpha:1.0];
    _bgGrad = [AddVGradient(self.view, [UIColor colorWithWhite:0.10 alpha:1],
                            [UIColor colorWithWhite:0.03 alpha:1]) retain];

    CGRect b = self.view.bounds;
    CGFloat top = GetTopOffset() + 24.0f;
    CGFloat pad = 18.0f;
    CGFloat w = b.size.width - pad * 2;

    _titleLbl = [[[UILabel alloc] initWithFrame:CGRectMake(pad, top, w, 28)] autorelease];
    _titleLbl.backgroundColor = [UIColor clearColor];
    _titleLbl.textAlignment = NSTextAlignmentCenter;
    _titleLbl.font = [UIFont boldSystemFontOfSize:20];
/* fixed dark sheet independent of theme */
    SenkoStyleInkOnDark(_titleLbl);
    _titleLbl.text = @"Installing";
    _titleLbl.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [self.view addSubview:_titleLbl];

    _pkgLbl = [[[UILabel alloc] initWithFrame:CGRectMake(pad, top + 32, w, 22)] autorelease];
    _pkgLbl.backgroundColor = [UIColor clearColor];
    _pkgLbl.textAlignment = NSTextAlignmentCenter;
    _pkgLbl.font = [UIFont systemFontOfSize:15];
    SenkoStyleAccentOnDark(_pkgLbl);
    _pkgLbl.text = @"Senko";
    _pkgLbl.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [self.view addSubview:_pkgLbl];

/* holds bar + label */
    _plate = [[[UIView alloc] initWithFrame:CGRectMake(pad, top + 70, w, 72)] autorelease];
    _plate.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    _plate.layer.cornerRadius = 10;
    _plate.layer.borderWidth = 0;
    _plate.layer.borderColor = [UIColor clearColor].CGColor;
    _plate.backgroundColor = [UIColor colorWithWhite:0.08 alpha:1];
    [self.view addSubview:_plate];

    _bar = [[[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault] autorelease];
    _bar.frame = CGRectMake(14, 18, w - 28, 12);
    _bar.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    _bar.progress = 0.02f;
    if ([_bar respondsToSelector:@selector(setProgressTintColor:)]) {
        _bar.progressTintColor = kAccentBlue;
        _bar.trackTintColor = [UIColor colorWithWhite:0.22 alpha:1];
    }
    [_plate addSubview:_bar];

    _statusLbl = [[[UILabel alloc] initWithFrame:CGRectMake(14, 40, w - 28, 20)] autorelease];
    _statusLbl.backgroundColor = [UIColor clearColor];
    _statusLbl.textAlignment = NSTextAlignmentCenter;
    _statusLbl.font = [UIFont boldSystemFontOfSize:13];
    SenkoStyleMutedOnDark(_statusLbl);
    _statusLbl.text = @"Starting";
    _statusLbl.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [_plate addSubview:_statusLbl];

    CGFloat logY = top + 156;
    CGFloat btnH = 44;
    CGFloat btnY = b.size.height - btnH - 24.0f;
    if (btnY < logY + 100.0f) btnY = logY + 100.0f;
    CGFloat logH = btnY - logY - 16;
    if (logH < 80) logH = 80;

    _logPlate = [[[UIView alloc] initWithFrame:CGRectMake(pad, logY, w, logH)] autorelease];
    _logPlate.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _logPlate.layer.cornerRadius = 10;
    _logPlate.layer.borderWidth = 0;
    _logPlate.layer.borderColor = [UIColor clearColor].CGColor;
    _logPlate.backgroundColor = [UIColor colorWithWhite:0.04 alpha:1];
    _logPlate.clipsToBounds = YES;
    [self.view addSubview:_logPlate];

    _log = [[[UITextView alloc] initWithFrame:CGRectInset(_logPlate.bounds, 8, 8)] autorelease];
    _log.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _log.editable = NO;
    _log.backgroundColor = [UIColor clearColor];
    _log.textColor = [UIColor colorWithRed:0.55 green:0.95 blue:0.55 alpha:1];
    _log.font = [UIFont fontWithName:@"Courier" size:12];
    if (!_log.font) _log.font = [UIFont systemFontOfSize:12];
    _log.text = @"";
    [_logPlate addSubview:_log];

    _closeBtn = [UIButton buttonWithType:UIButtonTypeCustom];
    _closeBtn.frame = CGRectMake(pad, btnY, w, btnH);
    _closeBtn.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleTopMargin;
    [_closeBtn setTitle:@"Close" forState:UIControlStateNormal];
    _closeBtn.enabled = NO;
    _closeBtn.alpha = 0.45f;
    StyleGlossyCapsule(_closeBtn, kIdleGrey, kIdleGreyLo);
    [_closeBtn addTarget:self action:@selector(closePressed)
       forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:_closeBtn];

    [self appendLog:[NSString stringWithFormat:@"Package: %@",
                     [_path lastPathComponent]]];
    [self startCreepTo:0.10f];
    [self layoutInstall];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutInstall];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (_finished || _started) return;
    if (![_path length]) {
        [self finishErr:@"UPDATE ERR no package path"];
        return;
    }
    _started = YES;
    NSString *path = [[_path retain] autorelease];
    [self appendLog:@"Starting install helper"];
    [_ctl updatePackageAtPath:path
                     progress:^(NSString *line) {
                         if (_finished) return;
                         if ([line hasPrefix:@"UPDATE STAGE "]) {
                             NSString *stage = [line substringFromIndex:13]; /* "update stage " */
                             [self applyStage:stage];
                         } else if ([line hasPrefix:@"UPDATE META version "]) {
/* prefix length is 20: "update meta version " */
                             NSString *ver = [line substringFromIndex:20];
                             [self appendLog:[NSString stringWithFormat:@"Version %@", ver]];
                         } else if ([line hasPrefix:@"UPDATE OK"]) {
/* completion path owns final state */
                         } else if ([line hasPrefix:@"UPDATE ERR"]) {
/* still show in log; completion finishes */
                             [self appendLog:line];
                         } else if ([line length]) {
                             [self appendLog:line];
                         }
                     }
                        reply:^(NSString *reply) {
                         if (_finished) return;
                         NSString *result = [reply stringByTrimmingCharactersInSet:
                                             [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                         if ([result hasPrefix:@"UPDATE OK"]) {
                             NSString *ver = result;
                             if ([result length] > 9)
                                 ver = [[result substringFromIndex:9]
                                        stringByTrimmingCharactersInSet:
                                        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                             [self finishOk:ver];
                         } else if ([result hasPrefix:@"UPDATE ERR"]) {
                             [self finishErr:result];
                         } else if ([result length]) {
                             [self finishErr:result];
                         } else {
                             [self finishErr:@"UPDATE ERR no response from senko-kick"];
                         }
                     }];
}

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)io {
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad)
        return YES;
    return UIInterfaceOrientationIsPortrait(io);
}

@end
