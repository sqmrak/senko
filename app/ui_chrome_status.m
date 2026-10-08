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
    } else if (SenkoThemeIsMaterial()) {
/* the page is one flat colour, with no ramp for a surface to be lit by */
        colors = [NSArray arrayWithObjects:(id)kBG.CGColor, (id)kBG.CGColor, nil];
        locations = nil;
    } else {
/* the palettes pick kBG and kBGBot a step apart at most, which read as a flat
   fill on an ios 5 panel, so the ramp is widened from both ends */
        BOOL light = SenkoThemeIsLight();
        colors = [NSArray arrayWithObjects:
                  (id)SenkoShadeColor(kBG, light ? 0.05f : 0.09f).CGColor,
                  (id)kBG.CGColor,
                  (id)SenkoShadeColor(kBGBot, light ? -0.07f : -0.05f).CGColor, nil];
        locations = [NSArray arrayWithObjects:
                     [NSNumber numberWithFloat:0.0f],
                     [NSNumber numberWithFloat:0.45f],
                     [NSNumber numberWithFloat:1.0f], nil];
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

