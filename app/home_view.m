#import "home_view.h"
#import "ui_theme.h"
#import "app_common.h"
#import "server_cell.h"

#include <math.h>

static NSString * const kSenkoOrbitSpinKey = @"senko.orbit.spin";
static NSString * const kSenkoOrbitPulseKey = @"senko.orbit.pulse";
static NSString * const kSenkoOrbitFadeKey = @"senko.orbit.fade";
static const CFTimeInterval kSenkoOrbitFadeTime = 0.45;
static const CGFloat kSenkoOrbitDotRest = 0.2f;

static BOOL SenkoHomeIsPad(void) {
    return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad;
}

static UIColor *SenkoHomeStateTint(NSString *key) {
    if ([key isEqualToString:@"connected"])
        return [UIColor colorWithRed:0.24 green:0.78 blue:0.36 alpha:1.0];
    if ([key isEqualToString:@"error"])
        return [UIColor colorWithRed:0.95 green:0.30 blue:0.26 alpha:1.0];
    return kAccentBlue;
}

/* seconds per lap while the tunnel is active */
static CFTimeInterval SenkoOrbitPeriod(NSString *key) {
    if ([key isEqualToString:@"connecting"]) return 1.4;
    return 4.5;
}

static CGPathRef SenkoCirclePath(CGPoint c, CGFloat r) {
    return [UIBezierPath bezierPathWithArcCenter:c radius:r startAngle:0.0f
                                        endAngle:(CGFloat)(M_PI * 2.0)
                                       clockwise:YES].CGPath;
}

@implementation SenkoOrbitButton

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.backgroundColor = [UIColor clearColor];
        self.exclusiveTouch = YES;
        _stateKey = [@"idle" copy];

        _track = [[CAShapeLayer alloc] init];
        _track.fillColor = [UIColor clearColor].CGColor;
        _track.lineWidth = 1.0f;
        [self.layer addSublayer:_track];

        _halo = [[CAShapeLayer alloc] init];
        _halo.shadowOffset = CGSizeZero;
        [self.layer addSublayer:_halo];

        _face = [[CAShapeLayer alloc] init];
        _face.shadowColor = [UIColor blackColor].CGColor;
        _face.shadowOffset = CGSizeMake(0.0f, 3.0f);
        _face.shadowRadius = 5.0f;
        [self.layer addSublayer:_face];
        _faceFill = [[CAGradientLayer alloc] init];
        _faceFill.startPoint = CGPointMake(0.5f, 0.0f);
        _faceFill.endPoint = CGPointMake(0.5f, 1.0f);
        _faceFill.masksToBounds = YES;
        [self.layer addSublayer:_faceFill];
        _faceSheen = [[CAGradientLayer alloc] init];
        _faceSheen.startPoint = CGPointMake(0.5f, 0.0f);
        _faceSheen.endPoint = CGPointMake(0.5f, 1.0f);
        [_faceFill addSublayer:_faceSheen];

        _ring = [[CAShapeLayer alloc] init];
        _ring.fillColor = [UIColor clearColor].CGColor;
        _ring.shadowOffset = CGSizeZero;
/* the glow is a blur of a still stroke; caching it keeps the spinning dot from
   re-rendering the shadow on every frame of an armv7 device */
        _ring.shouldRasterize = YES;
        _ring.rasterizationScale = [UIScreen mainScreen].scale;
        [self.layer addSublayer:_ring];

        _glyph = [[CAShapeLayer alloc] init];
        _glyph.fillColor = [UIColor clearColor].CGColor;
        _glyph.lineCap = kCALineCapRound;
        [self.layer addSublayer:_glyph];

        _orbit = [[CALayer alloc] init];
        [self.layer addSublayer:_orbit];
        _trail = [[CAShapeLayer alloc] init];
        _trail.fillColor = [UIColor clearColor].CGColor;
        _trail.lineCap = kCALineCapRound;
        _trail.lineWidth = 2.0f;
        [_orbit addSublayer:_trail];
        _dot = [[CAShapeLayer alloc] init];
        _dot.shadowOffset = CGSizeZero;
        _dot.shadowRadius = 5.0f;
        _dot.shadowOpacity = 0.9f;
        [_orbit addSublayer:_dot];
        /* the dot starts off the ring: no tail, shrunk, transparent */
        _orbit.opacity = 0.0f;
        _trail.strokeStart = 1.0f;
        _dot.transform = CATransform3DMakeScale(kSenkoOrbitDotRest, kSenkoOrbitDotRest, 1.0f);

        [self applyTheme];
    }
    return self;
}

- (void)dealloc {
    [_track release];
    [_halo release];
    [_face release];
    [_faceFill release];
    [_faceSheen release];
    [_ring release];
    [_glyph release];
    [_trail release];
    [_dot release];
    [_orbit release];
    [_stateKey release];
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGSize size = self.bounds.size;
    if (size.width < 1.0f || size.height < 1.0f) return;
    if (CGSizeEqualToSize(size, _laidSize)) return;
    _laidSize = size;

    CGRect b = self.bounds;
    CGFloat side = MIN(size.width, size.height);
    CGPoint c = CGPointMake(size.width * 0.5f, size.height * 0.5f);
    CGFloat dotR = MAX(4.0f, side * 0.018f);
    CGFloat trackR = side * 0.5f - dotR - 3.0f;
    CGFloat ringR = trackR * 0.76f;
    CGFloat g = ringR * 0.34f;

    UIBezierPath *power = [UIBezierPath bezierPathWithArcCenter:c radius:g
        startAngle:(CGFloat)(-M_PI_2 + 0.62) endAngle:(CGFloat)(-M_PI_2 - 0.62 + M_PI * 2.0)
         clockwise:YES];
    [power moveToPoint:CGPointMake(c.x, c.y - g * 1.22f)];
    [power addLineToPoint:CGPointMake(c.x, c.y - g * 0.30f)];

    SenkoBeginSilentLayers();
    _track.frame = b;
    _halo.frame = b;
    _face.frame = b;
    _ring.frame = b;
    _glyph.frame = b;
    _orbit.bounds = b;
    _orbit.position = c;
    _trail.frame = b;
/* the dot is its own small layer so it can grow from its centre; frame is
   undefined under the scale it rests at */
    _dot.bounds = CGRectMake(0.0f, 0.0f, dotR * 2.0f, dotR * 2.0f);
    _dot.position = CGPointMake(c.x + trackR, c.y);
    _track.path = SenkoCirclePath(c, trackR);
    CGFloat faceR = ringR - 1.0f;
    CGRect faceRect = CGRectMake(c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);
    _halo.path = SenkoCirclePath(c, ringR);
    _halo.shadowPath = _halo.path;
    _face.path = SenkoCirclePath(c, faceR);
    _face.shadowPath = _face.path;
    _faceFill.frame = faceRect;
    _faceFill.cornerRadius = faceR;
    _faceSheen.frame = CGRectMake(0.0f, 0.0f, faceR * 2.0f, faceR);
    _ring.path = SenkoCirclePath(c, ringR);
    _ring.lineWidth = MAX(2.5f, side * 0.013f);
    _glyph.path = power.CGPath;
    _glyph.lineWidth = MAX(3.0f, ringR * 0.075f);
    _trail.path = [UIBezierPath bezierPathWithArcCenter:c radius:trackR
                                             startAngle:-1.05f endAngle:0.0f
                                              clockwise:YES].CGPath;
    _dot.path = SenkoCirclePath(CGPointMake(dotR, dotR), dotR);
    _dot.shadowPath = _dot.path;
    SenkoEndSilentLayers();
}

/* at rest the button is lit only by the theme. a tunnel that is up, dialing or
   failed gives it a dim halo in its state colour */
- (void)applyTheme {
    UIColor *tint = SenkoHomeStateTint(_stateKey);
    BOOL idle = [_stateKey isEqualToString:@"idle"];
    BOOL light = SenkoThemeIsLight();
    BOOL flat = SenkoThemeIsFlat() && !SenkoThemeIsIos16();
    SenkoBeginSilentLayers();
    _track.strokeColor = [tint colorWithAlphaComponent:0.22f].CGColor;
    _halo.fillColor = tint.CGColor;
    _halo.shadowColor = tint.CGColor;
    _halo.shadowRadius = 14.0f;
    _halo.shadowOpacity = idle ? 0.0f : 0.32f;
    _halo.hidden = idle;
    _face.fillColor = kCellLo.CGColor;
    _face.strokeColor = [UIColor clearColor].CGColor;
    _face.shadowOpacity = flat ? 0.0f : (light ? 0.22f : 0.5f);
    _faceFill.colors = [NSArray arrayWithObjects:
                        (id)SenkoShadeColor(kCellHi, flat ? -0.03f : 0.04f).CGColor,
                        (id)SenkoShadeColor(kCellHi, flat ? -0.03f : -0.14f).CGColor, nil];
    _faceSheen.hidden = flat;
    _faceSheen.colors = [NSArray arrayWithObjects:
                         (id)[UIColor colorWithWhite:1.0f alpha:light ? 0.30f : 0.12f].CGColor,
                         (id)[UIColor colorWithWhite:1.0f alpha:0.0f].CGColor, nil];
    _ring.strokeColor = tint.CGColor;
    _ring.shadowColor = tint.CGColor;
    _ring.shadowRadius = 6.0f;
    _ring.shadowOpacity = idle ? 0.0f : 0.45f;
    _glyph.strokeColor = tint.CGColor;
    _trail.strokeColor = [tint colorWithAlphaComponent:0.45f].CGColor;
    _dot.fillColor = tint.CGColor;
    _dot.shadowColor = tint.CGColor;
    _dot.shadowOpacity = idle ? 0.35f : 0.8f;
    /* the aero wallpaper behind the orbit is sky and glass in the accent's own
       cyan, so the tinted dot and its glow vanished while connecting. a white
       rim and a navy shadow keep the state colour and read on sky and cloud */
    BOOL cased = SenkoThemeIsFrutigeraero();
    _dot.strokeColor = cased ? [UIColor whiteColor].CGColor : [UIColor clearColor].CGColor;
    _dot.lineWidth = cased ? 1.5f : 0.0f;
    if (cased) {
/* navy ink shades the dot on the sky; on the night wallpaper ink is white,
   so the shade has to be black to keep reading as a shadow */
        _dot.shadowColor = (light ? kInk : [UIColor blackColor]).CGColor;
        _dot.shadowRadius = 3.0f;
        _dot.shadowOpacity = 0.6f;
        _trail.strokeColor = [tint colorWithAlphaComponent:0.9f].CGColor;
    } else {
        _dot.shadowRadius = 5.0f;
    }
    _trail.lineWidth = cased ? 3.0f : 2.0f;
    _trail.shadowColor = kInk.CGColor;
    _trail.shadowOffset = CGSizeZero;
    _trail.shadowRadius = 2.0f;
    _trail.shadowOpacity = cased ? 0.5f : 0.0f;
/* the orbit turns every frame; a cached bitmap keeps these shadows from being
   blurred again on each one on armv7 */
    _dot.shouldRasterize = cased;
    _trail.shouldRasterize = cased;
    _dot.rasterizationScale = [UIScreen mainScreen].scale;
    _trail.rasterizationScale = [UIScreen mainScreen].scale;
    SenkoEndSilentLayers();
}

- (BOOL)orbitWanted {
    return _running && ([_stateKey isEqualToString:@"connecting"] ||
                        [_stateKey isEqualToString:@"connected"]);
}

- (double)shownAngle {
    CALayer *shown = [_orbit presentationLayer];
    if (!shown) shown = _orbit;
    return [[shown valueForKeyPath:@"transform.rotation.z"] doubleValue];
}

/* the dot keeps its place when the speed changes: a new lap starts from
   wherever the old one is on screen, so a state switch never makes it jump */
- (void)stopSpin {
    double angle = [self shownAngle];
    [_orbit removeAnimationForKey:kSenkoOrbitSpinKey];
    SenkoBeginSilentLayers();
    _orbit.transform = CATransform3DMakeRotation((CGFloat)angle, 0.0f, 0.0f, 1.0f);
    SenkoEndSilentLayers();
}

- (void)restartSpin {
    double angle = [self shownAngle];
    [self stopSpin];
    CABasicAnimation *spin = [CABasicAnimation animationWithKeyPath:@"transform.rotation.z"];
    spin.fromValue = [NSNumber numberWithDouble:angle];
    spin.toValue = [NSNumber numberWithDouble:angle + M_PI * 2.0];
    spin.duration = SenkoOrbitPeriod(_stateKey);
    spin.repeatCount = HUGE_VALF;
    spin.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
    [_orbit addAnimation:spin forKey:kSenkoOrbitSpinKey];
}

static void SenkoOrbitTween(CALayer *layer, NSString *path, id from, id to, BOOL show) {
    CABasicAnimation *tween = [CABasicAnimation animationWithKeyPath:path];
    tween.fromValue = from;
    tween.toValue = to;
    tween.duration = kSenkoOrbitFadeTime;
    tween.timingFunction = [CAMediaTimingFunction functionWithName:
        show ? kCAMediaTimingFunctionEaseOut : kCAMediaTimingFunctionEaseIn];
    [layer addAnimation:tween forKey:kSenkoOrbitFadeKey];
}

/* the ring stays put; the dot grows its tail on it when a tunnel starts and
   pulls it back when the tunnel stops. it keeps circling while it fades out,
   so it never freezes in place, and a new start mid-fade turns it around from
   wherever it is on screen */
- (void)showOrbit:(BOOL)show animated:(BOOL)animated {
    if (show == _orbitShown) return;
    _orbitShown = show;
    CALayer *orbitNow = [_orbit presentationLayer];
    CAShapeLayer *trailNow = (CAShapeLayer *)[_trail presentationLayer];
    CALayer *dotNow = [_dot presentationLayer];
    NSNumber *opacityFrom = [NSNumber numberWithFloat:(orbitNow ? orbitNow : _orbit).opacity];
    NSNumber *startFrom = [NSNumber numberWithFloat:(trailNow ? trailNow : _trail).strokeStart];
    NSValue *scaleFrom = [NSValue valueWithCATransform3D:(dotNow ? dotNow : _dot).transform];
    CATransform3D scale = show ? CATransform3DIdentity
        : CATransform3DMakeScale(kSenkoOrbitDotRest, kSenkoOrbitDotRest, 1.0f);

    [_orbit removeAnimationForKey:kSenkoOrbitFadeKey];
    [_trail removeAnimationForKey:kSenkoOrbitFadeKey];
    [_dot removeAnimationForKey:kSenkoOrbitFadeKey];
    SenkoBeginSilentLayers();
    _orbit.opacity = show ? 1.0f : 0.0f;
    _trail.strokeStart = show ? 0.0f : 1.0f;
    _dot.transform = scale;
    SenkoEndSilentLayers();
    if (!animated) {
        if (!show) [self stopSpin];
        return;
    }

    [CATransaction begin];
    if (!show) {
        [CATransaction setCompletionBlock:^{
            if (!_orbitShown) [self stopSpin];
        }];
    }
    SenkoOrbitTween(_orbit, @"opacity", opacityFrom,
                    [NSNumber numberWithFloat:_orbit.opacity], show);
    SenkoOrbitTween(_trail, @"strokeStart", startFrom,
                    [NSNumber numberWithFloat:_trail.strokeStart], show);
    SenkoOrbitTween(_dot, @"transform", scaleFrom,
                    [NSValue valueWithCATransform3D:scale], show);
    [CATransaction commit];
}

/* a state switch animates the dot; leaving or returning to the screen only
   puts it where it belongs */
- (void)syncOrbitAnimated:(BOOL)animated restart:(BOOL)restart {
    if ([self orbitWanted]) {
        if (restart || ![_orbit animationForKey:kSenkoOrbitSpinKey]) [self restartSpin];
        [self showOrbit:YES animated:animated];
    } else {
        [self showOrbit:NO animated:animated];
    }
}

- (void)syncPulse {
    BOOL want = _running && [_stateKey isEqualToString:@"connecting"];
    if (!want) {
        [_ring removeAnimationForKey:kSenkoOrbitPulseKey];
        return;
    }
    if ([_ring animationForKey:kSenkoOrbitPulseKey]) return;
    CABasicAnimation *pulse = [CABasicAnimation animationWithKeyPath:@"opacity"];
    pulse.fromValue = [NSNumber numberWithFloat:1.0f];
    pulse.toValue = [NSNumber numberWithFloat:0.35f];
    pulse.duration = 0.7;
    pulse.autoreverses = YES;
    pulse.repeatCount = HUGE_VALF;
    [_ring addAnimation:pulse forKey:kSenkoOrbitPulseKey];
}

/* core animation drops layer animations while the app is in the background,
   so every state write also puts back whatever went missing */
- (void)setStateKey:(NSString *)key {
    if (![key length]) key = @"idle";
    BOOL changed = ![key isEqualToString:_stateKey];
    if (changed) {
        [_stateKey release];
        _stateKey = [key copy];
    }
    [self applyTheme];
    [self syncOrbitAnimated:changed restart:changed];
    [self syncPulse];
}

- (void)setRunning:(BOOL)running {
    BOOL changed = _running != running;
    _running = running;
    [self syncOrbitAnimated:NO restart:changed];
    [self syncPulse];
}

- (void)setHighlighted:(BOOL)highlighted {
    [super setHighlighted:highlighted];
    SenkoPressPop(self, highlighted);
}

@end

static NSString * const kSenkoPlateFill = @"homePlateFill";
static NSString * const kSenkoPlateSheen = @"homePlateSheen";

static CAGradientLayer *SenkoPlateLayer(UIView *plate, NSString *name, CALayer *above) {
    for (CALayer *layer in plate.layer.sublayers)
        if ([layer.name isEqualToString:name] && [layer isKindOfClass:[CAGradientLayer class]])
            return (CAGradientLayer *)layer;
    CAGradientLayer *layer = [CAGradientLayer layer];
    layer.name = name;
    layer.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                     [NSNull null], @"bounds", [NSNull null], @"position",
                     [NSNull null], @"colors", [NSNull null], @"cornerRadius",
                     [NSNull null], @"hidden", nil];
    layer.startPoint = CGPointMake(0.5f, 0.0f);
    layer.endPoint = CGPointMake(0.5f, 1.0f);
    if (above) [plate.layer insertSublayer:layer above:above];
    else [plate.layer insertSublayer:layer atIndex:0];
    return layer;
}

/* the plates share the list card colours, so a theme restyles the home screen
   without a second palette. ios7 is the one theme with no light source, so it
   keeps a flat plate */
void SenkoStyleHomePlate(UIView *plate, CGFloat radius, UIColor *base) {
    BOOL light = SenkoThemeIsLight();
    BOOL flat = SenkoThemeIsFlat() && !SenkoThemeIsIos16();
    UIColor *top;
    UIColor *bottom;
    if (base) {
        top = SenkoShadeColor(base, 0.12f);
        bottom = SenkoShadeColor(base, -0.10f);
    } else if (SenkoThemeIsIos26()) {
        top = [UIColor colorWithWhite:1.0f alpha:light ? 0.42f : 0.18f];
        bottom = [UIColor colorWithWhite:1.0f alpha:light ? 0.28f : 0.08f];
    } else {
        UIColor *card = kCellHi;
        if (SenkoThemeIsMiside() || SenkoThemeIsFrutigeraero() || SenkoThemeIsBoykisser())
            card = [kCellHi colorWithAlphaComponent:0.86f];
        top = SenkoShadeColor(card, light ? 0.01f : 0.02f);
        bottom = SenkoShadeColor(card, light ? -0.08f : -0.10f);
    }
    if (flat) bottom = top;
    plate.backgroundColor = [UIColor clearColor];
    plate.layer.cornerRadius = radius;
    plate.layer.masksToBounds = NO;
    plate.layer.borderWidth = 0.5f;
    plate.layer.borderColor = (light ? [UIColor colorWithWhite:0.0f alpha:0.10f]
                                     : [UIColor colorWithWhite:1.0f alpha:0.10f]).CGColor;
    CAGradientLayer *fill = SenkoPlateLayer(plate, kSenkoPlateFill, nil);
    fill.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)bottom.CGColor, nil];
    CAGradientLayer *sheen = SenkoPlateLayer(plate, kSenkoPlateSheen, fill);
    sheen.hidden = flat;
    sheen.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1.0f alpha:light ? 0.34f : 0.14f].CGColor,
                    (id)[UIColor colorWithWhite:1.0f alpha:0.0f].CGColor, nil];
    plate.layer.shadowColor = [UIColor blackColor].CGColor;
    plate.layer.shadowOffset = CGSizeMake(0.0f, flat ? 0.0f : 2.0f);
    plate.layer.shadowRadius = 3.0f;
    plate.layer.shadowOpacity = flat ? 0.0f : (light ? 0.18f : 0.42f);
    SenkoLayoutHomePlate(plate);
}

/* uibutton puts its image view back at the bottom of the stack when the image
   changes, which buried the glyph under the fill after a theme switch. the
   relief is pushed below everything else on every pass */
static void SenkoKeepReliefBelow(UIView *plate) {
    CALayer *fill = nil;
    CALayer *sheen = nil;
    for (CALayer *layer in plate.layer.sublayers) {
        if ([layer.name isEqualToString:kSenkoPlateFill]) fill = layer;
        else if ([layer.name isEqualToString:kSenkoPlateSheen]) sheen = layer;
    }
    NSArray *order = plate.layer.sublayers;
    if (fill && (![order count] || [order objectAtIndex:0] != fill))
        [plate.layer insertSublayer:fill atIndex:0];
    if (sheen && ([order count] < 2 || [plate.layer.sublayers objectAtIndex:1] != sheen))
        [plate.layer insertSublayer:sheen atIndex:1];
}

void SenkoLayoutHomePlate(UIView *plate) {
    CGRect b = plate.bounds;
    if (b.size.width < 2.0f || b.size.height < 2.0f) return;
    CGFloat radius = plate.layer.cornerRadius;
    SenkoBeginSilentLayers();
    SenkoKeepReliefBelow(plate);
    for (CALayer *layer in plate.layer.sublayers) {
        if ([layer.name isEqualToString:kSenkoPlateFill]) {
            layer.frame = b;
            layer.cornerRadius = radius;
        } else if ([layer.name isEqualToString:kSenkoPlateSheen]) {
            layer.frame = CGRectMake(1.0f, 1.0f, b.size.width - 2.0f, floorf(b.size.height * 0.5f));
            layer.cornerRadius = radius > 1.0f ? radius - 1.0f : 0.0f;
        }
    }
/* a shadow without a path renders offscreen on every frame the screen moves */
    plate.layer.shadowPath = plate.layer.shadowOpacity > 0.0f
        ? [UIBezierPath bezierPathWithRoundedRect:b cornerRadius:radius].CGPath : NULL;
    SenkoEndSilentLayers();
}

@implementation SenkoReliefControl
- (void)layoutSubviews {
    [super layoutSubviews];
    if (CGSizeEqualToSize(_reliefSize, self.bounds.size)) {
        SenkoKeepReliefBelow(self);
        return;
    }
    _reliefSize = self.bounds.size;
    SenkoLayoutHomePlate(self);
}
@end

@implementation SenkoReliefButton
- (void)layoutSubviews {
    [super layoutSubviews];
    if (CGSizeEqualToSize(_reliefSize, self.bounds.size)) {
        SenkoKeepReliefBelow(self);
        return;
    }
    _reliefSize = self.bounds.size;
    SenkoLayoutHomePlate(self);
}

- (void)setImage:(UIImage *)image forState:(UIControlState)state {
    [super setImage:image forState:state];
    SenkoKeepReliefBelow(self);
}
@end

static void SenkoHomePlainLabel(UILabel *label, UIColor *ink) {
    label.backgroundColor = [UIColor clearColor];
    label.textColor = ink;
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
}

static UILabel *SenkoHomeLabel(UIView *host, NSTextAlignment alignment) {
    UILabel *label = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    label.backgroundColor = [UIColor clearColor];
    label.textAlignment = alignment;
    label.userInteractionEnabled = NO;
    [host addSubview:label];
    return label;
}

CGFloat SenkoHomePlateRadius(void) {
    CGFloat r = SenkoThemeCardRadius();
    return r < 14.0f ? 14.0f : r;
}

@class SenkoHomeView;
static void SenkoHomeLayoutCard(SenkoHomeView *v, BOOL isFlag);

@implementation SenkoHomeView

- (void)pressDown:(UIView *)view { SenkoPressPop(view, YES); }
- (void)pressUp:(UIView *)view { SenkoPressPop(view, NO); }

- (void)wirePress:(UIControl *)control {
    [control addTarget:self action:@selector(pressDown:)
      forControlEvents:UIControlEventTouchDown];
    [control addTarget:self action:@selector(pressUp:)
      forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                       UIControlEventTouchCancel];
}

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.backgroundColor = [UIColor clearColor];
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                UIViewAutoresizingFlexibleHeight;

        gear = [SenkoReliefButton buttonWithType:UIButtonTypeCustom];
        [self wirePress:gear];
        [self addSubview:gear];
        stats = [SenkoReliefButton buttonWithType:UIButtonTypeCustom];
        [self wirePress:stats];
        [self addSubview:stats];

        wordmark = SenkoHomeLabel(self, NSTextAlignmentCenter);
        wordmark.text = @"senko";
        tagline = SenkoHomeLabel(self, NSTextAlignmentCenter);
        tagline.text = @"by sqmrak :3";

        power = [[[SenkoOrbitButton alloc] initWithFrame:CGRectZero] autorelease];
        [self addSubview:power];

        state = SenkoHomeLabel(self, NSTextAlignmentCenter);
        detail = SenkoHomeLabel(self, NSTextAlignmentCenter);
        detail.numberOfLines = 2;
        detail.lineBreakMode = NSLineBreakByTruncatingTail;

        serverCard = [[[SenkoReliefControl alloc] initWithFrame:CGRectZero] autorelease];
        [self wirePress:serverCard];
        [self addSubview:serverCard];
        serverIcon = [[[UIImageView alloc] initWithFrame:CGRectZero] autorelease];
        serverIcon.clipsToBounds = YES;
        [serverCard addSubview:serverIcon];
        serverTitle = SenkoHomeLabel(serverCard, NSTextAlignmentLeft);
        serverSubtitle = SenkoHomeLabel(serverCard, NSTextAlignmentLeft);
        serverChevron = [[[UIImageView alloc] initWithFrame:CGRectZero] autorelease];
        serverChevron.contentMode = UIViewContentModeCenter;
        [serverCard addSubview:serverChevron];
        protocolDot = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
        [self addSubview:protocolDot];
        protocolLine = SenkoHomeLabel(self, NSTextAlignmentLeft);
        protocolLine.lineBreakMode = NSLineBreakByTruncatingTail;

        [self relocalize];
        [self applyTheme];
    }
    return self;
}

- (void)relocalize {
    gear.accessibilityLabel = SenkoLocalizedText(@"Settings");
    stats.accessibilityLabel = SenkoLocalizedText(@"Statistics");
}

- (void)applyTheme {
    CGFloat radius = SenkoHomePlateRadius();
    SenkoStyleHomePlate(gear, 12.0f, nil);
    SenkoStyleHomePlate(stats, 12.0f, nil);
    SenkoStyleHomePlate(serverCard, radius, nil);
    [gear setImage:TintedIconNamed(@"glyph-gear.png", 22.0f, kInk)
          forState:UIControlStateNormal];
    [stats setImage:TintedIconNamed(@"glyph-chart.png", 22.0f, kInk)
           forState:UIControlStateNormal];

    SenkoHomePlainLabel(wordmark, kAccentBlue);
    SenkoHomePlainLabel(tagline, kInkMuted);
    SenkoHomePlainLabel(state, kInk);
    SenkoHomePlainLabel(serverTitle, kInk);
    SenkoHomePlainLabel(serverSubtitle, kInkMuted);
    SenkoHomePlainLabel(protocolLine, kInkMuted);
    protocolDot.backgroundColor = kAccentBlue;
    serverChevron.image = SenkoIconChevron(14.0f, kInkMuted);

    SenkoHomeLayoutCard(self, _serverIconIsFlag);
    [power applyTheme];
    [self setNeedsLayout];
}

- (void)setServerTitle:(NSString *)title
              subtitle:(NSString *)subtitle
                  icon:(UIImage *)icon
                isFlag:(BOOL)isFlag {
    _serverIconIsFlag = isFlag;
    serverTitle.text = title;
    serverSubtitle.text = subtitle;
    serverIcon.image = icon;
    serverIcon.contentMode = isFlag ? UIViewContentModeScaleToFill
                                    : UIViewContentModeCenter;
    SenkoHomeLayoutCard(self, isFlag);
}

- (void)setProtocolLine:(NSString *)text {
    protocolLine.text = text;
    protocolLine.hidden = ![text length];
    protocolDot.hidden = protocolLine.hidden;
}

static void SenkoHomeLayoutCard(SenkoHomeView *v, BOOL isFlag) {
    CGRect b = v->serverCard.bounds;
    CGFloat h = b.size.height;
    CGFloat icon = floorf(h - 24.0f);
    if (icon > 44.0f) icon = 44.0f;
    if (icon < 28.0f) icon = 28.0f;
    CGFloat x = 14.0f;
    v->serverIcon.frame = CGRectMake(x, floorf((h - icon) * 0.5f), icon, icon);
    if (isFlag) {
        SenkoStyleBadgeShadow(v->serverIcon, 8.0f);
    } else {
        v->serverIcon.layer.shadowOpacity = 0.0f;
        v->serverIcon.layer.shadowPath = NULL;
        v->serverIcon.clipsToBounds = YES;
        v->serverIcon.layer.cornerRadius = icon * 0.5f;
        v->serverIcon.backgroundColor = [kAccentBlue colorWithAlphaComponent:0.14f];
    }
    CGFloat chevronW = 14.0f;
    v->serverChevron.frame = CGRectMake(b.size.width - 14.0f - chevronW,
                                        floorf((h - 20.0f) * 0.5f), chevronW, 20.0f);
    CGFloat tx = x + icon + 12.0f;
    CGFloat tw = CGRectGetMinX(v->serverChevron.frame) - 8.0f - tx;
    CGFloat titleSize = h >= 66.0f ? 17.0f : 15.0f;
    v->serverTitle.font = SenkoFontBody(titleSize, YES);
    v->serverSubtitle.font = SenkoFontBody(titleSize - 3.0f, NO);
    CGFloat titleH = ceilf(titleSize * 1.3f);
    CGFloat subH = ceilf((titleSize - 3.0f) * 1.35f);
    CGFloat top = floorf((h - titleH - subH) * 0.5f);
    v->serverTitle.frame = CGRectMake(tx, top, tw, titleH);
    v->serverSubtitle.frame = CGRectMake(tx, top + titleH, tw, subH);
}

/* a phone on its side has no height for a stacked screen, so the button takes
   the left half and the status and server column the right */
- (void)layoutWideFrom:(CGFloat)x0 to:(CGFloat)x1
                   top:(CGFloat)top bottom:(CGFloat)bottom {
    CGFloat H = self.bounds.size.height;
    tagline.hidden = YES;
    CGFloat leftW = floorf((x1 - x0) * 0.44f);
    CGFloat availH = H - top - bottom;
    CGFloat side = MIN(availH, leftW);
    if (side < 80.0f) side = 80.0f;
    power.frame = CGRectMake(x0 + floorf((leftW - side) * 0.5f),
                             top + floorf((availH - side) * 0.5f), side, side);

    CGFloat rx = x0 + leftW + 14.0f;
    CGFloat rw = x1 - rx;
    CGFloat stateH = 24.0f, detailH = 18.0f, cardH = 56.0f, gap = 10.0f;
    CGFloat protocolH = 22.0f;
    CGFloat blockH = stateH + detailH + gap + cardH + gap + protocolH;
    CGFloat y = top + floorf((availH - blockH) * 0.5f);
    if (y < top) y = top;
    state.font = SenkoFontBody(19.0f, YES);
    state.frame = CGRectMake(rx, y, rw, stateH);
    y += stateH;
    detail.numberOfLines = 1;
    detail.frame = CGRectMake(rx, y, rw, detailH);
    y += detailH + gap;
    serverCard.frame = CGRectMake(rx, y, rw, cardH);
    y += cardH + gap;
    protocolDot.frame = CGRectMake(rx + 8.0f, y + 8.0f, 6.0f, 6.0f);
    protocolDot.layer.cornerRadius = 3.0f;
    protocolLine.font = SenkoFontBody(13.0f, NO);
    protocolLine.frame = CGRectMake(rx + 22.0f, y, rw - 22.0f, protocolH);
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    CGFloat W = b.size.width;
    CGFloat H = b.size.height;
    if (W < 1.0f || H < 1.0f) return;

    BOOL pad = SenkoHomeIsPad();
    UIEdgeInsets safe = SenkoSafeAreaInsets(self);
    CGFloat top = MAX(safe.top, GetTopOffset());
    CGFloat bottom = safe.bottom + (pad ? 28.0f : 14.0f);
    CGFloat margin = pad ? 28.0f : (W <= 320.0f ? 14.0f : 18.0f);
    CGFloat x0 = safe.left + margin;
    CGFloat x1 = W - safe.right - margin;
    BOOL wide = W > H && H < 560.0f;
    BOOL compact = !pad && !wide && H <= 500.0f;

    CGFloat btn = pad ? 46.0f : 40.0f;
    CGFloat headerY = top + (pad ? 14.0f : 8.0f);
    gear.frame = CGRectMake(x0, headerY, btn, btn);
    stats.frame = CGRectMake(x1 - btn, headerY, btn, btn);

    CGFloat wordSize = pad ? 58.0f : ((compact || wide) ? 32.0f : 44.0f);
    wordmark.font = SenkoFontDisplay(wordSize);
    CGFloat wordH = ceilf(wordSize * 1.2f);
    CGFloat wordX = x0 + btn + 6.0f;
    CGFloat wordW = x1 - x0 - (btn + 6.0f) * 2.0f;
    BOOL wordInHeader = compact || wide;
    CGFloat wordY = wordInHeader ? headerY + floorf((btn - wordH) * 0.5f)
                                 : headerY + btn - 6.0f;
    wordmark.frame = CGRectMake(wordX, wordY, wordW, wordH);

    SenkoHomeLayoutCard(self, _serverIconIsFlag);
    if (wide) {
        [self layoutWideFrom:x0 to:x1 top:headerY + btn + 6.0f bottom:bottom];
        SenkoHomeLayoutCard(self, _serverIconIsFlag);
        return;
    }
    tagline.hidden = NO;
    CGFloat tagSize = pad ? 17.0f : (compact ? 12.0f : 14.0f);
    tagline.font = SenkoFontBody(tagSize, NO);
    CGFloat tagH = ceilf(tagSize * 1.4f);
    CGFloat tagY = wordInHeader ? headerY + btn - 2.0f : wordY + wordH - 4.0f;
    tagline.frame = CGRectMake(wordX, tagY, wordW, tagH);
    CGFloat orbTop = tagY + tagH + (compact ? 4.0f : 12.0f);

    CGFloat colW = x1 - x0;
    if (colW > (pad ? 480.0f : 440.0f)) colW = pad ? 480.0f : 440.0f;
    CGFloat colX = floorf((W - colW) * 0.5f);
    CGFloat cardH = pad ? 78.0f : (compact ? 58.0f : 68.0f);
    CGFloat protocolH = compact ? 20.0f : 24.0f;
    CGFloat y = H - bottom - protocolH;
    protocolDot.frame = CGRectMake(colX + 8.0f, y + floorf((protocolH - 6.0f) * 0.5f),
                                   6.0f, 6.0f);
    protocolDot.layer.cornerRadius = 3.0f;
    protocolLine.font = SenkoFontBody(pad ? 15.0f : 13.0f, NO);
    protocolLine.frame = CGRectMake(colX + 22.0f, y, colW - 22.0f, protocolH);
    y -= (compact ? 6.0f : 10.0f) + cardH;
    serverCard.frame = CGRectMake(colX, y, colW, cardH);

    CGFloat detailSize = pad ? 16.0f : 14.0f;
    CGFloat detailH = compact ? ceilf(detailSize * 1.3f) : ceilf(detailSize * 2.5f);
    detail.numberOfLines = compact ? 1 : 2;
    y -= (compact ? 8.0f : 14.0f) + detailH;
    detail.frame = CGRectMake(colX, y, colW, detailH);
    CGFloat stateSize = pad ? 26.0f : (compact ? 19.0f : 22.0f);
    state.font = SenkoFontBody(stateSize, YES);
    CGFloat stateH = ceilf(stateSize * 1.3f);
    y -= stateH;
    state.frame = CGRectMake(colX, y, colW, stateH);

    CGFloat avail = y - (compact ? 4.0f : 10.0f) - orbTop;
    CGFloat side = MIN(avail, colW * (pad ? 0.7f : 0.82f));
    CGFloat maxSide = pad ? 380.0f : 320.0f;
    if (side > maxSide) side = maxSide;
    if (side < 90.0f) side = 90.0f;
    power.frame = CGRectMake(floorf((W - side) * 0.5f),
                             orbTop + floorf((avail - side) * 0.5f), side, side);

    SenkoHomeLayoutCard(self, _serverIconIsFlag);
}

@end
