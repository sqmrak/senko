#import "main_vc_priv.h"

/* ios 5 has no blur view, so soften the wallpaper at reduced size and mix
   it at half strength without keeping a full-size decoded blur bitmap */
static UIImage *SenkoSoftWallpaperImage(UIImage *source) {
    if (!source || source.size.width < 1.0f || source.size.height < 1.0f) return nil;
    CGSize small = CGSizeMake(MAX(1.0f, floorf(source.size.width / 16.0f)),
                              MAX(1.0f, floorf(source.size.height / 16.0f)));
    UIGraphicsBeginImageContextWithOptions(small, YES, 1.0f);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    if (!ctx) {
        UIGraphicsEndImageContext();
        return nil;
    }
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    [source drawInRect:CGRectMake(0.0f, 0.0f, small.width, small.height)];
    UIImage *soft = [UIGraphicsGetImageFromCurrentImageContext() retain];
    UIGraphicsEndImageContext();
    return [soft autorelease];
}

static void SenkoAddSoftWallpaperOverlay(UIImageView *wallpaper, UIImage *source) {
    UIImage *soft = SenkoSoftWallpaperImage(source);
    if (!soft) return;
    UIImageView *blur = [[[UIImageView alloc] initWithFrame:wallpaper.bounds] autorelease];
    blur.image = soft;
    blur.contentMode = UIViewContentModeScaleAspectFill;
    blur.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                            UIViewAutoresizingFlexibleHeight;
    blur.alpha = 0.5f;
    [wallpaper addSubview:blur];
}

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
    /* particle fields do not receive touches and sit under the controls, so
       the home screen and the picker stay tappable through them */
    if (_home && _boyField) [self.view insertSubview:_boyField belowSubview:_home];
    if (_home && _bubbleField)
        [self.view insertSubview:_bubbleField belowSubview:_home];
    if (_home) [self.view bringSubviewToFront:_home];
    if (_picker) [self.view bringSubviewToFront:_picker];
    /* the detail sheet is modal over everything the screen draws */
    if (_sheet.superview == self.view) [self.view bringSubviewToFront:_sheet];
    if (_busyOverlay.superview == self.view && _busyOverlay.alpha > 0.0f)
        [self.view bringSubviewToFront:_busyOverlay];
}

/* wallpaper views behind every control */
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
                SenkoAddSoftWallpaperOverlay(_misidePattern, img);
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
        [self bringMainChromeToFront];
        [_boyField start];
    } else if (_boyField) {
        [_boyField stop];
    }
}

- (void)syncFrutigerDecor {
    if (SenkoThemeIsFrutigeraero()) {
        CGSize room = self.view.bounds.size;
        if (room.width < 1.0f || room.height < 1.0f)
            room = [[UIScreen mainScreen] bounds].size;
/* the day sky and the night city are separate photos, and the night one comes
   in a portrait and a wide cut so the planet stays on screen on a phone held
   upright. a switch replaces the view instead of repainting it: the soft
   overlay holds a blurred copy of the old photo */
        NSString *name = SenkoThemeIsLight() ? @"frutiger-bg"
            : (room.width > room.height ? @"frutiger-bg-dark-wide" : @"frutiger-bg-dark");
        if (_frutigerBg && ![_frutigerBgName isEqualToString:name]) {
            [_frutigerBg removeFromSuperview];
            [_frutigerBg release];
            _frutigerBg = nil;
        }
        if (!_frutigerBg) {
            UIImage *img = [UIImage imageNamed:[name stringByAppendingString:@".jpg"]];
            if (!img) {
                NSString *p = [[NSBundle mainBundle] pathForResource:name ofType:@"jpg"];
                if (p) img = [UIImage imageWithContentsOfFile:p];
            }
            if (img) {
                [_frutigerBgName release];
                _frutigerBgName = [name copy];
                _frutigerBg = [[UIImageView alloc] initWithImage:img];
                _frutigerBg.tag = 9005;
                _frutigerBg.contentMode = UIViewContentModeScaleAspectFill;
                _frutigerBg.clipsToBounds = YES;
                _frutigerBg.userInteractionEnabled = NO;
                _frutigerBg.autoresizingMask =
                    UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
                SenkoAddSoftWallpaperOverlay(_frutigerBg, img);
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
        [self bringMainChromeToFront];
        [_bubbleField start];
    } else if (_bubbleField) {
        [_bubbleField stop];
    }
}

/* the orbit colour: connecting, connected, error or idle */
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
    (void)animated;
    CGRect b = self.view.bounds;
    if (b.size.width < 1.0f || b.size.height < 1.0f)
        b = [[UIScreen mainScreen] bounds];
    if (_bgGrad) {
        _bgGrad.frame = b;
/* pure theme wallpaper, the state lives in the orbit */
        SenkoApplyBackgroundGradient(_bgGrad);
    }
    [self layoutWallpaperStack];
}


@end
