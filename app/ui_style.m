#import "ui_theme.h"
#include <math.h>
#include <objc/runtime.h>
#include <objc/message.h>

enum { kSenkoFrostTag = 9107 };

/* sf bold when system has it (ios9+); weight api on 8.2+; else boldsystem */
static UIFont *SenkoSFBold(CGFloat size) {
    if ([UIFont respondsToSelector:@selector(systemFontOfSize:weight:)]) {
/* uifontweightbold ~= 0.4 */
        UIFont *f = ((UIFont * (*)(id, SEL, CGFloat, CGFloat))objc_msgSend)(
            [UIFont class], @selector(systemFontOfSize:weight:), size, (CGFloat)0.4);
        if (f) return f;
    }
    return [UIFont boldSystemFontOfSize:size];
}

static UIFont *SenkoSFSemibold(CGFloat size) {
    if ([UIFont respondsToSelector:@selector(systemFontOfSize:weight:)]) {
/* uifontweightsemibold ~= 0.3 */
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

void SenkoStyleIos16StatusPill(UILabel *label) {
    if (!label) return;
    label.layer.masksToBounds = YES;
    CGFloat h = label.bounds.size.height;
    if (h < 1.0f) h = 30.0f;
    label.layer.cornerRadius = h * 0.5f;
    if (SenkoThemeIsIos26()) {
        label.layer.borderWidth = 0.5f;
        label.layer.borderColor = SenkoThemeIsLight()
            ? [UIColor colorWithWhite:1 alpha:0.90].CGColor
            : [UIColor colorWithWhite:1 alpha:0.35].CGColor;
        label.backgroundColor = SenkoThemeIsLight()
            ? [UIColor colorWithWhite:1.0 alpha:0.28]
            : [UIColor colorWithWhite:1.0 alpha:0.10];
        label.font = SenkoFontBody(13, YES);
    } else {
        label.layer.borderWidth = 0;
        label.layer.borderColor = [UIColor clearColor].CGColor;
        label.backgroundColor = SenkoThemeIsLight()
            ? [UIColor colorWithWhite:1.0 alpha:0.55]
            : [UIColor colorWithWhite:1.0 alpha:0.12];
        label.font = SenkoFontBody(13, NO);
    }
    SenkoStyleInkLabel(label);
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
/* pure white wash; gray wash looks dirty on light bg */
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

/* solid tint only (scroll-safe) */
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

/* 1 unknown, 0 no ve, 1 has uivisualeffectview */
static int gFrostHasVE = -1;

static int SenkoFrostHasVisualEffect(void) {
    if (gFrostHasVE < 0)
        gFrostHasVE = (NSClassFromString(@"UIVisualEffectView") &&
                       NSClassFromString(@"UIBlurEffect")) ? 1 : 0;
    return gFrostHasVE;
}

/* liquid glass: real blur on 8+; thin translucent wash on 6/7 (no uitoolbar) */
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

/* ios 6/7 wash - reuse existing view */
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

/* light text on always-dark surfaces */
static UIColor *DarkChromeInk(void) {
    return [UIColor colorWithRed:1.00 green:0.92 blue:0.82 alpha:1.0];
}
static UIColor *DarkChromeMuted(void) {
    return [UIColor colorWithRed:0.78 green:0.72 blue:0.64 alpha:1.0];
}
static UIColor *DarkChromeAccent(void) {
    return [UIColor colorWithRed:1.00 green:0.58 blue:0.14 alpha:1.0];
}

/* label shadow: white offset on light, black offset on dark; none on flat */
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
/* muted secondary: soft emboss only on ios6 light; never on flat/glass/dark */
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
/* secondary on dark: no cut-in */
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
/* white title on filled controls */
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
        g.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithRed:1.00 green:0.88 blue:0.93 alpha:0.92].CGColor,
                    (id)[UIColor colorWithRed:1.00 green:0.78 blue:0.88 alpha:0.92].CGColor, nil];
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
/* section headers: alpha plate only (few of them; no per-cell frost) */
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
        plate.layer.shouldRasterize = YES;
        plate.layer.rasterizationScale = [UIScreen mainScreen].scale;
/* frost only on static chrome outside scroll lists */
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

void SenkoStyleSectionTitle(UILabel *label) {
    if (!label) return;
/* section title follows light/flat vs dark styles */
    if (SenkoThemeIsFlat() || SenkoThemeIsLight())
        SenkoStyleInkLabel(label);
    else
        SenkoStyleInkOnDark(label);
}

void SenkoStyleSectionMeta(UILabel *label) {
    if (!label) return;
    if (SenkoThemeIsFlat() || SenkoThemeIsLight())
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

/* cache glass segment tiles (built once) */
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

/* tag for full-screen ios26 wallpaper on secondary vcs */
enum { kSenkoScreenBgTag = 9111 };

CGRect SenkoViewBounds(UIView *view) {
    if (!view) return CGRectZero;
    CGRect b = view.bounds;
    if (b.size.width < 1.0f || b.size.height < 1.0f) {
/* fall back to screen application frame */
        b = [[UIScreen mainScreen] applicationFrame];
        b.origin = CGPointZero;
    }
    UIInterfaceOrientation o = [UIApplication sharedApplication].statusBarOrientation;
    BOOL wantLand = UIInterfaceOrientationIsLandscape(o);
    BOOL isLand = b.size.width > b.size.height + 0.5f;
    if (wantLand != isLand) {
/* bounds still oriented the wrong way - swap axes */
        CGFloat t = b.size.width;
        b.size.width = b.size.height;
        b.size.height = t;
    }
/* prefer superview when it is larger (post-rotation lag) */
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
/* strip solid gradient layers named bggrad so wallpaper can show */
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
/* opaque cover: gpu skips blending the full-screen photo */
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
/* kill system bezel (solid white on ios6) */
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

/* strip stays on accent so idle/connected match "refreshing..." under every theme */
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

/* cache tinted glyphs: scroll headers re-request the same icons constantly */
