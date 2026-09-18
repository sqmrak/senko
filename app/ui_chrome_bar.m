#import "ui_theme.h"
#include <math.h>
#include <objc/runtime.h>
#include <objc/message.h>
#import "ui_chrome_priv.h"

/* ios 15 draws every navigation bar with scrollEdgeAppearance, and its default
   is transparent: the bar image set through the old properties is ignored and
   the dark title is left on whatever is behind the bar */
static void SenkoApplyBarAppearance(UINavigationBar *bar, UIImage *background,
                                    UIColor *titleColor, UIFont *titleFont) {
    Class cls = NSClassFromString(@"UINavigationBarAppearance");
    if (!cls || ![bar respondsToSelector:@selector(setStandardAppearance:)]) return;
    id appearance = [[cls alloc] init];
    if (!appearance) return;
    if ([appearance respondsToSelector:@selector(configureWithOpaqueBackground)])
        ((void (*)(id, SEL))objc_msgSend)(appearance, @selector(configureWithOpaqueBackground));
    if (background && [appearance respondsToSelector:@selector(setBackgroundImage:)])
        ((void (*)(id, SEL, id))objc_msgSend)(appearance, @selector(setBackgroundImage:),
                                              background);
    if ([appearance respondsToSelector:@selector(setShadowColor:)])
        ((void (*)(id, SEL, id))objc_msgSend)(appearance, @selector(setShadowColor:), nil);
/* the appearance object takes attributed string keys, and the legacy
   UITextAttribute* shadow keys are not among them */
    NSMutableDictionary *attrs = [NSMutableDictionary dictionary];
    if (titleColor) [attrs setObject:titleColor forKey:@"NSColor"];
    if (titleFont) [attrs setObject:titleFont forKey:@"NSFont"];
    if ([attrs count] && [appearance respondsToSelector:@selector(setTitleTextAttributes:)])
        ((void (*)(id, SEL, id))objc_msgSend)(appearance,
                                              @selector(setTitleTextAttributes:), attrs);
    ((void (*)(id, SEL, id))objc_msgSend)(bar, @selector(setStandardAppearance:), appearance);
    if ([bar respondsToSelector:@selector(setCompactAppearance:)])
        ((void (*)(id, SEL, id))objc_msgSend)(bar, @selector(setCompactAppearance:), appearance);
    if ([bar respondsToSelector:@selector(setScrollEdgeAppearance:)])
        ((void (*)(id, SEL, id))objc_msgSend)(bar, @selector(setScrollEdgeAppearance:), appearance);
    [appearance release];
}

void StyleNavBarClassic(UINavigationController *nav) {
    if (!nav) return;
    UINavigationBar *bar = nav.navigationBar;
    BOOL light = SenkoThemeIsLight();
    BOOL flat = SenkoThemeIsFlat();
    BOOL boy = SenkoThemeIsBoykisser();
    BOOL miside = SenkoThemeIsMiside();
    UIImage *barBackground = nil;
/* black chrome preserves contrast over the purple miside field on ios 6 */
    BOOL barLight = light && !miside;
    bar.barStyle = barLight ? UIBarStyleDefault : UIBarStyleBlack;
/* an opaque bar prevents old navigation controllers from covering the first section */
    bar.translucent = NO;
    if ([bar respondsToSelector:@selector(setBackgroundImage:forBarMetrics:)]) {
        CGSize sz = CGSizeMake(2, 44);
        UIGraphicsBeginImageContextWithOptions(sz, YES, 0);
        CGContextRef ctx = UIGraphicsGetCurrentContext();
        if (miside) {
            CGContextSetRGBFillColor(ctx, 0.12f, 0.04f, 0.14f, 1.0f);
            CGContextFillRect(ctx, CGRectMake(0, 0, 2, 44));
            CGContextSetRGBFillColor(ctx, 1.00f, 0.36f, 0.70f, 0.50f);
            CGContextFillRect(ctx, CGRectMake(0, 43, 2, 1));
        } else if (flat) {
            if (SenkoThemeIsIos16()) {
                if (light) {
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
        } else {
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGFloat comps[12];
            if (boy && light) {
                CGFloat p[] = { 1.00,0.92,0.95,1, 1.00,0.78,0.88,1, 1.00,0.68,0.82,1 };
                memcpy(comps, p, sizeof p);
            } else if (boy) {
                CGFloat p[] = { 0.30,0.115,0.20,1, 0.165,0.060,0.115,1, 0.095,0.032,0.068,1 };
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
        }
        barBackground = UIGraphicsGetImageFromCurrentImageContext();
        UIGraphicsEndImageContext();
        [bar setBackgroundImage:barBackground forBarMetrics:UIBarMetricsDefault];
    }
    UIColor *titleC;
/* the boykisser dark ink is already a pink white, and plain white next to it
   reads as a second colour on the same bar */
    if (miside)
        titleC = [UIColor whiteColor];
    else if (light || flat || boy)
        titleC = kInk;
    else
        titleC = [UIColor whiteColor];
    UIFont *titleFont = SenkoThemeIsIos16()
        ? SenkoFontBody(18, YES)
        : ((flat) ? SenkoFontTitle(17) : [UIFont boldSystemFontOfSize:18]);
    if ([bar respondsToSelector:@selector(setTitleTextAttributes:)]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
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
    if ([bar respondsToSelector:@selector(setTintColor:)]) {
        if (miside)
            bar.tintColor = [UIColor colorWithRed:1.0 green:0.55 blue:0.80 alpha:1.0];
        else if (boy)
            bar.tintColor = [UIColor colorWithRed:1.0 green:0.38 blue:0.68 alpha:1.0];
        else
            bar.tintColor = kAccentBlue;
    }
    if ([bar respondsToSelector:@selector(setShadowImage:)]) {
        CGSize one = CGSizeMake(1, 1);
        UIGraphicsBeginImageContextWithOptions(one, NO, 0);
        UIImage *clear = UIGraphicsGetImageFromCurrentImageContext();
        UIGraphicsEndImageContext();
        [bar setShadowImage:clear];
    }
    SenkoApplyBarAppearance(bar, barBackground, titleC, titleFont);
}

void StyleGlossyCapsuleLayout(UIButton *button) {
    if (!button) return;
    CGRect b = button.bounds;
    if (b.size.width < 1 || b.size.height < 1) return;
    CGFloat cr = b.size.height / 2.0f;
    CAGradientLayer *body = SenkoNamedGradientLayer(button.layer, @"body");
    CAGradientLayer *gloss = SenkoNamedGradientLayer(button.layer, @"gloss");
    UIColor *rememberedTop = objc_getAssociatedObject(button, &SenkoStyleTopKey);
    UIColor *rememberedBottom = objc_getAssociatedObject(button, &SenkoStyleBotKey);
    if (body && rememberedTop && rememberedBottom) {
        NSArray *colors = body.colors;
        BOOL stale = [colors count] < 2 ||
            !CGColorEqualToColor((CGColorRef)[colors objectAtIndex:0], rememberedTop.CGColor) ||
            !CGColorEqualToColor((CGColorRef)[colors objectAtIndex:1], rememberedBottom.CGColor);
        if (stale)
            body.colors = [NSArray arrayWithObjects:(id)rememberedTop.CGColor,
                                                     (id)rememberedBottom.CGColor, nil];
        body.hidden = NO;
        body.opacity = 1.0f;
    }
    if (!body || !gloss) return;
    if (SenkoStyleSizeMatches(button, b.size)) return;
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
/* reuse avoids adding duplicate chrome layers during repeated layout passes */
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
    if (b.size.width <= 200.0f && b.size.height <= 48.0f) {
        button.layer.shouldRasterize = YES;
        button.layer.rasterizationScale = [UIScreen mainScreen].scale;
    } else {
        button.layer.shouldRasterize = NO;
    }
    SenkoEndSilentLayers();

    if (ios26) {
        SenkoInstallFrost(button);
/* the glass caches itself, and rasterizing the layer that holds it makes core
   animation rebuild the blur offscreen on every frame through coreui and core
   image. the same pairing is refused on the section plate for this reason */
        button.layer.shouldRasterize = NO;
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
    UIEdgeInsets safe = SenkoSafeAreaInsets([UIApplication sharedApplication].keyWindow);
    if (safe.top > 0.0f) return safe.top;
    if ([[[UIDevice currentDevice] systemVersion] floatValue] < 7.0f)
        return 0.0f;
    CGRect sb = [UIApplication sharedApplication].statusBarFrame;
    CGFloat h = sb.size.height;
    CGFloat w = sb.size.width;
/* the shorter edge remains the status thickness after landscape axis swaps */
    CGFloat edge = h;
    if (w > 0.0f && w < edge) edge = w;
    if (edge < 1.0f) edge = 20.0f;
    return edge;
}

UIEdgeInsets SenkoSafeAreaInsets(UIView *view) {
    UIEdgeInsets zero = UIEdgeInsetsZero;
    if (!view) return zero;
    SEL sel = NSSelectorFromString(@"safeAreaInsets");
    if (![view respondsToSelector:sel]) return zero;
    IMP imp = [view methodForSelector:sel];
    if (!imp) return zero;
    UIEdgeInsets (*call)(id, SEL) = (UIEdgeInsets (*)(id, SEL))imp;
    UIEdgeInsets insets = call(view, sel);
    if (insets.top < 0.0f || insets.left < 0.0f ||
        insets.bottom < 0.0f || insets.right < 0.0f)
        return zero;
    return insets;
}

@interface SenkoBackdropView : UIView
@end

@implementation SenkoBackdropView
+ (Class)layerClass { return [CAGradientLayer class]; }
@end

CAGradientLayer *AddVGradient(UIView *view, UIColor *top, UIColor *bottom) {
    if (!view) return nil;
    SenkoBackdropView *host = (SenkoBackdropView *)[view viewWithTag:kSenkoBackdropTag];
    if (![host isKindOfClass:[SenkoBackdropView class]] || host.superview != view) {
        host = [[[SenkoBackdropView alloc] initWithFrame:view.bounds] autorelease];
        host.tag = kSenkoBackdropTag;
        host.userInteractionEnabled = NO;
        host.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                UIViewAutoresizingFlexibleHeight;
        [view insertSubview:host atIndex:0];
    } else {
        host.frame = view.bounds;
        [view sendSubviewToBack:host];
    }
    CAGradientLayer *gradient = (CAGradientLayer *)host.layer;
    gradient.name = @"vgrad";
    gradient.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)bottom.CGColor, nil];
    return gradient;
}
