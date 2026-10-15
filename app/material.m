#import "material.h"
#import "ui_theme.h"

#include <math.h>

/* ---- curves -------------------------------------------------------------- */

CAMediaTimingFunction *SenkoMaterialStandardCurve(void) {
    return [CAMediaTimingFunction functionWithControlPoints:0.4f :0.0f :0.2f :1.0f];
}

CAMediaTimingFunction *SenkoMaterialDecelerateCurve(void) {
    return [CAMediaTimingFunction functionWithControlPoints:0.0f :0.0f :0.2f :1.0f];
}

CAMediaTimingFunction *SenkoMaterialAccelerateCurve(void) {
    return [CAMediaTimingFunction functionWithControlPoints:0.4f :0.0f :1.0f :1.0f];
}

/* the standard curve evaluated by hand, because the spinner is a table of
   samples and core animation only exposes the curve through its own timing */
static double BezierAt(double t, double a, double b) {
    double u = 1.0 - t;
    return 3.0 * u * u * t * a + 3.0 * u * t * t * b + t * t * t;
}

static double StandardEase(double x) {
    const double x1 = 0.4, y1 = 0.0, x2 = 0.2, y2 = 1.0;
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    double lo = 0.0, hi = 1.0, t = x;
    for (int i = 0; i < 24; ++i) {
        double got = BezierAt(t, x1, x2);
        if (got < x) lo = t; else hi = t;
        t = (lo + hi) * 0.5;
    }
    return BezierAt(t, y1, y2);
}

/* ---- elevation ----------------------------------------------------------- */

void SenkoMaterialElevate(CALayer *layer, CGFloat dp) {
    if (!layer) return;
    BOOL light = SenkoThemeIsLight();
    layer.shadowColor = [UIColor blackColor].CGColor;
    layer.shadowOffset = CGSizeMake(0.0f, MAX(1.0f, dp * 0.5f));
    layer.shadowRadius = 1.0f + dp * 0.6f;
    layer.shadowOpacity = dp <= 0.0f ? 0.0f : (light ? 0.24f : 0.50f);
}

void SenkoMaterialSetElevation(CALayer *layer, CGFloat dp, BOOL animated) {
    if (!layer) return;
    CALayer *shown = [layer presentationLayer];
    if (!shown) shown = layer;
    CGSize fromOffset = shown.shadowOffset;
    CGFloat fromRadius = shown.shadowRadius;
    [layer removeAnimationForKey:@"senko.elevation.offset"];
    [layer removeAnimationForKey:@"senko.elevation.radius"];
    SenkoBeginSilentLayers();
    SenkoMaterialElevate(layer, dp);
    SenkoEndSilentLayers();
    if (!animated) return;
    CABasicAnimation *offset = [CABasicAnimation animationWithKeyPath:@"shadowOffset"];
    offset.fromValue = [NSValue valueWithCGSize:fromOffset];
    offset.toValue = [NSValue valueWithCGSize:layer.shadowOffset];
    offset.duration = 0.15;
    offset.timingFunction = SenkoMaterialStandardCurve();
    [layer addAnimation:offset forKey:@"senko.elevation.offset"];
    CABasicAnimation *radius = [CABasicAnimation animationWithKeyPath:@"shadowRadius"];
    radius.fromValue = [NSNumber numberWithFloat:(float)fromRadius];
    radius.toValue = [NSNumber numberWithFloat:(float)layer.shadowRadius];
    radius.duration = 0.15;
    radius.timingFunction = SenkoMaterialStandardCurve();
    [layer addAnimation:radius forKey:@"senko.elevation.radius"];
}

void SenkoMaterialShadowPath(CALayer *layer, CGFloat radius) {
    if (!layer || layer.bounds.size.width < 1.0f || layer.bounds.size.height < 1.0f) return;
    layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:layer.bounds
                                                  cornerRadius:radius].CGPath;
}

/* ---- the circular progress ring ------------------------------------------ */

static NSString * const kSpinnerRotateKey = @"senko.material.rotate";
static NSString * const kSpinnerSweepKey = @"senko.material.sweep";
static NSString * const kSpinnerFadeKey = @"senko.material.fade";

enum {
    kSpinnerCycles = 4,
    kSpinnerSamples = 270
};
static const double kSpinnerCycle = 1.35;     /* seconds for one grow and catch up */
static const double kSpinnerExpand = 0.494;   /* share of a cycle the arc spends growing */
static const double kSpinnerMinimum = 0.02;   /* shortest arc, in turns */
static const double kSpinnerBaseTurns = 1520.0 / 360.0; /* steady turning per period */

/* the arc is described in turns. while it grows its tail turns with the base
   rotation only; while it shrinks the layer takes up the lost length as extra
   rotation, so the head stays still and the tail catches up with it. each
   cycle adds the same extra turn, and the period is chosen so base and extra
   add to exactly seven turns, which makes the repeat seamless */
void SenkoMaterialSpinnerStart(CAShapeLayer *arc) {
    if (!arc) return;
    if ([arc animationForKey:kSpinnerRotateKey]) return;
    const double period = kSpinnerCycle * kSpinnerCycles;
    const double perCycle = (7.0 - kSpinnerBaseTurns) / kSpinnerCycles;
    const double longest = perCycle + kSpinnerMinimum;

    NSMutableArray *rotation = [NSMutableArray arrayWithCapacity:kSpinnerSamples + 1];
    NSMutableArray *sweep = [NSMutableArray arrayWithCapacity:kSpinnerSamples + 1];
    NSMutableArray *times = [NSMutableArray arrayWithCapacity:kSpinnerSamples + 1];
    for (int i = 0; i <= kSpinnerSamples; ++i) {
        double fraction = (double)i / (double)kSpinnerSamples;
        double t = fraction * period;
        int cycle = (int)floor(t / kSpinnerCycle);
        if (cycle >= kSpinnerCycles) cycle = kSpinnerCycles - 1;
        double u = (t - cycle * kSpinnerCycle) / kSpinnerCycle;
        double length, extra;
        if (u < kSpinnerExpand) {
            double p = StandardEase(u / kSpinnerExpand);
            length = kSpinnerMinimum + (longest - kSpinnerMinimum) * p;
            extra = cycle * perCycle;
        } else {
            double p = StandardEase((u - kSpinnerExpand) / (1.0 - kSpinnerExpand));
            length = longest - (longest - kSpinnerMinimum) * p;
            extra = cycle * perCycle + perCycle * p;
        }
        double turns = kSpinnerBaseTurns * fraction + extra;
        [rotation addObject:[NSNumber numberWithDouble:turns * 2.0 * M_PI]];
        [sweep addObject:[NSNumber numberWithDouble:length]];
        [times addObject:[NSNumber numberWithDouble:fraction]];
    }

    SenkoBeginSilentLayers();
    arc.strokeStart = 0.0f;
    arc.strokeEnd = (CGFloat)kSpinnerMinimum;
    SenkoEndSilentLayers();

    CAKeyframeAnimation *turn = [CAKeyframeAnimation animationWithKeyPath:@"transform.rotation.z"];
    turn.values = rotation;
    turn.keyTimes = times;
    turn.calculationMode = kCAAnimationLinear;
    turn.duration = period;
    turn.repeatCount = HUGE_VALF;
    turn.removedOnCompletion = NO;
    [arc addAnimation:turn forKey:kSpinnerRotateKey];

    CAKeyframeAnimation *grow = [CAKeyframeAnimation animationWithKeyPath:@"strokeEnd"];
    grow.values = sweep;
    grow.keyTimes = times;
    grow.calculationMode = kCAAnimationLinear;
    grow.duration = period;
    grow.repeatCount = HUGE_VALF;
    grow.removedOnCompletion = NO;
    [arc addAnimation:grow forKey:kSpinnerSweepKey];

    [arc removeAnimationForKey:kSpinnerFadeKey];
    CABasicAnimation *in = [CABasicAnimation animationWithKeyPath:@"opacity"];
    in.fromValue = [NSNumber numberWithFloat:0.0f];
    in.toValue = [NSNumber numberWithFloat:1.0f];
    in.duration = 0.2;
    in.timingFunction = SenkoMaterialDecelerateCurve();
    SenkoBeginSilentLayers();
    arc.opacity = 1.0f;
    SenkoEndSilentLayers();
    [arc addAnimation:in forKey:kSpinnerFadeKey];
}

void SenkoMaterialSpinnerStop(CAShapeLayer *arc) {
    if (!arc) return;
    if (![arc animationForKey:kSpinnerRotateKey]) {
        SenkoBeginSilentLayers();
        arc.opacity = 0.0f;
        SenkoEndSilentLayers();
        return;
    }
    CALayer *shown = [arc presentationLayer];
    float from = shown ? shown.opacity : arc.opacity;
    SenkoBeginSilentLayers();
    arc.opacity = 0.0f;
    SenkoEndSilentLayers();
    [CATransaction begin];
    /* the ring keeps turning while it fades, so it never freezes in place */
    [CATransaction setCompletionBlock:^{
        if (arc.opacity == 0.0f) {
            [arc removeAnimationForKey:kSpinnerRotateKey];
            [arc removeAnimationForKey:kSpinnerSweepKey];
        }
    }];
    CABasicAnimation *out = [CABasicAnimation animationWithKeyPath:@"opacity"];
    out.fromValue = [NSNumber numberWithFloat:from];
    out.toValue = [NSNumber numberWithFloat:0.0f];
    out.duration = 0.2;
    out.timingFunction = SenkoMaterialAccelerateCurve();
    [arc addAnimation:out forKey:kSpinnerFadeKey];
    [CATransaction commit];
}

void SenkoMaterialPulse(CALayer *host, CGPoint center, CGFloat radius, UIColor *color) {
    if (!host || radius < 1.0f) return;
    CAShapeLayer *ring = [CAShapeLayer layer];
    ring.bounds = CGRectMake(0.0f, 0.0f, radius * 2.0f, radius * 2.0f);
    ring.position = center;
    ring.path = [UIBezierPath bezierPathWithOvalInRect:ring.bounds].CGPath;
    ring.fillColor = [color colorWithAlphaComponent:0.28f].CGColor;
    ring.opacity = 0.0f;
    [host insertSublayer:ring atIndex:0];

    [CATransaction begin];
    [CATransaction setCompletionBlock:^{ [ring removeFromSuperlayer]; }];
    CABasicAnimation *grow = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
    grow.fromValue = [NSNumber numberWithFloat:1.0f];
    grow.toValue = [NSNumber numberWithFloat:1.45f];
    CABasicAnimation *fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
    fade.fromValue = [NSNumber numberWithFloat:1.0f];
    fade.toValue = [NSNumber numberWithFloat:0.0f];
    CAAnimationGroup *group = [CAAnimationGroup animation];
    group.animations = [NSArray arrayWithObjects:grow, fade, nil];
    group.duration = 0.55;
    group.timingFunction = SenkoMaterialDecelerateCurve();
    [ring addAnimation:group forKey:@"senko.material.pulse"];
    [CATransaction commit];
}

/* ---- ripple -------------------------------------------------------------- */

static const CFTimeInterval kRippleSpread = 0.38;  /* seconds to reach the far corner */
static const CFTimeInterval kRippleFade = 0.26;
/* a touch inside a scrolling list may be the start of a scroll, so the wave
   waits a moment, the way a scroll view waits before it hands a touch down */
static const CFTimeInterval kRippleScrollDelay = 0.10;
static const CGFloat kRippleScrollSlop = 8.0f;
static const CGFloat kRippleLeaveSlop = 24.0f;

static NSString * const kRippleLayerName = @"senko.ripple";

@interface SenkoRipple : NSObject {
@public
    UIView *host;
    CGPoint start;      /* window coordinates, to tell a scroll from a press */
    CGPoint point;      /* host coordinates */
    CALayer *clip;
    CFTimeInterval spawned;
    BOOL ended;
}
- (void)spawn;
- (void)lift;
- (void)cancel;
@end

static BOOL RippleLight(UIColor *color) {
    CGFloat r, g, b, a;
    if (!SenkoColorComponents(color, &r, &g, &b, &a)) return NO;
    return 0.299f * r + 0.587f * g + 0.114f * b > 0.7f;
}

/* ink at about a tenth over a light surface, white at a bit more over a dark
   one, and white on a filled control whose own label is light */
static UIColor *RippleColor(UIView *host) {
    if ([host respondsToSelector:@selector(senkoRippleColor)]) {
        UIColor *own = [(id<SenkoRippleHost>)host senkoRippleColor];
        if (own) return own;
    }
    if ([host isKindOfClass:[UIButton class]]) {
        UIColor *title = [(UIButton *)host titleColorForState:UIControlStateNormal];
        if (title && RippleLight(title)) return [UIColor colorWithWhite:1.0f alpha:0.28f];
    }
    return SenkoThemeIsLight() ? [UIColor colorWithWhite:0.0f alpha:0.10f]
                               : [UIColor colorWithWhite:1.0f alpha:0.14f];
}

@implementation SenkoRipple

- (void)dealloc {
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [clip removeFromSuperlayer];
    [clip release];
    [host release];
    [super dealloc];
}

- (void)spawn {
    if (clip || !host.window) return;
    CGRect rect = host.bounds;
    CGFloat radius = host.layer.cornerRadius;
    if ([host respondsToSelector:@selector(senkoRippleBounds)])
        rect = [(id<SenkoRippleHost>)host senkoRippleBounds];
    if ([host respondsToSelector:@selector(senkoRippleRadius)])
        radius = [(id<SenkoRippleHost>)host senkoRippleRadius];
    if (rect.size.width < 1.0f || rect.size.height < 1.0f) return;

    CGPoint p = CGPointMake(MIN(MAX(point.x - rect.origin.x, 0.0f), rect.size.width),
                            MIN(MAX(point.y - rect.origin.y, 0.0f), rect.size.height));
    CGFloat dx = MAX(p.x, rect.size.width - p.x);
    CGFloat dy = MAX(p.y, rect.size.height - p.y);
    CGFloat reach = sqrtf(dx * dx + dy * dy);

    clip = [[CALayer alloc] init];
    clip.name = kRippleLayerName;
    clip.frame = rect;
    clip.masksToBounds = YES;
    clip.cornerRadius = radius;
    CALayer *wave = [CALayer layer];
    wave.bounds = CGRectMake(0.0f, 0.0f, reach * 2.0f, reach * 2.0f);
    wave.cornerRadius = reach;
    wave.position = p;
    wave.backgroundColor = RippleColor(host).CGColor;
    [clip addSublayer:wave];
    [host.layer addSublayer:clip];
    spawned = CACurrentMediaTime();

    CABasicAnimation *spread = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
    spread.fromValue = [NSNumber numberWithFloat:0.05f];
    spread.toValue = [NSNumber numberWithFloat:1.0f];
    spread.duration = kRippleSpread;
    spread.timingFunction = SenkoMaterialDecelerateCurve();
    [wave addAnimation:spread forKey:@"spread"];
    if (ended) [self fade];
}

/* the wave finishes spreading before it fades, even when the finger left first */
- (void)fade {
    if (!clip) return;
    CFTimeInterval wait = MAX(0.0, kRippleSpread - (CACurrentMediaTime() - spawned));
    CABasicAnimation *out = [CABasicAnimation animationWithKeyPath:@"opacity"];
    out.fromValue = [NSNumber numberWithFloat:1.0f];
    out.toValue = [NSNumber numberWithFloat:0.0f];
    out.beginTime = [clip convertTime:CACurrentMediaTime() fromLayer:nil] + wait;
    out.duration = kRippleFade;
    out.fillMode = kCAFillModeBoth;
    out.removedOnCompletion = NO;
    [clip addAnimation:out forKey:@"fade"];
    [self performSelector:@selector(finish) withObject:nil afterDelay:wait + kRippleFade + 0.05];
}

- (void)finish {
    [clip removeFromSuperlayer];
}

- (void)lift {
    if (ended) return;
    ended = YES;
    if (clip) [self fade];
}

- (void)cancel {
    ended = YES;
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [clip removeFromSuperlayer];
}

@end

/* controls whose own drawing already shows the touch, and fields a ripple
   would only get in the way of */
static BOOL RippleExcluded(UIView *view) {
    return [view isKindOfClass:[UISwitch class]] ||
           [view isKindOfClass:[UISegmentedControl class]] ||
           [view isKindOfClass:[UISlider class]] ||
           [view isKindOfClass:[UITextField class]] ||
           [view isKindOfClass:[UIStepper class]] ||
           [view isKindOfClass:[UIPageControl class]];
}

static UIView *RippleHostFor(UIView *view) {
    for (UIView *v = view; v; v = v.superview) {
        if ([v isKindOfClass:[UIWindow class]]) return nil;
        if (RippleExcluded(v)) return nil;
        if ([v isKindOfClass:[UITableViewCell class]]) return v;
        if ([v isKindOfClass:[UIControl class]]) {
            if (!((UIControl *)v).enabled) return nil;
            return v;
        }
    }
    return nil;
}

static BOOL RippleInScroller(UIView *view) {
    for (UIView *v = view.superview; v; v = v.superview)
        if ([v isKindOfClass:[UIScrollView class]]) return YES;
    return NO;
}

/* a control the size of the screen is a tap catcher, and a wave across the
   whole screen would be a flash, not feedback */
static BOOL RippleTooLarge(UIView *host) {
    UIWindow *w = host.window;
    if (!w) return YES;
    CGRect r = [host convertRect:host.bounds toView:nil];
    return r.size.width * r.size.height > w.bounds.size.width * w.bounds.size.height * 0.5f;
}

static NSMutableDictionary *gRipples;

static void RippleObserve(UIEvent *event) {
    if (!gRipples) gRipples = [[NSMutableDictionary alloc] init];
    for (UITouch *touch in [event allTouches]) {
        NSValue *key = [NSValue valueWithPointer:touch];
        SenkoRipple *ripple = [gRipples objectForKey:key];
        switch (touch.phase) {
        case UITouchPhaseBegan: {
            UIView *host = RippleHostFor(touch.view);
            if (!host || RippleTooLarge(host)) break;
            SenkoRipple *fresh = [[[SenkoRipple alloc] init] autorelease];
            fresh->host = [host retain];
            fresh->start = [touch locationInView:nil];
            fresh->point = [touch locationInView:host];
            [gRipples setObject:fresh forKey:key];
            if (RippleInScroller(host))
                [fresh performSelector:@selector(spawn) withObject:nil
                            afterDelay:kRippleScrollDelay];
            else
                [fresh spawn];
            break;
        }
        case UITouchPhaseMoved: {
            if (!ripple) break;
            if (!ripple->clip) {
                CGPoint now = [touch locationInView:nil];
                if (hypotf(now.x - ripple->start.x, now.y - ripple->start.y) > kRippleScrollSlop) {
                    [ripple cancel];
                    [gRipples removeObjectForKey:key];
                }
                break;
            }
            CGRect near = CGRectInset(ripple->host.bounds, -kRippleLeaveSlop, -kRippleLeaveSlop);
            if (!CGRectContainsPoint(near, [touch locationInView:ripple->host])) {
                [ripple lift];
                [gRipples removeObjectForKey:key];
            }
            break;
        }
        case UITouchPhaseEnded:
            if (!ripple) break;
            /* a tap that ended inside the wait still shows its wave */
            if (!ripple->clip) {
                [NSObject cancelPreviousPerformRequestsWithTarget:ripple
                                                         selector:@selector(spawn) object:nil];
                [ripple spawn];
            }
            [ripple lift];
            [gRipples removeObjectForKey:key];
            break;
        case UITouchPhaseCancelled:
            if (!ripple) break;
            [NSObject cancelPreviousPerformRequestsWithTarget:ripple
                                                     selector:@selector(spawn) object:nil];
            if (ripple->clip) [ripple lift];
            else [ripple cancel];
            [gRipples removeObjectForKey:key];
            break;
        default:
            break;
        }
    }
}

@implementation SenkoApplication

- (void)sendEvent:(UIEvent *)event {
    if (event.type == UIEventTypeTouches && SenkoThemeIsMaterial())
        RippleObserve(event);
    [super sendEvent:event];
}

@end
