#import "main_vc_priv.h"

static void SenkoPlaceBehind(UIView *view, UIView *root) {
    if (!view || !root) return;
    if (view.superview != root) {
        [root insertSubview:view atIndex:0];
        return;
    }
    NSArray *views = root.subviews;
    if (![views count] || [views objectAtIndex:0] != view)
        [root insertSubview:view atIndex:0];
}

@implementation MainVC (Decor)

- (void)bringMainChromeToFront {
    if (_connectBtn) [self.view bringSubviewToFront:_connectBtn];
    if (_pingAllBtn) [self.view bringSubviewToFront:_pingAllBtn];
    if (_statusLabel) [self.view bringSubviewToFront:_statusLabel];
    UIView *title = [self.view viewWithTag:8001];
    UIView *gear = [self.view viewWithTag:8002];
    UIView *refresh = [self.view viewWithTag:8003];
    UIView *plus = [self.view viewWithTag:8004];
    if (title) [self.view bringSubviewToFront:title];
    if (_misideLogo) [self.view bringSubviewToFront:_misideLogo];
    if (gear) [self.view bringSubviewToFront:gear];
    if (refresh) [self.view bringSubviewToFront:refresh];
    if (plus) [self.view bringSubviewToFront:plus];
}

/* wallpaper only (glow is laid out with the connect button) */
- (void)layoutWallpaperStack {
    CGRect b = self.view.bounds;
    if (b.size.width < 1.0f || b.size.height < 1.0f)
        b = [[UIScreen mainScreen] bounds];

    if (_bgGrad) {
        _bgGrad.frame = b;
        if (_bgGrad.superlayer != self.view.layer)
            [self.view.layer insertSublayer:_bgGrad atIndex:0];
    }
    if (_misidePattern && !_misidePattern.hidden) {
        _misidePattern.frame = b;
        SenkoPlaceBehind(_misidePattern, self.view);
    }
    if (_frutigerBg && !_frutigerBg.hidden) {
        _frutigerBg.frame = b;
        SenkoPlaceBehind(_frutigerBg, self.view);
    }
    if (_ios26Bg && !_ios26Bg.hidden) {
        _ios26Bg.frame = b;
        SenkoPlaceBehind(_ios26Bg, self.view);
    }
}

/* soft halo centered on connect; size tracks button frame (incl. scale) */
- (void)layoutStatusGlow {
    if (!_statusWashHost || !_connectBtn) return;
    CGRect fr = _connectBtn.frame;
    if (fr.size.width < 1.0f || fr.size.height < 1.0f) return;
    CGFloat d = MAX(fr.size.width, fr.size.height) * 2.35f;
    if (d < 160.0f) d = 160.0f;
    _statusWashHost.bounds = CGRectMake(0, 0, d, d);
    _statusWashHost.center = _connectBtn.center;
    if (_statusWash)
        _statusWash.frame = _statusWashHost.bounds;
    if (_connectBtn.superview == self.view)
        [self.view insertSubview:_statusWashHost belowSubview:_connectBtn];
}

- (void)ensureStatusWash {
    if (_statusWashHost) return;
    _statusWashHost = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 200, 200)];
    _statusWashHost.userInteractionEnabled = NO;
    _statusWashHost.backgroundColor = [UIColor clearColor];
    _statusWashHost.opaque = NO;
    _statusWashHost.autoresizingMask = UIViewAutoresizingNone;
    _statusWashHost.tag = 9004;
    _statusWashHost.clipsToBounds = NO;
    _statusWash = [[CALayer layer] retain];
    _statusWash.name = @"statusWash";
    _statusWash.frame = _statusWashHost.bounds;
    _statusWash.contentsGravity = kCAGravityResize;
    _statusWash.opacity = 0.0f;
    [_statusWashHost.layer insertSublayer:_statusWash atIndex:0];
    [self.view insertSubview:_statusWashHost atIndex:0];
}

- (void)layoutMisideChrome {
    [self layoutWallpaperStack];
    if (_misideLogo && !_misideLogo.hidden) {
        UIView *title = [self.view viewWithTag:8001];
        CGRect b = self.view.bounds;
        CGFloat W = b.size.width;
        CGFloat top = GetTopOffset();
        BOOL pad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
        BOOL compact = (!pad && b.size.height <= 568.0f);
        CGFloat logoW = pad ? 160.0f : (compact ? 110.0f : 132.0f);
        CGFloat logoH = logoW * (84.0f / 240.0f);
        CGFloat titleBottom = title
            ? CGRectGetMaxY(title.frame)
            : (top + (compact ? 42.0f : 46.0f));
        _misideLogo.frame = CGRectMake((W - logoW) * 0.5f, titleBottom - 2.0f, logoW, logoH);
    }
}

- (void)syncMisideDecor {
    if (SenkoThemeIsMiside()) {
        if (!_misidePattern) {
            UIImage *img = [UIImage imageNamed:@"miside-bg.jpg"];
            if (!img) {
                NSString *p = [[NSBundle mainBundle] pathForResource:@"miside-bg" ofType:@"jpg"];
                if (p) img = [UIImage imageWithContentsOfFile:p];
            }
            if (img) {
                _misidePattern = [[UIImageView alloc] initWithImage:img];
                _misidePattern.tag = 9003;
                _misidePattern.contentMode = UIViewContentModeScaleAspectFill;
                _misidePattern.clipsToBounds = YES;
                _misidePattern.userInteractionEnabled = NO;
                _misidePattern.autoresizingMask =
                    UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
                [self.view insertSubview:_misidePattern atIndex:0];
            }
        }
        _misidePattern.hidden = NO;

        if (!_misideLogo) {
            UIImage *logo = [UIImage imageNamed:@"miside-logo.png"];
            if (!logo) {
                NSString *p = [[NSBundle mainBundle] pathForResource:@"miside-logo" ofType:@"png"];
                if (p) logo = [UIImage imageWithContentsOfFile:p];
            }
            if (logo) {
                _misideLogo = [[UIImageView alloc] initWithImage:logo];
                _misideLogo.tag = 8006;
                _misideLogo.contentMode = UIViewContentModeScaleAspectFit;
                _misideLogo.userInteractionEnabled = NO;
                _misideLogo.autoresizingMask =
                    UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin;
                [self.view addSubview:_misideLogo];
            }
        }
        if (_misideLogo) _misideLogo.hidden = NO;
        [self layoutMisideChrome];
        [self bringMainChromeToFront];
    } else {
        if (_misidePattern) _misidePattern.hidden = YES;
        if (_misideLogo) _misideLogo.hidden = YES;
    }
}

- (void)syncBoykisserField {
    if (SenkoThemeIsBoykisser()) {
        if (!_boyField) {
            _boyField = [[SenkoBoykisserField alloc] initWithFrame:self.view.bounds];
            _boyField.autoresizingMask =
                UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            _boyField.tag = 9002;
            _boyField.userInteractionEnabled = NO;
            [self.view addSubview:_boyField];
        }
        _boyField.frame = self.view.bounds;
/* flakes under controls; z-order keeps buttons tappable */
        UIView *well = [self.view viewWithTag:9001];
        if (_table)
            [self.view insertSubview:_boyField aboveSubview:_table];
        else if (well)
            [self.view insertSubview:_boyField aboveSubview:well];
        [self bringMainChromeToFront];
        [_boyField start];
    } else if (_boyField) {
        [_boyField stop];
    }
}

- (void)syncFrutigerDecor {
    if (SenkoThemeIsFrutigeraero()) {
        if (!_frutigerBg) {
            UIImage *img = [UIImage imageNamed:@"frutiger-bg.jpg"];
            if (!img) {
                NSString *p = [[NSBundle mainBundle] pathForResource:@"frutiger-bg" ofType:@"jpg"];
                if (p) img = [UIImage imageWithContentsOfFile:p];
            }
            if (img) {
                _frutigerBg = [[UIImageView alloc] initWithImage:img];
                _frutigerBg.tag = 9005;
                _frutigerBg.contentMode = UIViewContentModeScaleAspectFill;
                _frutigerBg.clipsToBounds = YES;
                _frutigerBg.userInteractionEnabled = NO;
                _frutigerBg.autoresizingMask =
                    UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
                [self.view insertSubview:_frutigerBg atIndex:0];
            }
        }
        if (_frutigerBg) {
            _frutigerBg.hidden = NO;
            _frutigerBg.frame = self.view.bounds;
        }
        [self bringMainChromeToFront];
    } else if (_frutigerBg) {
        _frutigerBg.hidden = YES;
    }
}

- (void)syncIos26Decor {
    if (SenkoThemeIsIos26()) {
        BOOL wantLight = SenkoThemeIsLight();
        if (!_ios26Bg || _ios26BgLight != wantLight) {
            NSString *name = wantLight ? @"ios26-bg-light" : @"ios26-bg-dark";
            UIImage *img = [UIImage imageNamed:[name stringByAppendingString:@".jpg"]];
            if (!img) {
                NSString *p = [[NSBundle mainBundle] pathForResource:name ofType:@"jpg"];
                if (p) img = [UIImage imageWithContentsOfFile:p];
            }
            if (img) {
                if (!_ios26Bg) {
                    _ios26Bg = [[UIImageView alloc] initWithImage:img];
                    _ios26Bg.tag = 9007;
                    _ios26Bg.contentMode = UIViewContentModeScaleAspectFill;
                    _ios26Bg.clipsToBounds = YES;
                    _ios26Bg.userInteractionEnabled = NO;
/* full-bleed photo: skip per-pixel blend of clear under it */
                    _ios26Bg.opaque = YES;
                    _ios26Bg.backgroundColor = wantLight
                        ? [UIColor colorWithWhite:0.92 alpha:1]
                        : [UIColor colorWithWhite:0.06 alpha:1];
                    _ios26Bg.autoresizingMask =
                        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
                    [self.view insertSubview:_ios26Bg atIndex:0];
                } else {
                    _ios26Bg.image = img;
                    _ios26Bg.backgroundColor = wantLight
                        ? [UIColor colorWithWhite:0.92 alpha:1]
                        : [UIColor colorWithWhite:0.06 alpha:1];
                }
                _ios26BgLight = wantLight;
            }
        }
        if (_ios26Bg) {
            _ios26Bg.hidden = NO;
            _ios26Bg.frame = self.view.bounds;
            _ios26Bg.opaque = YES;
        }
        [self bringMainChromeToFront];
    } else if (_ios26Bg) {
        _ios26Bg.hidden = YES;
    }
}

- (void)syncBubbleField {
    if (SenkoThemeIsFrutigeraero()) {
        if (!_bubbleField) {
            _bubbleField = [[SenkoBubbleField alloc] initWithFrame:self.view.bounds];
            _bubbleField.autoresizingMask =
                UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            _bubbleField.tag = 9006;
            _bubbleField.userInteractionEnabled = NO;
            [self.view addSubview:_bubbleField];
        }
        _bubbleField.frame = self.view.bounds;
        UIView *well = [self.view viewWithTag:9001];
        if (_table)
            [self.view insertSubview:_bubbleField aboveSubview:_table];
        else if (well)
            [self.view insertSubview:_bubbleField aboveSubview:well];
        [self bringMainChromeToFront];
        [_bubbleField start];
    } else if (_bubbleField) {
        [_bubbleField stop];
    }
}

/* glow key: connecting / connected / error */
- (NSString *)backgroundStatusKey {
    if ([_state isEqualToString:@"connecting"]) return @"connecting";
    if ([_state isEqualToString:@"connected"]) return @"connected";
    if ([_state isEqualToString:@"error"]) return @"error";
    if (_lastErr && [_lastErr length] &&
        ![_state isEqualToString:@"connected"] &&
        ![_state isEqualToString:@"connecting"])
        return @"error";
    return @"idle";
}

- (void)applyBackgroundForCurrentState:(BOOL)animated {
    [self ensureStatusWash];
    CGRect b = self.view.bounds;
    if (b.size.width < 1.0f || b.size.height < 1.0f)
        b = [[UIScreen mainScreen] bounds];
    NSString *key = [self backgroundStatusKey];

    if (_bgGrad) {
        _bgGrad.frame = b;
/* pure theme wallpaper - no full-screen status tint */
        SenkoApplyBackgroundGradient(_bgGrad);
    }
    [self layoutWallpaperStack];
    [self layoutStatusGlow];
    if (_statusWash) {
        CGFloat side = _statusWashHost ? _statusWashHost.bounds.size.width : 200.0f;
        SenkoApplyStatusWash(_statusWash, key, side, animated);
        _statusWashHost.hidden = NO;
    }
    if (_statusWashHost && _connectBtn && _connectBtn.superview == self.view)
        [self.view insertSubview:_statusWashHost belowSubview:_connectBtn];
}

/* recolor existing ui after theme change */

@end
