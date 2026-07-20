#import "ui_theme.h"
#include <math.h>
#include <objc/runtime.h>
#include <objc/message.h>
#import "ui_chrome_priv.h"

void StyleNavBarClassic(UINavigationController *nav) {
    if (!nav) return;
    UINavigationBar *bar = nav.navigationBar;
    BOOL light = SenkoThemeIsLight();
    BOOL flat = SenkoThemeIsFlat();
    BOOL boy = SenkoThemeIsBoykisser();
    BOOL miside = SenkoThemeIsMiside();
/* miside is purple field: use black bar chrome, not ios6 light white */
    BOOL barLight = light && !miside;
    bar.barStyle = barLight ? UIBarStyleDefault : UIBarStyleBlack;
/* opaque bars: translucent nav ate the first section title (settings/daemon) */
    bar.translucent = NO;
    if ([bar respondsToSelector:@selector(setBackgroundImage:forBarMetrics:)]) {
        CGSize sz = CGSizeMake(2, 44);
        UIGraphicsBeginImageContextWithOptions(sz, YES, 0);
        CGContextRef ctx = UIGraphicsGetCurrentContext();
        if (miside) {
/* deep plum bar matching dark wallpaper */
            CGContextSetRGBFillColor(ctx, 0.12f, 0.04f, 0.14f, 1.0f);
            CGContextFillRect(ctx, CGRectMake(0, 0, 2, 44));
            CGContextSetRGBFillColor(ctx, 1.00f, 0.36f, 0.70f, 0.50f);
            CGContextFillRect(ctx, CGRectMake(0, 43, 2, 1));
            UIImage *img = UIGraphicsGetImageFromCurrentImageContext();
            UIGraphicsEndImageContext();
            [bar setBackgroundImage:img forBarMetrics:UIBarMetricsDefault];
        } else if (flat) {
            if (SenkoThemeIsIos16()) {
                if (light) {
/* frosted material bar over pastel wallpaper */
                    CGContextSetRGBFillColor(ctx, 0.98f, 0.96f, 0.99f, 1.0f);
                    CGContextFillRect(ctx, CGRectMake(0, 0, 2, 44));
                    CGContextSetRGBFillColor(ctx, 0.88f, 0.84f, 0.94f, 1.0f);
                    CGContextFillRect(ctx, CGRectMake(0, 43, 2, 1));
                } else {
                    CGContextSetRGBFillColor(ctx, 0.08f, 0.05f, 0.14f, 1.0f);
                    CGContextFillRect(ctx, CGRectMake(0, 0, 2, 44));
                    CGContextSetRGBFillColor(ctx, 0.22f, 0.16f, 0.32f, 1.0f);
                    CGContextFillRect(ctx, CGRectMake(0, 43, 2, 1));
                }
            } else if (light) {
                CGContextSetRGBFillColor(ctx, 0.96f, 0.96f, 0.97f, 1.0f);
                CGContextFillRect(ctx, CGRectMake(0, 0, 2, 44));
                CGContextSetRGBFillColor(ctx, 0.78f, 0.78f, 0.80f, 1.0f);
                CGContextFillRect(ctx, CGRectMake(0, 43, 2, 1));
            } else {
                CGContextSetRGBFillColor(ctx, 0.12f, 0.13f, 0.15f, 1.0f);
                CGContextFillRect(ctx, CGRectMake(0, 0, 2, 44));
                CGContextSetRGBFillColor(ctx, 0.22f, 0.22f, 0.24f, 1.0f);
                CGContextFillRect(ctx, CGRectMake(0, 43, 2, 1));
            }
            UIImage *img = UIGraphicsGetImageFromCurrentImageContext();
            UIGraphicsEndImageContext();
            [bar setBackgroundImage:img forBarMetrics:UIBarMetricsDefault];
        } else {
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGFloat comps[12];
            if (boy) {
                CGFloat p[] = { 1.00,0.92,0.95,1, 1.00,0.78,0.88,1, 1.00,0.68,0.82,1 };
                memcpy(comps, p, sizeof p);
            } else if (light) {
                CGFloat l[] = { 0.96,0.95,0.92,1, 0.86,0.84,0.80,1, 0.76,0.74,0.70,1 };
                memcpy(comps, l, sizeof l);
            } else {
                CGFloat d[] = { 0.42,0.43,0.46,1, 0.12,0.13,0.15,1, 0.05,0.05,0.06,1 };
                memcpy(comps, d, sizeof d);
            }
            CGFloat locs[] = { 0, 0.5, 1 };
            CGGradientRef gr = CGGradientCreateWithColorComponents(cs, comps, locs, 3);
            CGContextDrawLinearGradient(ctx, gr, CGPointMake(0, 0), CGPointMake(0, 44), 0);
            CGGradientRelease(gr);
            CGColorSpaceRelease(cs);
            UIImage *img = UIGraphicsGetImageFromCurrentImageContext();
            UIGraphicsEndImageContext();
            [bar setBackgroundImage:img forBarMetrics:UIBarMetricsDefault];
        }
    }
    if ([bar respondsToSelector:@selector(setTitleTextAttributes:)]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        UIColor *titleC;
        if (miside)
            titleC = [UIColor whiteColor];
        else if (light || flat)
            titleC = kInk;
        else
            titleC = [UIColor whiteColor];
        UIFont *titleFont = SenkoThemeIsIos16()
            ? SenkoFontTitle(18)
            : ((flat) ? SenkoFontTitle(17) : [UIFont boldSystemFontOfSize:18]);
        NSMutableDictionary *attrs = [NSMutableDictionary dictionaryWithObjectsAndKeys:
            titleC, UITextAttributeTextColor,
            titleFont, UITextAttributeFont, nil];
        if (flat || miside) {
            [attrs setObject:[UIColor clearColor] forKey:UITextAttributeTextShadowColor];
            [attrs setObject:[NSValue valueWithCGSize:CGSizeZero]
                      forKey:UITextAttributeTextShadowOffset];
        } else {
            UIColor *shC = light ? [UIColor colorWithWhite:1 alpha:0.55]
                                 : [UIColor colorWithWhite:0 alpha:0.65];
            [attrs setObject:shC forKey:UITextAttributeTextShadowColor];
            [attrs setObject:[NSValue valueWithCGSize:CGSizeMake(0, light ? 1 : -1)]
                      forKey:UITextAttributeTextShadowOffset];
        }
#pragma clang diagnostic pop
        [bar setTitleTextAttributes:attrs];
    }
/* done / back tint */
    if ([bar respondsToSelector:@selector(setTintColor:)]) {
        if (miside)
            bar.tintColor = [UIColor colorWithRed:1.0 green:0.55 blue:0.80 alpha:1.0];
        else if (boy)
            bar.tintColor = [UIColor colorWithRed:1.0 green:0.38 blue:0.68 alpha:1.0];
        else
            bar.tintColor = kAccentBlue;
    }
    if ([bar respondsToSelector:@selector(setShadowImage:)]) {
/* clear default nav bar shadow image */
        CGSize one = CGSizeMake(1, 1);
        UIGraphicsBeginImageContextWithOptions(one, NO, 0);
        UIImage *clear = UIGraphicsGetImageFromCurrentImageContext();
        UIGraphicsEndImageContext();
        [bar setShadowImage:clear];
    }
}

void StyleGlossyCapsuleLayout(UIButton *button) {
    if (!button) return;
    CGRect b = button.bounds;
    if (b.size.width < 1 || b.size.height < 1) return;
    CGFloat cr = b.size.height / 2.0f;
    CAGradientLayer *body = SenkoNamedGradientLayer(button.layer, @"body");
    CAGradientLayer *gloss = SenkoNamedGradientLayer(button.layer, @"gloss");
    if (!body || !gloss) return;
    SenkoBeginSilentLayers();
    button.layer.cornerRadius = cr;
    body.frame = b;
    body.cornerRadius = cr;
    gloss.frame = CGRectMake(1, 1, b.size.width - 2, b.size.height * 0.48f);
    gloss.cornerRadius = cr * 0.9f;
    if (SenkoThemeIsIos16() || !SenkoThemeIsFlat())
        SenkoApplyShadowPath(button.layer, cr);
    SenkoEndSilentLayers();
    SenkoStyleRemember(button, b.size,
                  objc_getAssociatedObject(button, &SenkoStyleTopKey),
                  objc_getAssociatedObject(button, &SenkoStyleBotKey));
}

void StyleGlossyCapsule(UIButton *button, UIColor *top, UIColor *bottom) {
    if (!button) return;
    CGRect b = button.bounds;
    if (b.size.width < 1 || b.size.height < 1) return;
    UIColor *prevTop = objc_getAssociatedObject(button, &SenkoStyleTopKey);
    UIColor *prevBot = objc_getAssociatedObject(button, &SenkoStyleBotKey);
    BOOL sameColors = (prevTop == top && prevBot == bottom);
    BOOL ios26 = SenkoThemeIsIos26();
/* reuse chrome when size and color pointers match */
    if (sameColors && SenkoStyleSizeMatches(button, b.size) &&
        SenkoNamedGradientLayer(button.layer, @"body")) {
        StyleGlossyCapsuleLayout(button);
        return;
    }

    CGFloat cr = b.size.height / 2.0f;
    BOOL flat = SenkoThemeIsFlat();
    BOOL light = SenkoThemeIsLight();
    SenkoBeginSilentLayers();
    button.layer.cornerRadius = cr;
    button.layer.masksToBounds = NO;
    BOOL ios16 = SenkoThemeIsIos16();
    if (ios26) {
/* floating glass control plane (capsule) */
        button.layer.borderWidth = 0.5f;
        button.layer.borderColor = light
            ? [UIColor colorWithWhite:1 alpha:0.90].CGColor
            : [UIColor colorWithWhite:1 alpha:0.40].CGColor;
        button.layer.shadowColor = [UIColor colorWithWhite:0 alpha:1].CGColor;
        button.layer.shadowOffset = CGSizeMake(0, 4);
        button.layer.shadowOpacity = light ? 0.12f : 0.36f;
        button.layer.shadowRadius = 8.0f;
        SenkoApplyShadowPath(button.layer, cr);
    } else if (flat) {
        button.layer.borderWidth = 0;
        button.layer.borderColor = [UIColor clearColor].CGColor;
        if (ios16) {
            button.layer.shadowColor = [UIColor blackColor].CGColor;
            button.layer.shadowOffset = CGSizeMake(0, 2);
            button.layer.shadowOpacity = light ? 0.14f : 0.30f;
            button.layer.shadowRadius = 5;
            SenkoApplyShadowPath(button.layer, cr);
        } else {
            button.layer.shadowOpacity = 0.0f;
            button.layer.shadowRadius = 0;
            button.layer.shadowPath = nil;
        }
    } else {
        button.layer.borderWidth = 0;
        button.layer.borderColor = [UIColor clearColor].CGColor;
        button.layer.shadowOpacity = 0.0f;
        button.layer.shadowRadius = 0;
        button.layer.shadowPath = nil;
    }

    CAGradientLayer *body = SenkoNamedGradientLayer(button.layer, @"body");
    if (!body) {
        body = [CAGradientLayer layer];
        body.name = @"body";
        [button.layer insertSublayer:body atIndex:0];
    }
    body.frame = b;
    body.cornerRadius = cr;
    if (ios26) {
/* keep wash thin so blur shows through */
        body.colors = [NSArray arrayWithObjects:
                       (id)[UIColor colorWithWhite:1 alpha:light ? 0.18f : 0.12f].CGColor,
                       (id)[UIColor colorWithWhite:1 alpha:light ? 0.05f : 0.03f].CGColor, nil];
        body.hidden = NO;
        body.opacity = 1.0f;
    } else if (ios16)
        body.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)bottom.CGColor, nil];
    else if (flat)
        body.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)top.CGColor, nil];
    else
        body.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)bottom.CGColor, nil];

    CAGradientLayer *gloss = SenkoNamedGradientLayer(button.layer, @"gloss");
    if (!gloss) {
        gloss = [CAGradientLayer layer];
        gloss.name = @"gloss";
        [button.layer insertSublayer:gloss above:body];
    }
    gloss.frame = CGRectMake(1, 1, b.size.width - 2, b.size.height * 0.48f);
    gloss.cornerRadius = cr * 0.9f;
    if (ios26 || flat) {
        gloss.hidden = YES;
        gloss.colors = [NSArray arrayWithObjects:
                        (id)[UIColor clearColor].CGColor,
                        (id)[UIColor clearColor].CGColor, nil];
    } else {
        gloss.hidden = NO;
        gloss.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithWhite:1 alpha:0.55].CGColor,
                        (id)[UIColor colorWithWhite:1 alpha:0.05].CGColor, nil];
    }
/* capsule is small: bake after frost install */
    if (b.size.width <= 200.0f && b.size.height <= 48.0f) {
        button.layer.shouldRasterize = YES;
        button.layer.rasterizationScale = [UIScreen mainScreen].scale;
    } else {
        button.layer.shouldRasterize = NO;
    }
    SenkoEndSilentLayers();

    if (ios26) {
        SenkoInstallFrost(button);
        button.layer.shouldRasterize = YES;
        button.layer.rasterizationScale = [UIScreen mainScreen].scale;
        button.titleLabel.font = SenkoFontBody(13, YES);
        [button setTitleColor:light ? kInk : [UIColor whiteColor]
                     forState:UIControlStateNormal];
        button.titleLabel.shadowColor = nil;
        button.titleLabel.shadowOffset = CGSizeZero;
    } else {
        if (b.size.width > 160.0f || b.size.height > 40.0f) {
            if (!button.titleLabel.font || button.titleLabel.font.pointSize < 14.0f)
                button.titleLabel.font = [UIFont boldSystemFontOfSize:18];
        } else if (ios16) {
            button.titleLabel.font = SenkoFontBody(13, YES);
        } else {
            button.titleLabel.font = (flat)
                ? [UIFont systemFontOfSize:13]
                : [UIFont boldSystemFontOfSize:12];
        }
        SenkoStyleChromeTitle(button);
    }
    SenkoStyleRemember(button, b.size, top, bottom);
}

CGFloat GetTopOffset(void) {
/* ios6: 0; ios7+: status bar height, cap 20 (landscape may swap axes) */
    if ([[[UIDevice currentDevice] systemVersion] floatValue] < 7.0f)
        return 0.0f;
    CGRect sb = [UIApplication sharedApplication].statusBarFrame;
    CGFloat h = sb.size.height;
    CGFloat w = sb.size.width;
/* min edge; landscape may swap status bar frame */
    CGFloat edge = h;
    if (w > 0.0f && w < edge) edge = w;
    if (edge < 1.0f) edge = 20.0f;
    if (edge > 20.0f) edge = 20.0f;
    return edge;
}

CAGradientLayer *AddVGradient(UIView *view, UIColor *top, UIColor *bottom) {
    CAGradientLayer *gradient = [CAGradientLayer layer];
    gradient.name = @"vgrad";
    gradient.frame = view.bounds;
    gradient.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)bottom.CGColor, nil];
    [view.layer insertSublayer:gradient atIndex:0];
    return gradient;
}

