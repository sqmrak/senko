#import "main_vc_priv.h"

/* the theme gradient is the ground everything else stands on, so wallpaper goes
   directly above it rather than at index 0 */
static NSUInteger SenkoWallpaperIndex(UIView *root) {
    NSArray *views = root.subviews;
    if ([views count] && [(UIView *)[views objectAtIndex:0] tag] == kSenkoBackdropTag)
        return 1;
    return 0;
}

static void SenkoPlaceBehind(UIView *view, UIView *root) {
    if (!view || !root) return;
    NSUInteger want = SenkoWallpaperIndex(root);
    if (view.superview != root) {
        [root insertSubview:view atIndex:want];
        return;
    }
    NSArray *views = root.subviews;
    if ([views count] <= want || [views objectAtIndex:want] != view)
        [root insertSubview:view atIndex:want];
}

@implementation MainVC (Decor)

- (void)bringMainChromeToFront {
    /* the connect pill, the check pill, and the detail line live inside the
       status card now, so raising the card raises all three at once */
    if (_statusCard) [self.view bringSubviewToFront:_statusCard];
    /* particle fields do not receive touches, so they can cross the status card
       without taking the connect and ping controls out of the responder chain */
    if (_boyField && !_boyField.hidden) [self.view bringSubviewToFront:_boyField];
    if (_bubbleField && !_bubbleField.hidden) [self.view bringSubviewToFront:_bubbleField];
    if (_ui.title) [self.view bringSubviewToFront:_ui.title];
    if (_ui.gear) [self.view bringSubviewToFront:_ui.gear];
    if (_ui.refresh) [self.view bringSubviewToFront:_ui.refresh];
    if (_ui.plus) [self.view bringSubviewToFront:_ui.plus];
    /* the detail sheet is modal over everything the screen draws */
    if (_sheet.superview == self.view) [self.view bringSubviewToFront:_sheet];
}

/* wallpaper only (glow is laid out with the connect button) */
- (void)layoutWallpaperStack {
    CGRect b = self.view.bounds;
    if (b.size.width < 1.0f || b.size.height < 1.0f)
        b = [[UIScreen mainScreen] bounds];

    UIView *backdrop = [self.view viewWithTag:kSenkoBackdropTag];
    if (backdrop) {
        if (!CGRectEqualToRect(backdrop.frame, b)) backdrop.frame = b;
        if ([self.view.subviews count] &&
            [self.view.subviews objectAtIndex:0] != backdrop)
            [self.view sendSubviewToBack:backdrop];
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

- (void)layoutStatusGlow {
    if (!_statusWashHost || !_statusCard || !_statusCard->orb)
        return;
    if (_statusWashHost.superview != _statusCard) {
        if (_statusCard->ring)
            [_statusCard insertSubview:_statusWashHost belowSubview:_statusCard->ring];
        else
            [_statusCard insertSubview:_statusWashHost atIndex:0];
    }
    CGFloat orbSide = _statusCard->orb.bounds.size.width;
    if (orbSide < 1.0f) orbSide = 44.0f;
    CGFloat d = orbSide * 2.8f;
    if (d < 100.0f) d = 100.0f;
    if (fabsf((float)(_statusWashHost.bounds.size.width - d)) > 0.5f) {
        _statusWashHost.bounds = CGRectMake(0, 0, d, d);
/* the wash is a bare layer, so a plain frame write starts core animation's
   default quarter second action. the orb resizes on every frame of the card
   collapse, which restarted that action on every frame and left the glow
   trailing the icon for as long as the list kept moving */
        SenkoSetLayerFrame(_statusWash, _statusWashHost.bounds);
    }
    _statusWashHost.center = _statusCard->orb.center;
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
}

- (void)layoutMisideChrome {
    [self layoutWallpaperStack];
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
                [self.view insertSubview:_misidePattern
                                  atIndex:SenkoWallpaperIndex(self.view)];
            }
        }
        _misidePattern.hidden = NO;
        [self layoutMisideChrome];
        [self bringMainChromeToFront];
    } else {
        if (_misidePattern) _misidePattern.hidden = YES;
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
                [self.view insertSubview:_frutigerBg
                                  atIndex:SenkoWallpaperIndex(self.view)];
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
                    [self.view insertSubview:_ios26Bg
                                      atIndex:SenkoWallpaperIndex(self.view)];
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
    [self layoutStatusGlow];
}


@end
