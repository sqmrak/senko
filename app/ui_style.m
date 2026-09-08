#import "ui_theme.h"
#include <math.h>
#include <objc/runtime.h>
#include <objc/message.h>

enum { kSenkoFrostTag = 9107 };

static char kSenkoRasterKey;

/* the veil is the previous screen held still while the palette swaps under it */
static UIView *SenkoThemeVeilForWindow(UIWindow *w) {
    /* snapshotViewAfterScreenUpdates: landed in ios 7 and copies the existing
       render tree instead of rasterizing it again. on a modern phone the
       renderInContext: path allocates a full retina bitmap of the whole window
       on the main thread, and it also flattens a UIVisualEffectView into an
       opaque tile */
    if ([w respondsToSelector:@selector(snapshotViewAfterScreenUpdates:)]) {
        UIView *snap = ((id (*)(id, SEL, BOOL))objc_msgSend)(
            w, @selector(snapshotViewAfterScreenUpdates:), NO);
        if (snap) return [snap retain];
    }

    CGSize size = w.bounds.size;
    UIGraphicsBeginImageContextWithOptions(size, YES, 0.0f);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    if (!ctx) {
        UIGraphicsEndImageContext();
        return nil;
    }
    [w.layer renderInContext:ctx];
    UIImage *shot = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    if (!shot) return nil;
    return [[UIImageView alloc] initWithImage:shot];
}

void SenkoThemeCrossfadeWindows(void) {
    /* block animation api landed in ios 4, and this runs on theme switches
       only, never on live palette edits */
    if (![UIView respondsToSelector:@selector(animateWithDuration:animations:completion:)])
        return;
    UIWindow *w = [[UIApplication sharedApplication] keyWindow];
    if (!w) return;
    CGSize size = w.bounds.size;
    if (size.width < 1.0f || size.height < 1.0f) return;

    UIView *veil = SenkoThemeVeilForWindow(w);
    if (!veil) return;
    veil.frame = w.bounds;
    veil.userInteractionEnabled = NO;
    veil.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [w addSubview:veil];
    [UIView animateWithDuration:0.30
                     animations:^{ veil.alpha = 0.0f; }
                     completion:^(BOOL done) {
                         (void)done;
                         [veil removeFromSuperview];
                         [veil release];
                     }];
}

/* runtime lookup keeps font-weight symbols from breaking ios 5 launches */
static UIFont *SenkoSFBold(CGFloat size) {
    if ([UIFont respondsToSelector:@selector(systemFontOfSize:weight:)]) {
        UIFont *f = ((UIFont * (*)(id, SEL, CGFloat, CGFloat))objc_msgSend)(
            [UIFont class], @selector(systemFontOfSize:weight:), size, (CGFloat)0.4);
        if (f) return f;
    }
    return [UIFont boldSystemFontOfSize:size];
}

static UIFont *SenkoSFSemibold(CGFloat size) {
    if ([UIFont respondsToSelector:@selector(systemFontOfSize:weight:)]) {
        UIFont *f = ((UIFont * (*)(id, SEL, CGFloat, CGFloat))objc_msgSend)(
            [UIFont class], @selector(systemFontOfSize:weight:), size, (CGFloat)0.3);
        if (f) return f;
    }
    return [UIFont boldSystemFontOfSize:size];
}

UIFont *SenkoFontTitle(CGFloat size) {
    if (SenkoThemeIsIos26())
        return SenkoSFBold(size);
    UIFont *f = [UIFont fontWithName:@"HelveticaNeue-UltraLight" size:size];
    if (!f) f = [UIFont fontWithName:@"HelveticaNeue-Thin" size:size];
    if (!f) f = [UIFont fontWithName:@"HelveticaNeue-Light" size:size];
    if (!f) f = [UIFont systemFontOfSize:size];
    return f;
}

UIFont *SenkoFontBody(CGFloat size, BOOL semibold) {
    if (SenkoThemeIsIos26())
        return semibold ? SenkoSFBold(size) : SenkoSFSemibold(size);
    if (semibold) {
        UIFont *f = [UIFont fontWithName:@"HelveticaNeue-Medium" size:size];
        if (!f) f = [UIFont boldSystemFontOfSize:size];
        return f;
    }
    return [UIFont systemFontOfSize:size];
}


void SenkoStyleIos16ListWell(UIView *well) {
    if (!well) return;
    SenkoRemoveFrost(well);
    well.backgroundColor = [UIColor clearColor];
    well.layer.borderWidth = 0;
    well.layer.borderColor = [UIColor clearColor].CGColor;
    well.layer.cornerRadius = 0;
    well.layer.shadowOpacity = 0;
    well.clipsToBounds = NO;
    for (CALayer *layer in well.layer.sublayers) {
        if ([layer.name isEqualToString:@"wellGrad"] &&
            [layer isKindOfClass:[CAGradientLayer class]]) {
            ((CAGradientLayer *)layer).hidden = YES;
            ((CAGradientLayer *)layer).colors = [NSArray arrayWithObjects:
                (id)[UIColor clearColor].CGColor,
                (id)[UIColor clearColor].CGColor, nil];
        }
    }
}

void SenkoRemoveFrost(UIView *host) {
    if (!host) return;
    UIView *old = [host viewWithTag:kSenkoFrostTag];
    if (old) [old removeFromSuperview];
}

static UIColor *FrostTintColor(BOOL lite) {
    BOOL light = SenkoThemeIsLight();
    if (SenkoThemeIsIos26()) {
        return light
            ? [UIColor colorWithWhite:1.0 alpha:lite ? 0.62f : 0.48f]
            : [UIColor colorWithWhite:1.0 alpha:lite ? 0.14f : 0.08f];
    }
    if (lite)
        return light
            ? [UIColor colorWithRed:0.94 green:0.95 blue:0.98 alpha:0.88]
            : [UIColor colorWithRed:0.14 green:0.16 blue:0.22 alpha:0.90];
    return light
        ? [UIColor colorWithRed:0.92 green:0.94 blue:0.98 alpha:0.72]
        : [UIColor colorWithRed:0.12 green:0.14 blue:0.20 alpha:0.78];
}

void SenkoInstallFrostLite(UIView *host) {
    if (!host) return;
    if (!SenkoThemeUsesFrost() && !SenkoThemeIsIos26()) {
        SenkoRemoveFrost(host);
        return;
    }
    UIView *old = [host viewWithTag:kSenkoFrostTag];
    if (old && ![old isKindOfClass:[UIToolbar class]] &&
        ![NSStringFromClass([old class]) isEqualToString:@"UIVisualEffectView"]) {
        old.frame = host.bounds;
        old.backgroundColor = FrostTintColor(YES);
        old.layer.cornerRadius = host.layer.cornerRadius;
        return;
    }
    SenkoRemoveFrost(host);
    UIView *v = [[[UIView alloc] initWithFrame:host.bounds] autorelease];
    v.tag = kSenkoFrostTag;
    v.userInteractionEnabled = NO;
    v.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    v.backgroundColor = FrostTintColor(YES);
    v.clipsToBounds = YES;
    v.layer.cornerRadius = host.layer.cornerRadius;
    [host insertSubview:v atIndex:0];
}

static int gFrostHasVE = -1;

static int SenkoFrostHasVisualEffect(void) {
    if (gFrostHasVE < 0)
        gFrostHasVE = (NSClassFromString(@"UIVisualEffectView") &&
                       NSClassFromString(@"UIBlurEffect")) ? 1 : 0;
    return gFrostHasVE;
}

/* ios 6 and 7 need a wash because UIVisualEffectView does not exist */
void SenkoInstallFrost(UIView *host) {
    if (!host) return;
    if (!SenkoThemeIsIos26()) {
        SenkoInstallFrostLite(host);
        return;
    }

    CGFloat r = host.layer.cornerRadius;
    BOOL light = SenkoThemeIsLight();
    UIColor *washColor = light
        ? [UIColor colorWithWhite:1.0 alpha:0.28]
        : [UIColor colorWithWhite:1.0 alpha:0.10];
    UIView *old = [host viewWithTag:kSenkoFrostTag];

    if (SenkoFrostHasVisualEffect()) {
        Class effectViewCls = NSClassFromString(@"UIVisualEffectView");
        Class blurCls = NSClassFromString(@"UIBlurEffect");
        NSInteger style = light ? 0 : 2;
        id effect = ((id (*)(id, SEL, NSInteger))objc_msgSend)(
            blurCls, NSSelectorFromString(@"effectWithStyle:"), style);
        if (effect) {
            if (old && [old isKindOfClass:effectViewCls]) {
                if (!CGRectEqualToRect(old.frame, host.bounds))
                    old.frame = host.bounds;
                old.layer.cornerRadius = r;
                old.clipsToBounds = YES;
                if ([old respondsToSelector:NSSelectorFromString(@"setEffect:")])
                    ((void (*)(id, SEL, id))objc_msgSend)(
                        old, NSSelectorFromString(@"setEffect:"), effect);
                for (UIView *sub in old.subviews) {
                    if (sub.tag == 9108)
                        sub.backgroundColor = washColor;
                }
                return;
            }
            SenkoRemoveFrost(host);
            UIView *v = ((id (*)(id, SEL, id))objc_msgSend)(
                [effectViewCls alloc],
                NSSelectorFromString(@"initWithEffect:"),
                effect);
            v.tag = kSenkoFrostTag;
            v.userInteractionEnabled = NO;
            v.frame = host.bounds;
            v.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                 UIViewAutoresizingFlexibleHeight;
            v.clipsToBounds = YES;
            v.layer.cornerRadius = r;
            UIView *tint = [[[UIView alloc] initWithFrame:v.bounds] autorelease];
            tint.tag = 9108;
            tint.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                    UIViewAutoresizingFlexibleHeight;
            tint.userInteractionEnabled = NO;
            tint.backgroundColor = washColor;
            [v addSubview:tint];
            [host insertSubview:v atIndex:0];
            [v release];
            return;
        }
    }

/* reusing the wash prevents stacked alpha layers after theme changes */
    if (old && ![old isKindOfClass:[UIToolbar class]]) {
        if (!CGRectEqualToRect(old.frame, host.bounds))
            old.frame = host.bounds;
        old.layer.cornerRadius = r;
        old.clipsToBounds = YES;
        old.backgroundColor = washColor;
        old.opaque = NO;
        return;
    }
    SenkoRemoveFrost(host);
    UIView *wash = [[UIView alloc] initWithFrame:host.bounds];
    wash.tag = kSenkoFrostTag;
    wash.userInteractionEnabled = NO;
    wash.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                            UIViewAutoresizingFlexibleHeight;
    wash.backgroundColor = washColor;
    wash.opaque = NO;
    wash.clipsToBounds = YES;
    wash.layer.cornerRadius = r;
    [host insertSubview:wash atIndex:0];
    [wash release];
}

static UIColor *DarkChromeInk(void) {
    return [UIColor colorWithRed:1.00 green:0.92 blue:0.82 alpha:1.0];
}
static UIColor *DarkChromeMuted(void) {
    return [UIColor colorWithRed:0.78 green:0.72 blue:0.64 alpha:1.0];
}
static UIColor *DarkChromeAccent(void) {
    return [UIColor colorWithRed:1.00 green:0.58 blue:0.14 alpha:1.0];
}

static void ApplyPaperShadow(UILabel *label, CGFloat darkA, CGFloat lightA) {
    if (!label) return;
    if (SenkoThemeIsFlat()) {
        label.shadowColor = nil;
        label.shadowOffset = CGSizeZero;
        return;
    }
    if (SenkoThemeIsLight()) {
        label.shadowColor = [UIColor colorWithWhite:1 alpha:lightA];
        label.shadowOffset = CGSizeMake(0, 1);
    } else {
        label.shadowColor = [UIColor colorWithWhite:0 alpha:darkA];
        label.shadowOffset = CGSizeMake(0, -1);
    }
}

static void ApplyCutInShadow(UILabel *label, CGFloat alpha) {
    if (!label) return;
    label.shadowColor = [UIColor colorWithWhite:0 alpha:alpha];
    label.shadowOffset = CGSizeMake(0, -1);
}

void SenkoStyleInkLabel(UILabel *label) {
    if (!label) return;
    label.textColor = kInk;
    ApplyPaperShadow(label, 0.70f, 0.50f);
}

void SenkoStyleMutedLabel(UILabel *label) {
    if (!label) return;
    label.textColor = kInkMuted;
    if (SenkoThemeIsLight() && !SenkoThemeIsFlat()) {
        label.shadowColor = [UIColor colorWithWhite:1 alpha:0.35f];
        label.shadowOffset = CGSizeMake(0, 1);
    } else {
        label.shadowColor = nil;
        label.shadowOffset = CGSizeZero;
    }
}

void SenkoStyleAccentLabel(UILabel *label) {
    if (!label) return;
    label.textColor = kAccentBlue;
    ApplyPaperShadow(label, 0.65f, 0.40f);
}

void SenkoStyleInkOnDark(UILabel *label) {
    if (!label) return;
    label.textColor = DarkChromeInk();
    ApplyCutInShadow(label, 0.70f);
}

void SenkoStyleMutedOnDark(UILabel *label) {
    if (!label) return;
    label.textColor = DarkChromeMuted();
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
}

void SenkoStyleAccentOnDark(UILabel *label) {
    if (!label) return;
    label.textColor = DarkChromeAccent();
    ApplyCutInShadow(label, 0.65f);
}

void SenkoStyleChromeTitle(UIButton *button) {
    if (!button) return;
    [button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    if (SenkoThemeIsFlat()) {
        button.titleLabel.shadowColor = nil;
        button.titleLabel.shadowOffset = CGSizeZero;
    } else {
        button.titleLabel.shadowColor = [UIColor colorWithWhite:0 alpha:0.45];
        button.titleLabel.shadowOffset = CGSizeMake(0, -1);
    }
}

void SenkoStyleGlyphOnDark(UIButton *button) {
    if (!button) return;
    [button setTitleColor:DarkChromeInk() forState:UIControlStateNormal];
    button.titleLabel.shadowColor = [UIColor colorWithWhite:0 alpha:0.70f];
    button.titleLabel.shadowOffset = CGSizeMake(0, -1);
}

void SenkoStylePaperGlyph(UIButton *button) {
    if (!button) return;
    [button setTitleColor:kAccentBlue forState:UIControlStateNormal];
    if (SenkoThemeIsFlat()) {
        button.titleLabel.shadowColor = nil;
        button.titleLabel.shadowOffset = CGSizeZero;
    } else if (SenkoThemeIsLight()) {
        button.titleLabel.shadowColor = [UIColor colorWithWhite:1 alpha:0.45f];
        button.titleLabel.shadowOffset = CGSizeMake(0, 1);
    } else {
        button.titleLabel.shadowColor = [UIColor colorWithWhite:0 alpha:0.70f];
        button.titleLabel.shadowOffset = CGSizeMake(0, -1);
    }
}

void SenkoFillSectionGradient(CAGradientLayer *g) {
    if (!g) return;
    if (SenkoThemeIsMiside()) {
        g.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithRed:0.22 green:0.10 blue:0.28 alpha:0.94].CGColor,
                    (id)[UIColor colorWithRed:0.14 green:0.06 blue:0.20 alpha:0.94].CGColor, nil];
        g.opacity = 1.0f;
    } else if (SenkoThemeIsBoykisser()) {
        if (SenkoThemeIsLight()) {
            g.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithRed:1.00 green:0.88 blue:0.93 alpha:0.92].CGColor,
                        (id)[UIColor colorWithRed:1.00 green:0.78 blue:0.88 alpha:0.92].CGColor, nil];
        } else {
/* the plate keeps its saturation so the header still sits above the cards the
   way the pink plate does on paper */
            g.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithRed:0.310 green:0.110 blue:0.200 alpha:0.94].CGColor,
                        (id)[UIColor colorWithRed:0.185 green:0.058 blue:0.125 alpha:0.94].CGColor, nil];
        }
        g.opacity = 1.0f;
    } else if (SenkoThemeIsFrutigeraero()) {
        g.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithRed:0.55 green:0.88 blue:1.00 alpha:0.95].CGColor,
                    (id)[UIColor colorWithRed:0.20 green:0.68 blue:0.92 alpha:0.95].CGColor, nil];
        g.opacity = 1.0f;
    } else if (SenkoThemeIsIos26()) {
        if (SenkoThemeIsLight()) {
            g.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithWhite:1.0 alpha:0.36].CGColor,
                        (id)[UIColor colorWithWhite:1.0 alpha:0.14].CGColor, nil];
        } else {
            g.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithWhite:1.0 alpha:0.16].CGColor,
                        (id)[UIColor colorWithWhite:1.0 alpha:0.05].CGColor, nil];
        }
        g.opacity = 1.0f;
    } else if (SenkoThemeIsIos16()) {
        if (SenkoThemeIsLight()) {
            g.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithWhite:1.0 alpha:0.72].CGColor,
                        (id)[UIColor colorWithWhite:1.0 alpha:0.58].CGColor, nil];
        } else {
            g.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithWhite:1.0 alpha:0.14].CGColor,
                        (id)[UIColor colorWithWhite:1.0 alpha:0.08].CGColor, nil];
        }
        g.opacity = 1.0f;
    } else if (SenkoThemeIsFlat()) {
        UIColor *c = SenkoThemeIsLight()
            ? [UIColor colorWithRed:0.95 green:0.96 blue:0.99 alpha:0.35]
            : [UIColor colorWithRed:0.14 green:0.16 blue:0.22 alpha:0.40];
        g.colors = [NSArray arrayWithObjects:(id)c.CGColor, (id)c.CGColor, nil];
        g.opacity = 1.0f;
    } else if (SenkoThemeIsLight()) {
        g.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithRed:0.97 green:0.93 blue:0.86 alpha:1].CGColor,
                    (id)[UIColor colorWithRed:0.90 green:0.84 blue:0.74 alpha:1].CGColor, nil];
        g.opacity = 1.0f;
    } else {
        g.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithRed:0.22 green:0.14 blue:0.06 alpha:1].CGColor,
                    (id)[UIColor colorWithRed:0.09 green:0.06 blue:0.03 alpha:1].CGColor, nil];
        g.opacity = 1.0f;
    }
}

void SenkoStyleSectionPlate(UIView *plate) {
    if (!plate) return;
    CGFloat r = SenkoThemeCardRadius();
    plate.layer.cornerRadius = r;
    if (SenkoThemeIsMiside()) {
        SenkoRemoveFrost(plate);
        plate.backgroundColor = [UIColor clearColor];
        plate.layer.borderWidth = 0.5f;
        plate.layer.borderColor =
            [UIColor colorWithRed:1.0 green:0.40 blue:0.72 alpha:0.35].CGColor;
        plate.layer.shadowOpacity = 0.0f;
        plate.clipsToBounds = YES;
    } else if (SenkoThemeIsBoykisser()) {
        SenkoRemoveFrost(plate);
        plate.backgroundColor = [UIColor clearColor];
        plate.layer.borderWidth = 0.5f;
        plate.layer.borderColor =
            [UIColor colorWithRed:1.0 green:0.50 blue:0.72 alpha:0.45].CGColor;
        plate.layer.shadowOpacity = 0.0f;
        plate.layer.shadowRadius = 0;
        plate.clipsToBounds = YES;
    } else if (SenkoThemeIsFrutigeraero()) {
        SenkoRemoveFrost(plate);
        plate.backgroundColor = [UIColor clearColor];
        plate.layer.borderWidth = 0.5f;
        plate.layer.borderColor =
            [UIColor colorWithRed:0.20 green:0.75 blue:0.95 alpha:0.50].CGColor;
        plate.layer.shadowOpacity = 0.12f;
        plate.layer.shadowRadius = 3.0f;
        plate.layer.shadowOffset = CGSizeMake(0, 1);
        plate.clipsToBounds = YES;
    } else if (SenkoThemeIsIos26()) {
        BOOL light = SenkoThemeIsLight();
        plate.backgroundColor = [UIColor clearColor];
        plate.opaque = NO;
        plate.layer.borderWidth = 0.5f;
        plate.layer.borderColor = light
            ? [UIColor colorWithWhite:1 alpha:0.70].CGColor
            : [UIColor colorWithWhite:1 alpha:0.30].CGColor;
        plate.clipsToBounds = YES;
        plate.layer.masksToBounds = YES;
        plate.layer.shadowOpacity = 0.0f;
        plate.layer.shadowPath = nil;
/* a rasterized layer that holds a live UIVisualEffectView is re-rendered
   offscreen on every blur update, which pins the gpu and starves the rest of
   the system; the glass caches itself, so the plate must not rasterize */
        plate.layer.shouldRasterize = NO;
/* per-row blur overwhelms old gpus, so frost stays outside scrolling lists */
        SenkoInstallFrost(plate);
    } else if (SenkoThemeIsIos16()) {
        SenkoRemoveFrost(plate);
        plate.backgroundColor = [UIColor clearColor];
        plate.layer.borderWidth = 0;
        plate.layer.borderColor = [UIColor clearColor].CGColor;
        plate.clipsToBounds = YES;
        plate.layer.masksToBounds = NO;
        plate.layer.shadowColor = [UIColor blackColor].CGColor;
        plate.layer.shadowOpacity = SenkoThemeIsLight() ? 0.10f : 0.28f;
        plate.layer.shadowRadius = 6.0f;
        plate.layer.shadowOffset = CGSizeMake(0, 2);
        UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:plate.bounds
                                                        cornerRadius:r];
        plate.layer.shadowPath = path.CGPath;
        plate.clipsToBounds = NO;
        plate.layer.masksToBounds = NO;
    } else if (SenkoThemeIsFlat()) {
        plate.backgroundColor = [UIColor clearColor];
        plate.layer.borderWidth = 0.5f;
        plate.layer.borderColor = SenkoThemeIsLight()
            ? [UIColor colorWithRed:0.70 green:0.74 blue:0.82 alpha:0.40].CGColor
            : [UIColor colorWithWhite:1 alpha:0.10].CGColor;
        plate.layer.shadowOpacity = 0.0f;
        plate.layer.shadowRadius = 0;
        plate.clipsToBounds = YES;
        SenkoInstallFrostLite(plate);
    } else {
        SenkoRemoveFrost(plate);
        plate.layer.borderWidth = 0.5f;
        if (SenkoThemeIsLight()) {
            plate.layer.borderColor =
                [UIColor colorWithRed:0.85 green:0.55 blue:0.28 alpha:0.35].CGColor;
            plate.layer.shadowOpacity = 0.12f;
        } else {
            plate.layer.borderColor =
                [UIColor colorWithRed:1 green:0.55 blue:0.16 alpha:0.30].CGColor;
            plate.layer.shadowOpacity = 0.20f;
        }
    }
}

/* the on-dark pair is the fixed ios6 cream, which fights the pink plate, so the
   boykisser header takes its own palette ink */
void SenkoStyleSectionTitle(UILabel *label) {
    if (!label) return;
    if (SenkoThemeIsFlat() || SenkoThemeIsLight() || SenkoThemeIsBoykisser())
        SenkoStyleInkLabel(label);
    else
        SenkoStyleInkOnDark(label);
}

void SenkoStyleSectionMeta(UILabel *label) {
    if (!label) return;
    if (SenkoThemeIsFlat() || SenkoThemeIsLight() || SenkoThemeIsBoykisser())
        SenkoStyleMutedLabel(label);
    else
        SenkoStyleMutedOnDark(label);
}

void SenkoStyleSectionGlyph(UIButton *button) {
    if (!button) return;
    if (SenkoThemeIsMiside() || SenkoThemeIsBoykisser()) {
        UIColor *pink = [UIColor colorWithRed:1.00 green:0.42 blue:0.72 alpha:1.0];
        [button setTitleColor:pink forState:UIControlStateNormal];
        [button setTitleColor:[UIColor colorWithRed:0.90 green:0.28 blue:0.58 alpha:1.0]
                     forState:UIControlStateHighlighted];
        button.titleLabel.shadowColor = nil;
        button.titleLabel.shadowOffset = CGSizeZero;
    } else if (SenkoThemeIsFlat()) {
        [button setTitleColor:kAccentBlue forState:UIControlStateNormal];
        button.titleLabel.shadowColor = nil;
        button.titleLabel.shadowOffset = CGSizeZero;
    } else if (SenkoThemeIsLight()) {
        [button setTitleColor:kInk forState:UIControlStateNormal];
        button.titleLabel.shadowColor = [UIColor colorWithWhite:1 alpha:0.50f];
        button.titleLabel.shadowOffset = CGSizeMake(0, 1);
    } else {
        SenkoStyleGlyphOnDark(button);
    }
}

void SenkoStyleTerminalPlate(UIView *plate) {
    if (!plate) return;
    plate.layer.borderWidth = 0;
    plate.layer.borderColor = [UIColor clearColor].CGColor;
    plate.layer.shadowOpacity = 0.0f;
    plate.layer.shadowPath = nil;
    plate.clipsToBounds = YES;
    if (SenkoThemeUsesFrost()) {
        plate.backgroundColor = [UIColor clearColor];
        SenkoInstallFrost(plate);
        plate.layer.borderWidth = 0;
    } else {
        SenkoRemoveFrost(plate);
        if (SenkoThemeIsLight())
            plate.backgroundColor = kWell;
        else
            plate.backgroundColor = [UIColor colorWithRed:0.08 green:0.09 blue:0.10 alpha:1.0];
    }
}

void SenkoStyleTerminalText(UITextView *tv) {
    if (!tv) return;
    if ((SenkoThemeIsFlat()) && SenkoThemeIsLight()) {
        tv.textColor = [UIColor colorWithRed:0.20 green:0.20 blue:0.22 alpha:1.0];
        tv.backgroundColor = [UIColor clearColor];
    } else if ((SenkoThemeIsFlat()) && !SenkoThemeIsLight()) {
        tv.textColor = [UIColor colorWithRed:0.70 green:0.95 blue:0.70 alpha:1.0];
        tv.backgroundColor = [UIColor clearColor];
    } else if (SenkoThemeIsLight()) {
        tv.textColor = [UIColor colorWithRed:0.22 green:0.38 blue:0.24 alpha:1.0];
        tv.backgroundColor = [UIColor clearColor];
    } else {
        tv.textColor = [UIColor colorWithRed:0.55 green:0.95 blue:0.45 alpha:1.0];
        tv.backgroundColor = [UIColor clearColor];
    }
}

/* caching avoids redrawing identical glass tiles during scrolling */
static UIImage *gGlassIdleL, *gGlassIdleD, *gGlassSelL, *gGlassSelD;
static UIImage *gGlassDivL, *gGlassDivD;

static UIImage *SenkoGlassStretch(UIColor *fill, CGFloat h) {
    if (h < 1.0f) h = 28.0f;
    CGSize sz = CGSizeMake(12.0f, h);
    UIGraphicsBeginImageContextWithOptions(sz, NO, 0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    if (ctx) {
        [fill setFill];
        UIBezierPath *p = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 0, sz.width, sz.height)
                                                     cornerRadius:6.0f];
        [p fill];
    }
    UIImage *img = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return [img stretchableImageWithLeftCapWidth:6 topCapHeight:0];
}

static void SenkoGlassEnsureCache(void) {
    if (gGlassIdleL && gGlassIdleD) return;
    UIColor *idleL = [UIColor colorWithWhite:1.0 alpha:0.30];
    UIColor *idleD = [UIColor colorWithWhite:1.0 alpha:0.12];
    UIColor *selL = [UIColor colorWithRed:0.15 green:0.50 blue:1.00 alpha:0.50];
    UIColor *selD = [UIColor colorWithRed:0.25 green:0.55 blue:1.00 alpha:0.42];
    gGlassIdleL = [SenkoGlassStretch(idleL, 32.0f) retain];
    gGlassIdleD = [SenkoGlassStretch(idleD, 32.0f) retain];
    gGlassSelL = [SenkoGlassStretch(selL, 32.0f) retain];
    gGlassSelD = [SenkoGlassStretch(selD, 32.0f) retain];
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(2, 32), NO, 0);
    [[UIColor colorWithWhite:1 alpha:0.40f] setFill];
    UIRectFill(CGRectMake(0, 0, 2, 32));
    gGlassDivL = [UIGraphicsGetImageFromCurrentImageContext() retain];
    UIGraphicsEndImageContext();
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(2, 32), NO, 0);
    [[UIColor colorWithWhite:1 alpha:0.22f] setFill];
    UIRectFill(CGRectMake(0, 0, 2, 32));
    gGlassDivD = [UIGraphicsGetImageFromCurrentImageContext() retain];
    UIGraphicsEndImageContext();
}

enum { kSenkoScreenBgTag = 9111 };

CGRect SenkoViewBounds(UIView *view) {
    if (!view) return CGRectZero;
    CGRect b = view.bounds;
    if (b.size.width < 1.0f || b.size.height < 1.0f) {
        b = [[UIScreen mainScreen] applicationFrame];
        b.origin = CGPointZero;
    }
    UIInterfaceOrientation o = [UIApplication sharedApplication].statusBarOrientation;
    BOOL wantLand = UIInterfaceOrientationIsLandscape(o);
    BOOL isLand = b.size.width > b.size.height + 0.5f;
    if (wantLand != isLand) {
/* pre-ios 8 reports portrait bounds during rotation, so landscape swaps axes */
        CGFloat t = b.size.width;
        b.size.width = b.size.height;
        b.size.height = t;
    }
/* the superview reflects the new size before child bounds finish rotating */
    if (view.superview) {
        CGRect sb = view.superview.bounds;
        if (sb.size.width > b.size.width + 1.0f)
            b.size.width = sb.size.width;
        if (sb.size.height > b.size.height + 1.0f)
            b.size.height = sb.size.height;
    }
    return b;
}

void SenkoApplyScreenChrome(UIView *root) {
    if (!root) return;
    UIView *old = [root viewWithTag:kSenkoScreenBgTag];
    NSArray *subs = [NSArray arrayWithArray:root.layer.sublayers];
    for (CALayer *L in subs) {
        if ([L.name isEqualToString:@"bgGrad"] || [L.name isEqualToString:@"vgrad"])
            [L removeFromSuperlayer];
    }
    if (!SenkoThemeIsIos26()) {
        if (old) [old removeFromSuperview];
        root.backgroundColor = kBG;
        AddVGradient(root, kBG, kBGBot);
        return;
    }
    BOOL light = SenkoThemeIsLight();
    NSString *name = light ? @"ios26-bg-light" : @"ios26-bg-dark";
    UIImage *img = [UIImage imageNamed:[name stringByAppendingString:@".jpg"]];
    if (!img) {
        NSString *p = [[NSBundle mainBundle] pathForResource:name ofType:@"jpg"];
        if (p) img = [UIImage imageWithContentsOfFile:p];
    }
    root.backgroundColor = light
        ? [UIColor colorWithWhite:0.92 alpha:1]
        : [UIColor colorWithWhite:0.06 alpha:1];
    if (!img) {
        if (old) [old removeFromSuperview];
        return;
    }
    UIImageView *bg = (UIImageView *)old;
    if (![bg isKindOfClass:[UIImageView class]]) {
        if (old) [old removeFromSuperview];
        bg = [[[UIImageView alloc] initWithImage:img] autorelease];
        bg.tag = kSenkoScreenBgTag;
        bg.contentMode = UIViewContentModeScaleAspectFill;
        bg.clipsToBounds = YES;
        bg.userInteractionEnabled = NO;
        bg.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [root insertSubview:bg atIndex:0];
    } else {
        bg.image = img;
        bg.hidden = NO;
    }
    bg.frame = root.bounds;
/* an opaque cover avoids full-screen blending on old gpus */
    bg.opaque = YES;
    bg.backgroundColor = root.backgroundColor;
    [root sendSubviewToBack:bg];
}

void SenkoStyleGlassField(UITextField *field) {
    if (!field) return;
    BOOL light = SenkoThemeIsLight();
    if (!SenkoThemeIsIos26()) {
        field.borderStyle = UITextBorderStyleRoundedRect;
        field.backgroundColor = kWell;
        field.textColor = kInk;
        return;
    }
/* removing the system bezel prevents a solid white ios 6 button background */
    field.borderStyle = UITextBorderStyleNone;
    field.opaque = NO;
    field.backgroundColor = light
        ? [UIColor colorWithWhite:1.0 alpha:0.32]
        : [UIColor colorWithWhite:1.0 alpha:0.14];
    field.textColor = kInk;
    field.layer.cornerRadius = 8.0f;
    field.clipsToBounds = YES;
    field.layer.borderWidth = 0.5f;
    field.layer.borderColor = light
        ? [UIColor colorWithWhite:1 alpha:0.65].CGColor
        : [UIColor colorWithWhite:1 alpha:0.30].CGColor;
    if (!field.leftView) {
        UIView *pad = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, 10, 28)] autorelease];
        pad.userInteractionEnabled = NO;
        field.leftView = pad;
        field.leftViewMode = UITextFieldViewModeAlways;
    }
}

void SenkoStyleGlassSegmented(UISegmentedControl *seg) {
    if (!seg) return;
    if (!SenkoThemeIsIos26()) return;
    BOOL light = SenkoThemeIsLight();

    seg.segmentedControlStyle = UISegmentedControlStyleBar;
    seg.opaque = NO;
    seg.backgroundColor = [UIColor clearColor];
    if ([seg respondsToSelector:@selector(setTintColor:)])
        seg.tintColor = [UIColor clearColor];

    SenkoGlassEnsureCache();
    UIImage *idleImg = light ? gGlassIdleL : gGlassIdleD;
    UIImage *selImg = light ? gGlassSelL : gGlassSelD;
    UIImage *div = light ? gGlassDivL : gGlassDivD;
    if ([seg respondsToSelector:@selector(setBackgroundImage:forState:barMetrics:)]) {
        [seg setBackgroundImage:idleImg forState:UIControlStateNormal barMetrics:UIBarMetricsDefault];
        [seg setBackgroundImage:selImg forState:UIControlStateSelected barMetrics:UIBarMetricsDefault];
        [seg setBackgroundImage:selImg forState:UIControlStateHighlighted barMetrics:UIBarMetricsDefault];
        [seg setBackgroundImage:selImg forState:(UIControlState)(UIControlStateSelected | UIControlStateHighlighted)
                     barMetrics:UIBarMetricsDefault];
    }
    if (div && [seg respondsToSelector:@selector(setDividerImage:forLeftSegmentState:rightSegmentState:barMetrics:)]) {
        [seg setDividerImage:div
         forLeftSegmentState:UIControlStateNormal
           rightSegmentState:UIControlStateNormal
                  barMetrics:UIBarMetricsDefault];
        [seg setDividerImage:div
         forLeftSegmentState:UIControlStateSelected
           rightSegmentState:UIControlStateNormal
                  barMetrics:UIBarMetricsDefault];
        [seg setDividerImage:div
         forLeftSegmentState:UIControlStateNormal
           rightSegmentState:UIControlStateSelected
                  barMetrics:UIBarMetricsDefault];
    }
    NSDictionary *titleN = [NSDictionary dictionaryWithObjectsAndKeys:
                            kInk, UITextAttributeTextColor,
                            [UIFont boldSystemFontOfSize:13], UITextAttributeFont, nil];
    NSDictionary *titleS = [NSDictionary dictionaryWithObjectsAndKeys:
                            [UIColor whiteColor], UITextAttributeTextColor,
                            [UIFont boldSystemFontOfSize:13], UITextAttributeFont, nil];
    if ([seg respondsToSelector:@selector(setTitleTextAttributes:forState:)]) {
        [seg setTitleTextAttributes:titleN forState:UIControlStateNormal];
        [seg setTitleTextAttributes:titleS forState:UIControlStateSelected];
    }
}

void SenkoStyleSelectableCell(UITableViewCell *cell) {
    if (!cell) return;
    UIView *selected = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    selected.backgroundColor = [kAccentBlue colorWithAlphaComponent:
        SenkoThemeIsLight() ? 0.13f : 0.20f];
    selected.opaque = NO;
    cell.selectedBackgroundView = selected;
    cell.selectionStyle = UITableViewCellSelectionStyleBlue;
}

/* a fixed accent prevents status color jumps while refresh changes state */
void SetStatusDefault(UILabel *label, NSString *text) {
    if (SenkoThemeIsIos26())
        label.font = SenkoFontBody(13, NO);
    else if (SenkoThemeIsIos16())
        label.font = SenkoFontBody(13, NO);
    else if (SenkoThemeIsFlat())
        label.font = [UIFont systemFontOfSize:14];
    else
        label.font = [UIFont boldSystemFontOfSize:14];
    SenkoStyleAccentLabel(label);
    label.text = text;
}

void SetStatusRefresh(UILabel *label, NSString *text) {
    if (SenkoThemeIsIos26())
        label.font = SenkoFontBody(13, YES);
    else if (SenkoThemeIsIos16())
        label.font = SenkoFontBody(13, YES);
    else if (SenkoThemeIsFlat())
        label.font = [UIFont systemFontOfSize:13];
    else
        label.font = [UIFont boldSystemFontOfSize:13];
    SenkoStyleAccentLabel(label);
    label.text = text;
}

/* caching prevents section headers from retinting identical icons while scrolling */

/* motion helpers. every entry point degrades instead of skipping the movement:
   ios 5 has block animation but no spring curve, so the fallback plays the
   overshoot as two timed steps */

void SenkoAnimate(NSTimeInterval duration, void (^animations)(void),
                  void (^completion)(BOOL finished)) {
    if (!animations) return;
    if (![UIView respondsToSelector:@selector(animateWithDuration:animations:completion:)]) {
        animations();
        if (completion) completion(YES);
        return;
    }
    [UIView animateWithDuration:duration
                          delay:0
                        options:UIViewAnimationOptionBeginFromCurrentState |
                                UIViewAnimationOptionCurveEaseOut
                     animations:animations
                     completion:completion];
}

void SenkoAnimateSpring(NSTimeInterval duration, NSTimeInterval delay,
                        void (^animations)(void),
                        void (^completion)(BOOL finished)) {
    if (!animations) return;
    SEL spring = @selector(animateWithDuration:delay:usingSpringWithDamping:
                           initialSpringVelocity:options:animations:completion:);
    if ([UIView respondsToSelector:spring]) {
        /* the armv7 slice builds against an sdk that predates the spring api,
           so the call goes through a typed send instead of an implicit
           declaration that would pass the damping floats in the wrong slots */
        typedef void (*SenkoSpringFn)(id, SEL, NSTimeInterval, NSTimeInterval,
                                      CGFloat, CGFloat, NSUInteger,
                                      void (^)(void), void (^)(BOOL));
        ((SenkoSpringFn)objc_msgSend)([UIView class], spring, duration, delay,
                                      0.78f, 0.45f,
                                      UIViewAnimationOptionBeginFromCurrentState,
                                      animations, completion);
        return;
    }
    if (![UIView respondsToSelector:@selector(animateWithDuration:animations:completion:)]) {
        animations();
        if (completion) completion(YES);
        return;
    }
    [UIView animateWithDuration:duration
                          delay:delay
                        options:UIViewAnimationOptionBeginFromCurrentState |
                                UIViewAnimationOptionCurveEaseOut
                     animations:animations
                     completion:completion];
}

/* a rasterized layer rebuilds its cache on every step of a scale, which is what
   makes a press feel expensive on an armv7 device. the cache is dropped for the
   duration and restored once the view is at rest again */
static void SuspendRasterization(UIView *view) {
    if (!view.layer.shouldRasterize) return;
    objc_setAssociatedObject(view, &kSenkoRasterKey, [NSNumber numberWithBool:YES],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    view.layer.shouldRasterize = NO;
}

static void RestoreRasterization(UIView *view) {
    if (![objc_getAssociatedObject(view, &kSenkoRasterKey) boolValue]) return;
    objc_setAssociatedObject(view, &kSenkoRasterKey, nil,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    view.layer.rasterizationScale = [UIScreen mainScreen].scale;
    view.layer.shouldRasterize = YES;
}

void SenkoPressPop(UIView *view, BOOL pressed) {
    if (!view) return;
    CGAffineTransform target = pressed
        ? CGAffineTransformMakeScale(0.965f, 0.965f)
        : CGAffineTransformIdentity;
    if (CGAffineTransformEqualToTransform(view.transform, target)) return;
    SuspendRasterization(view);
    /* the release has to spring, the press must not: a bouncy press feels
       like lag when the finger is still down */
    if (pressed) {
        SenkoAnimate(0.10, ^{ view.transform = target; }, NULL);
    } else {
        SenkoAnimateSpring(0.30, 0, ^{ view.transform = target; }, ^(BOOL done) {
            (void)done;
            RestoreRasterization(view);
        });
    }
}

void SenkoRevealView(UIView *view, NSUInteger index) {
    if (!view) return;
    if (![UIView respondsToSelector:@selector(animateWithDuration:delay:options:animations:completion:)]) {
        view.alpha = 1.0f;
        return;
    }
    /* the stagger is capped so a long list does not delay its last row by a
       visible pause after the first paint */
    NSTimeInterval delay = index > 7 ? 0.28 : index * 0.04;
    CGAffineTransform rest = view.transform;
    SuspendRasterization(view);
    view.alpha = 0.0f;
    view.transform = CGAffineTransformConcat(CGAffineTransformMakeTranslation(0, 14.0f), rest);
    [UIView animateWithDuration:0.34
                          delay:delay
                        options:UIViewAnimationOptionBeginFromCurrentState |
                                UIViewAnimationOptionCurveEaseOut
                     animations:^{
                         view.alpha = 1.0f;
                         view.transform = rest;
                     }
                     completion:^(BOOL done) {
                         (void)done;
                         RestoreRasterization(view);
                     }];
}

/* the header wordmark wants a geometric bold close to Rubik. no ios ships that
   face, so the ladder walks from the roundest system face down to what ios 5
   actually has and stops at the first one the device can create. every symbol
   past ios 6 is resolved at runtime because the armv7 slice builds against an
   sdk that has neither UIFontDescriptor nor the rounded design */
UIFont *SenkoFontDisplay(CGFloat size) {
    UIFont *base = [UIFont boldSystemFontOfSize:size];
    SEL descSel = @selector(fontDescriptor);
    SEL designSel = NSSelectorFromString(@"fontDescriptorWithDesign:");
    SEL makeSel = NSSelectorFromString(@"fontWithDescriptor:size:");
    if ([base respondsToSelector:descSel] && [UIFont respondsToSelector:makeSel]) {
        id desc = ((id (*)(id, SEL))objc_msgSend)(base, descSel);
        if ([desc respondsToSelector:designSel]) {
            id rounded = ((id (*)(id, SEL, NSString *))objc_msgSend)
                (desc, designSel, @"NSCTFontUIFontDesignRounded");
            if (rounded) {
                UIFont *f = ((id (*)(id, SEL, id, CGFloat))objc_msgSend)
                    ([UIFont class], makeSel, rounded, size);
                if (f) return f;
            }
        }
    }
    static NSString * const kFaces[] = {
        @"AvenirNext-Bold", @"Avenir-Black", @"HelveticaNeue-Bold"
    };
    for (size_t i = 0; i < sizeof kFaces / sizeof kFaces[0]; ++i) {
        UIFont *f = [UIFont fontWithName:kFaces[i] size:size];
        if (f) return f;
    }
    return base;
}

/* a label lays out text the same way every screen here draws it, and it is the
   one measurement that exists unchanged from ios 5 to ios 16. the string
   drawing category cannot be used instead: -sizeWithAttributes: returns a
   struct through objc_msgSend, which is the wrong call on armv7, and
   -sizeWithFont:constrainedToSize:lineBreakMode: takes an unbounded height
   that newer text layout does not accept */
CGSize SenkoTextSize(NSString *text, UIFont *font, CGFloat width) {
    static UILabel *gauge = nil;
    if (![text length] || !font || width < 1.0f) return CGSizeZero;
    if (!gauge) {
        gauge = [[UILabel alloc] initWithFrame:CGRectZero];
        gauge.numberOfLines = 0;
        gauge.lineBreakMode = NSLineBreakByWordWrapping;
        gauge.backgroundColor = [UIColor clearColor];
    }
    gauge.font = font;
    gauge.text = text;
    CGSize fit = [gauge sizeThatFits:CGSizeMake(width, 100000.0f)];
    if (fit.width > width) fit.width = width;
    fit.width = ceilf(fit.width);
    fit.height = ceilf(fit.height);
    return fit;
}

/* measuring the string is the only way to stop a control from ellipsing its own
   title */
CGFloat SenkoTextWidth(NSString *text, UIFont *font) {
    return SenkoTextSize(text, font, 100000.0f).width;
}

/* a palette is free to hand over two nearly identical stops, which paints a
   flat slab where the design asks for a gradient. these derive the stops from
   one colour so every theme gets the same amount of relief */
static void SenkoColorParts(UIColor *c, CGFloat *r, CGFloat *g, CGFloat *b, CGFloat *a) {
    *r = *g = *b = 0.0f;
    *a = 1.0f;
    if ([c respondsToSelector:@selector(getRed:green:blue:alpha:)] &&
        [c getRed:r green:g blue:b alpha:a])
        return;
    CGFloat w = 0.0f;
    if ([c respondsToSelector:@selector(getWhite:alpha:)] && [c getWhite:&w alpha:a])
        *r = *g = *b = w;
}

UIColor *SenkoShadeColor(UIColor *base, CGFloat delta) {
    if (!base) return nil;
    CGFloat r, g, b, a;
    SenkoColorParts(base, &r, &g, &b, &a);
    CGFloat target = delta > 0.0f ? 1.0f : 0.0f;
    CGFloat mix = delta < 0.0f ? -delta : delta;
    if (mix > 1.0f) mix = 1.0f;
    return [UIColor colorWithRed:r + (target - r) * mix
                           green:g + (target - g) * mix
                            blue:b + (target - b) * mix
                           alpha:a];
}
