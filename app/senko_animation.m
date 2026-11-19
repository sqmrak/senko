#import "senko_animation.h"

#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <math.h>

/* a key every 4 ms: core animation interpolates linearly between keys, so a
   frame of a 120 Hz screen never lands between two far apart ones */
#define MOTION_KEYS_PER_SECOND 240.0
/* a press needs about eight frames at 60 Hz to look like a movement and not like
   a flicker of the picture */
#define BOUNCE_PRESS_SECONDS 0.14
/* how far a screen travels as it comes in or goes away, in points */
#define SWAP_DISTANCE 36.0f
/* a picture may hold this many pixels, which keeps a full screen tablet picture
   from asking for 30 MB */
#define SNAPSHOT_PIXELS_MAX 4000000.0

static NSString *const kMotionKey = @"senko.motion";
static NSString *const kEnterKey = @"senko.enter";
static NSString *const kBounceKey = @"senko.bounce";

BOOL SenkoReduceMotion(void) {
    BOOL (*enabled)(void) = (BOOL (*)(void))dlsym(RTLD_DEFAULT, "UIAccessibilityIsReduceMotionEnabled");
    return enabled ? enabled() : NO;
}

/* the delegate of a move: core animation retains it for the run, the block runs
   once at the end */
@interface SenkoMotionCompletion : NSObject {
    void (^_completion)(BOOL);
}
- (id)initWithCompletion:(void (^)(BOOL))completion;
@end

@implementation SenkoMotionCompletion
- (id)initWithCompletion:(void (^)(BOOL))completion {
    if ((self = [super init])) _completion = [completion copy];
    return self;
}
- (void)dealloc {
    [_completion release];
    [super dealloc];
}
- (void)animationDidStop:(CAAnimation *)animation finished:(BOOL)finished {
    (void)animation;
    if (_completion) _completion(finished);
}
@end

/* the transform and the alpha at every key of a curve, from one state to another.
   the opacity never overshoots, even when the position of a spring does */
static CAAnimationGroup *MotionGroup(CGAffineTransform from, CGFloat fromAlpha,
                                     CGAffineTransform to, CGFloat toAlpha,
                                     senko_motion_curve curve) {
    NSTimeInterval span = senko_motion_duration(curve);
    NSUInteger keys = MAX((NSUInteger)2, (NSUInteger)ceil(span * MOTION_KEYS_PER_SECOND) + 1);
    NSMutableArray *transforms = [NSMutableArray arrayWithCapacity:keys];
    NSMutableArray *opacities = [NSMutableArray arrayWithCapacity:keys];
    for (NSUInteger i = 0; i < keys; ++i) {
        CGFloat progress = (CGFloat)senko_motion_progress(curve, (double)i / (double)(keys - 1));
        CGAffineTransform value = CGAffineTransformMake(
            from.a + (to.a - from.a) * progress, from.b + (to.b - from.b) * progress,
            from.c + (to.c - from.c) * progress, from.d + (to.d - from.d) * progress,
            from.tx + (to.tx - from.tx) * progress, from.ty + (to.ty - from.ty) * progress);
        [transforms addObject:[NSValue valueWithCATransform3D:CATransform3DMakeAffineTransform(value)]];
        CGFloat shown = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
        [opacities addObject:[NSNumber numberWithDouble:fromAlpha + (toAlpha - fromAlpha) * shown]];
    }
    CAKeyframeAnimation *movement = [CAKeyframeAnimation animationWithKeyPath:@"transform"];
    movement.values = transforms;
    CAKeyframeAnimation *fade = [CAKeyframeAnimation animationWithKeyPath:@"opacity"];
    fade.values = opacities;
    movement.duration = fade.duration = span;
    CAAnimationGroup *group = [CAAnimationGroup animation];
    group.duration = span;
    group.animations = [NSArray arrayWithObjects:movement, fade, nil];
    group.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
    return group;
}

void SenkoMotionAnimate(UIView *view, CGAffineTransform transform, CGFloat alpha,
                        senko_motion_curve curve, NSTimeInterval delay,
                        void (^completion)(BOOL finished)) {
    CALayer *layer = view.layer;
    CALayer *visible = [layer animationForKey:kMotionKey] ? [layer presentationLayer] : nil;
    CGAffineTransform from = visible ? CATransform3DGetAffineTransform(visible.transform) : view.transform;
    CGFloat fromAlpha = visible ? visible.opacity : view.alpha;
    [layer removeAnimationForKey:kMotionKey];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    view.transform = transform;
    view.alpha = alpha;
    [CATransaction commit];
    if (!view.window || ![UIView areAnimationsEnabled] || SenkoReduceMotion()) {
        if (completion) completion(YES);
        return;
    }
    CAAnimationGroup *group = MotionGroup(from, fromAlpha, transform, alpha, curve);
    if (delay > 0) {
        group.beginTime = [layer convertTime:CACurrentMediaTime() fromLayer:nil] + delay;
        group.fillMode = kCAFillModeBackwards;
    }
    if (completion)
        group.delegate = (id)[[[SenkoMotionCompletion alloc] initWithCompletion:completion] autorelease];
    [layer addAnimation:group forKey:kMotionKey];
}

void SenkoMotionEnter(UIView *view, CGAffineTransform from, CGFloat fromAlpha,
                      senko_motion_curve curve, NSTimeInterval delay) {
    CALayer *layer = view.layer;
    [layer removeAnimationForKey:kEnterKey];
    if (!(view.window || view.superview) || ![UIView areAnimationsEnabled] || SenkoReduceMotion())
        return;
    CAAnimationGroup *group = MotionGroup(from, fromAlpha, view.transform, view.alpha, curve);
    if (delay > 0) {
        group.beginTime = [layer convertTime:CACurrentMediaTime() fromLayer:nil] + delay;
        group.fillMode = kCAFillModeBackwards;
    }
    [layer addAnimation:group forKey:kEnterKey];
}

/* the scale the layer shows at this moment, which is where a new bounce starts from */
static CGFloat BounceShownScale(CALayer *layer) {
    CALayer *shown = [layer animationForKey:kBounceKey] ? [layer presentationLayer] : nil;
    NSNumber *scale = shown ? [shown valueForKeyPath:@"transform.scale"] : nil;
    return scale ? (CGFloat)[scale doubleValue] : 1.0f;
}

static BOOL BounceAnimates(UIView *view) {
    return view.window && [UIView areAnimationsEnabled] && !SenkoReduceMotion();
}

void SenkoMotionBounce(UIView *view, BOOL pressed, CGFloat depth) {
    CALayer *layer = view.layer;
    if (pressed) {
        if (!BounceAnimates(view)) return;
        CABasicAnimation *shrink = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
        shrink.fromValue = [NSNumber numberWithDouble:BounceShownScale(layer)];
        shrink.toValue = [NSNumber numberWithDouble:1.0 - depth];
        shrink.duration = BOUNCE_PRESS_SECONDS;
        shrink.fillMode = kCAFillModeForwards;
        shrink.removedOnCompletion = NO;
        shrink.timingFunction = [CAMediaTimingFunction functionWithControlPoints:0.25f :0.0f :0.15f :1.0f];
        [layer addAnimation:shrink forKey:kBounceKey];
        return;
    }
    if (![layer animationForKey:kBounceKey]) return;
    CGFloat from = BounceShownScale(layer);
    if (!BounceAnimates(view)) {
        [layer removeAnimationForKey:kBounceKey];
        return;
    }
    /* the release swings back past the size and settles on the loose spring of
       senko_motion, which leaves at rest. the old back ease left at eight times
       its average speed, which is the snap a lifted button used to have */
    NSTimeInterval span = senko_motion_duration(SENKO_MOTION_WOBBLE);
    NSUInteger keys = (NSUInteger)ceil(span * MOTION_KEYS_PER_SECOND) + 1;
    NSMutableArray *values = [NSMutableArray arrayWithCapacity:keys];
    for (NSUInteger i = 0; i < keys; ++i) {
        double progress = senko_motion_progress(SENKO_MOTION_WOBBLE, (double)i / (double)(keys - 1));
        [values addObject:[NSNumber numberWithDouble:from + (1.0 - from) * progress]];
    }
    CAKeyframeAnimation *spring = [CAKeyframeAnimation animationWithKeyPath:@"transform.scale"];
    spring.values = values;
    spring.duration = span;
    spring.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
    [layer addAnimation:spring forKey:kBounceKey];
}

BOOL SenkoMotionBouncing(UIView *view) {
    return [view.layer animationForKey:kBounceKey] != nil;
}

/* the one target of every attached control, so a control needs no object of its own */
@interface SenkoBounceTarget : NSObject
+ (SenkoBounceTarget *)shared;
- (void)press:(UIControl *)control;
- (void)lift:(UIControl *)control;
@end

static char kBounceDepthKey;

@implementation SenkoBounceTarget

+ (SenkoBounceTarget *)shared {
    static SenkoBounceTarget *target = nil;
    if (!target) target = [[SenkoBounceTarget alloc] init];
    return target;
}

- (void)press:(UIControl *)control {
    SenkoMotionBounce(control, YES, (CGFloat)[objc_getAssociatedObject(control, &kBounceDepthKey) doubleValue]);
}

- (void)lift:(UIControl *)control {
    SenkoMotionBounce(control, NO, 0.0f);
}

@end

void SenkoMotionAttachBounce(UIControl *control, CGFloat depth) {
    SenkoBounceTarget *target = [SenkoBounceTarget shared];
    objc_setAssociatedObject(control, &kBounceDepthKey, [NSNumber numberWithDouble:depth],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [control addTarget:target action:@selector(press:)
      forControlEvents:UIControlEventTouchDown | UIControlEventTouchDragEnter];
    [control addTarget:target action:@selector(lift:)
      forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                       UIControlEventTouchCancel | UIControlEventTouchDragExit];
}

#define STAGGER_ROWS_MAX 10
#define STAGGER_STEP_SECONDS 0.03
#define STAGGER_RISE 16.0f
static char kStaggerKey;

void SenkoMotionRiseRows(UITableView *table) {
    if (!table) return;
    [table layoutIfNeeded];
    NSUInteger index = 0;
    for (UITableViewCell *cell in [table visibleCells]) {
        if (index >= STAGGER_ROWS_MAX) break;
        SenkoMotionEnter(cell, CGAffineTransformMakeTranslation(0.0f, STAGGER_RISE), 0.0f,
                         SENKO_MOTION_ENTER, STAGGER_STEP_SECONDS * (double)index);
        ++index;
    }
}

void SenkoMotionStaggerRows(UITableView *table) {
    if (!table || objc_getAssociatedObject(table, &kStaggerKey)) return;
    [table layoutIfNeeded];
    if (![[table visibleCells] count]) return;
    objc_setAssociatedObject(table, &kStaggerKey, [NSNumber numberWithBool:YES],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    SenkoMotionRiseRows(table);
}

UIImageView *SenkoMotionSnapshot(UIView *view) {
    CGSize size = view.bounds.size;
    if (size.width < 1.0f || size.height < 1.0f) return nil;
    CGFloat scale = [UIScreen mainScreen].scale;
    CGFloat limit = (CGFloat)sqrt(SNAPSHOT_PIXELS_MAX / ((double)size.width * (double)size.height));
    if (scale > limit) scale = limit;
    UIGraphicsBeginImageContextWithOptions(size, NO, scale);
    CGContextRef context = UIGraphicsGetCurrentContext();
    if (!context) {
        UIGraphicsEndImageContext();
        return nil;
    }
    [view.layer renderInContext:context];
    UIImage *picture = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    if (!picture) return nil;
    UIImageView *shot = [[[UIImageView alloc] initWithImage:picture] autorelease];
    shot.frame = view.frame;
    shot.userInteractionEnabled = NO;
    return shot;
}

void SenkoMotionSwap(UIView *leaving, UIView *entering, int direction) {
    if (!leaving) return;
    if (!entering.window || ![UIView areAnimationsEnabled] || SenkoReduceMotion()) {
        [leaving removeFromSuperview];
        return;
    }
    CGFloat side = direction < 0 ? -1.0f : 1.0f;
    /* the new screen stays put and opaque under the picture, and only the picture
       moves. two layers that both fade, the old one out and the new one in, are
       each part transparent in the middle of the move and show the window behind
       them, a dark flash in the middle of every swap; and fading the live screen
       as a whole makes the gpu draw it off screen on every frame */
    SenkoMotionAnimate(leaving, CGAffineTransformMakeTranslation(-side * SWAP_DISTANCE, 0.0f), 0.0f,
                       SENKO_MOTION_EXIT, 0.0, ^(BOOL finished) {
        (void)finished;
        [leaving removeFromSuperview];
    });
}

@implementation SenkoNavigationController

/* the swap needs the screen on show and a stack that already has something under
   the new top. the picture is taken before the stack changes and laid over the
   new screen, which is already in its final state */
- (BOOL)swapsScreens:(BOOL)animated {
    return animated && self.view.window && [self.viewControllers count] > 0 &&
           [UIView areAnimationsEnabled] && !SenkoReduceMotion();
}

- (void)runSwap:(int)direction change:(void (^)(void))change {
    UIImageView *shot = SenkoMotionSnapshot(self.view);
    change();
    if (!shot) return;
    [self.view addSubview:shot];
    UIView *content = self.topViewController.view;
    SenkoMotionSwap(shot, content, direction);
}

- (void)pushViewController:(UIViewController *)controller animated:(BOOL)animated {
    if (![self swapsScreens:animated]) {
        [super pushViewController:controller animated:animated];
        return;
    }
    [self runSwap:1 change:^{ [super pushViewController:controller animated:NO]; }];
}

- (UIViewController *)popViewControllerAnimated:(BOOL)animated {
    if (![self swapsScreens:animated] || [self.viewControllers count] < 2)
        return [super popViewControllerAnimated:animated];
    __block UIViewController *popped = nil;
    [self runSwap:-1 change:^{ popped = [super popViewControllerAnimated:NO]; }];
    return popped;
}

- (NSArray *)popToViewController:(UIViewController *)controller animated:(BOOL)animated {
    if (![self swapsScreens:animated] || self.topViewController == controller)
        return [super popToViewController:controller animated:animated];
    __block NSArray *popped = nil;
    [self runSwap:-1 change:^{ popped = [super popToViewController:controller animated:NO]; }];
    return popped;
}

- (NSArray *)popToRootViewControllerAnimated:(BOOL)animated {
    if (![self swapsScreens:animated] || [self.viewControllers count] < 2)
        return [super popToRootViewControllerAnimated:animated];
    __block NSArray *popped = nil;
    [self runSwap:-1 change:^{ popped = [super popToRootViewControllerAnimated:NO]; }];
    return popped;
}

@end

/* the modal slide. the screen is presented without the system transition and
   rises from the bottom on the entry curve, and it leaves as a picture that
   falls away while the real screen is already gone, so the host underneath is
   live at once. the view of a presented controller is a plain subview of the
   transition container on every ios from 5, which is what lets the move run on
   its layer */
void SenkoMotionPresent(UIViewController *host, UIViewController *vc) {
    BOOL moves = host.view.window && [UIView areAnimationsEnabled] && !SenkoReduceMotion();
    if (!moves) {
        [host presentViewController:vc animated:YES completion:nil];
        return;
    }
    [host presentViewController:vc animated:NO completion:nil];
    UIView *screen = vc.view;
    CGFloat travel = screen.window ? screen.window.bounds.size.height : [UIScreen mainScreen].bounds.size.height;
    SenkoMotionEnter(screen, CGAffineTransformMakeTranslation(0.0f, travel), screen.alpha,
                     SENKO_MOTION_ENTER, 0.0);
}

void SenkoMotionDismiss(UIViewController *vc, void (^completion)(void)) {
    UIViewController *target = vc.presentedViewController ? vc.presentedViewController : vc;
    /* a call with nothing presented must stay a no-op: laying a picture over a
       screen that does not go away would leave it sliding off a live screen */
    if (!vc.presentedViewController && !vc.presentingViewController) {
        [vc dismissViewControllerAnimated:YES completion:completion];
        return;
    }
    UIView *screen = target.view;
    UIWindow *window = screen.window;
    BOOL moves = window && [UIView areAnimationsEnabled] && !SenkoReduceMotion();
    if (!moves) {
        [vc dismissViewControllerAnimated:YES completion:completion];
        return;
    }
    UIImageView *shot = SenkoMotionSnapshot(screen);
    if (!shot) {
        [vc dismissViewControllerAnimated:YES completion:completion];
        return;
    }
    shot.frame = [screen.superview convertRect:screen.frame toView:window];
    [window addSubview:shot];
    [vc dismissViewControllerAnimated:NO completion:completion];
    CGFloat travel = window.bounds.size.height - shot.frame.origin.y;
    SenkoMotionAnimate(shot, CGAffineTransformMakeTranslation(0.0f, travel), 1.0f,
                       SENKO_MOTION_DISMISS, 0.0, ^(BOOL finished) {
        (void)finished;
        [shot removeFromSuperview];
    });
}
