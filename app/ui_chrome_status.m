#import "ui_theme.h"
#include <math.h>
#include <objc/runtime.h>
#include <objc/message.h>
#import "ui_chrome_priv.h"

void SenkoApplyBackgroundGradient(CAGradientLayer *g) {
    SenkoApplyBackgroundGradientForState(g, @"idle", NO);
}

void SenkoApplyBackgroundGradientForState(CAGradientLayer *g, NSString *state, BOOL animated) {
    if (!g) return;
    (void)state;

    NSArray *colors = nil;
    NSArray *locations = nil;
    CGPoint start = CGPointMake(0.5f, 0.0f);
    CGPoint end = CGPointMake(0.5f, 1.0f);

    if (SenkoThemeIsIos26()) {
        start = CGPointMake(0.5f, 0.0f);
        end = CGPointMake(0.5f, 1.0f);
        if (SenkoThemeIsLight()) {
            colors = [NSArray arrayWithObjects:
                      (id)[UIColor colorWithWhite:0.96 alpha:1].CGColor,
                      (id)[UIColor colorWithWhite:0.90 alpha:1].CGColor, nil];
        } else {
            colors = [NSArray arrayWithObjects:
                      (id)[UIColor colorWithWhite:0.08 alpha:1].CGColor,
                      (id)[UIColor colorWithWhite:0.02 alpha:1].CGColor, nil];
        }
        locations = nil;
    } else if (SenkoThemeIsIos16()) {
        start = CGPointMake(0.05f, 0.0f);
        end = CGPointMake(0.95f, 1.0f);
        if (SenkoThemeIsLight()) {
            colors = [NSArray arrayWithObjects:
                      (id)[UIColor colorWithRed:0.78 green:0.70 blue:0.96 alpha:1].CGColor,
                      (id)[UIColor colorWithRed:0.98 green:0.78 blue:0.88 alpha:1].CGColor,
                      (id)[UIColor colorWithRed:0.68 green:0.86 blue:0.99 alpha:1].CGColor, nil];
            locations = [NSArray arrayWithObjects:
                         [NSNumber numberWithFloat:0.0f],
                         [NSNumber numberWithFloat:0.48f],
                         [NSNumber numberWithFloat:1.0f], nil];
        } else {
            colors = [NSArray arrayWithObjects:
                      (id)[UIColor colorWithRed:0.12 green:0.05 blue:0.28 alpha:1].CGColor,
                      (id)[UIColor colorWithRed:0.02 green:0.01 blue:0.08 alpha:1].CGColor,
                      (id)[UIColor colorWithRed:0.00 green:0.05 blue:0.18 alpha:1].CGColor, nil];
            locations = [NSArray arrayWithObjects:
                         [NSNumber numberWithFloat:0.0f],
                         [NSNumber numberWithFloat:0.55f],
                         [NSNumber numberWithFloat:1.0f], nil];
        }
    } else {
        colors = [NSArray arrayWithObjects:(id)kBG.CGColor, (id)kBGBot.CGColor, nil];
        locations = nil;
    }

    if (animated) {
        [CATransaction begin];
        [CATransaction setAnimationDuration:0.28];
        g.startPoint = start;
        g.endPoint = end;
        g.locations = locations;
        g.colors = colors;
        [CATransaction commit];
    } else {
        SenkoBeginSilentLayers();
        g.startPoint = start;
        g.endPoint = end;
        g.locations = locations;
        g.colors = colors;
        SenkoEndSilentLayers();
    }
}

static UIImage *gStatusGlowImage;
static NSString *gStatusGlowKey;

/* a raster halo works on ios 6 where radial gradient layers are unavailable */
static UIImage *SenkoStatusGlowImage(NSString *state, CGFloat side) {
    if (side < 8.0f) return nil;
    NSString *s = state ? [state lowercaseString] : @"idle";
    BOOL connecting = [s isEqualToString:@"connecting"];
    BOOL connected = [s isEqualToString:@"connected"];
    BOOL error = [s isEqualToString:@"error"];
    if (!connecting && !connected && !error) return nil;

    /* state changes are rare, but layout and app activation can request the
       same halo repeatedly. keep one exact theme-sized bitmap instead of
       redrawing a full-screen-scale radial gradient on the main thread */
    NSString *cacheKey = [NSString stringWithFormat:@"%@:%d:%@:%ld",
                          SenkoThemeCurrentId(), SenkoThemeIsLight() ? 1 : 0,
                          s, (long)lroundf((float)side)];
    if (gStatusGlowImage && [gStatusGlowKey isEqualToString:cacheKey])
        return gStatusGlowImage;

    BOOL light = SenkoThemeIsLight();
    BOOL miside = SenkoThemeIsMiside();
    BOOL boy = SenkoThemeIsBoykisser();
    BOOL aero = SenkoThemeIsFrutigeraero();
    CGFloat r, g, b, a0;
    if (connecting) {
        if (miside || boy) {
            r = 1.00f; g = light ? 0.45f : 0.38f; b = light ? 0.55f : 0.48f;
            a0 = light ? 0.52f : 0.60f;
        } else if (aero) {
            r = 0.10f; g = light ? 0.75f : 0.65f; b = 1.00f;
            a0 = light ? 0.50f : 0.58f;
        } else {
            r = 1.00f; g = light ? 0.62f : 0.52f; b = light ? 0.08f : 0.04f;
            a0 = light ? 0.55f : 0.62f;
        }
    } else if (error) {
        r = light ? 0.95f : 1.00f;
        g = light ? 0.22f : 0.18f;
        b = light ? 0.26f : 0.22f;
        a0 = light ? 0.52f : 0.60f;
    } else if (miside || boy) {
        r = light ? 1.00f : 1.00f;
        g = light ? 0.28f : 0.22f;
        b = light ? 0.62f : 0.58f;
        a0 = light ? 0.52f : 0.62f;
    } else if (aero) {
        r = light ? 0.05f : 0.10f;
        g = light ? 0.78f : 0.72f;
        b = light ? 0.98f : 0.95f;
        a0 = light ? 0.48f : 0.56f;
    } else {
        r = light ? 0.12f : 0.10f;
        g = light ? 0.88f : 0.82f;
        b = light ? 0.32f : 0.28f;
        a0 = light ? 0.50f : 0.58f;
    }

    CGFloat scale = [UIScreen mainScreen].scale;
    if (scale < 1.0f) scale = 1.0f;
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(side, side), NO, scale);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    if (!ctx) {
        UIGraphicsEndImageContext();
        return nil;
    }
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGFloat comps[12] = {
        r, g, b, a0,
        r, g, b, a0 * 0.35f,
        r, g, b, 0.0f
    };
    CGFloat locs[3] = { 0.0f, 0.42f, 1.0f };
    CGGradientRef grad = CGGradientCreateWithColorComponents(space, comps, locs, 3);
    CGPoint c = CGPointMake(side * 0.5f, side * 0.5f);
    CGContextDrawRadialGradient(ctx, grad, c, 0.0f, c, side * 0.50f,
                                kCGGradientDrawsAfterEndLocation);
    CGGradientRelease(grad);
    CGColorSpaceRelease(space);
    UIImage *img = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    [gStatusGlowImage release];
    gStatusGlowImage = [img retain];
    [gStatusGlowKey release];
    gStatusGlowKey = [cacheKey copy];
    return gStatusGlowImage;
}

void SenkoApplyStatusWash(CALayer *layer, NSString *state, CGFloat side, BOOL animated) {
    if (!layer) return;
    if (side < 8.0f) side = 8.0f;

    UIImage *img = SenkoStatusGlowImage(state, side);
    id contents = img ? (id)img.CGImage : nil;

    if (animated) {
        [CATransaction begin];
        [CATransaction setAnimationDuration:0.32];
        layer.contents = contents;
        layer.opacity = img ? 1.0f : 0.0f;
        layer.hidden = NO;
        [CATransaction commit];
    } else {
        SenkoBeginSilentLayers();
        layer.contents = contents;
        layer.opacity = img ? 1.0f : 0.0f;
        layer.hidden = NO;
        SenkoEndSilentLayers();
    }
}
