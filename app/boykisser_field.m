#import "boykisser_field.h"

#import <QuartzCore/QuartzCore.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* count set in init by idiom */
enum { kBoyMaxPhone = 10 };
enum { kBoyMaxPad   = 16 };
enum { kBoyMax      = 16 }; /* array capacity = pad max */

typedef struct {
    CGFloat x, y;
    CGFloat vx, vy;
    CGFloat rot, vrot;
    CGFloat scale;
    CGFloat alpha;
    CGFloat w, h;
    CALayer *layer;
} boy_flake_t;

@implementation SenkoBoykisserField {
    boy_flake_t _flakes[kBoyMax];
    int _count;
    CADisplayLink *_link;
    BOOL _running;
    BOOL _paused;
    BOOL _isPad;
    UIImage *_sprite;
    CGSize _laidSize;
    CFTimeInterval _lastTs;
    CGFloat _baseH; /* base height before scale */
}

static UIImage *gBoySpritePhone;
static UIImage *gBoySpritePad;

static BOOL BoyIsPad(void) {
    return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad;
}

UIImage *SenkoBoykisserSprite(CGFloat size) {
    BOOL pad = (size >= 140.0f) || BoyIsPad();
    UIImage **slot = pad ? &gBoySpritePad : &gBoySpritePhone;
    if (*slot) return *slot;

    UIImage *src = [UIImage imageNamed:@"boykisser.png"];
    if (!src)
        src = [UIImage imageNamed:@"boykisser"];
    if (!src) {
        NSString *path = [[NSBundle mainBundle] pathForResource:@"boykisser" ofType:@"png"];
        if (path) src = [UIImage imageWithContentsOfFile:path];
    }
    if (!src) return nil;

/* larger bake size so pad upscale is sharp */
    CGFloat maxSide = pad ? 180.0f : 112.0f;
    CGFloat scale = maxSide / MAX(src.size.width, src.size.height);
    CGSize outSz = CGSizeMake(src.size.width * scale + 10, src.size.height * scale + 10);
    UIGraphicsBeginImageContextWithOptions(outSz, NO, 1.0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGContextSetShadowWithColor(ctx, CGSizeMake(0, 1.5f), pad ? 3.0f : 2.0f,
        [UIColor colorWithRed:0.55 green:0.12 blue:0.35 alpha:0.35].CGColor);
    [src drawInRect:CGRectMake(5, 5, src.size.width * scale, src.size.height * scale)];
    *slot = [UIGraphicsGetImageFromCurrentImageContext() retain];
    UIGraphicsEndImageContext();
    return *slot;
}

static CGFloat boy_randf(CGFloat a, CGFloat b) {
    return a + ((CGFloat)(arc4random() % 10000) / 10000.0f) * (b - a);
}

- (void)applyFlake:(boy_flake_t *)f {
    if (!f->layer) return;
    f->layer.bounds = CGRectMake(0, 0, f->w, f->h);
    f->layer.position = CGPointMake(f->x, f->y);
    f->layer.opacity = f->alpha;
    f->layer.affineTransform = CGAffineTransformMakeRotation(f->rot);
}

- (void)resetFlake:(boy_flake_t *)f scatter:(BOOL)scatter {
    CGFloat bw = self.bounds.size.width;
    CGFloat bh = self.bounds.size.height;
    if (bw < 1) bw = 320;
    if (bh < 1) bh = 480;
/* pad: bigger and more opaque so they read on a huge canvas */
    if (_isPad) {
        f->scale = boy_randf(0.85f, 1.45f);
        f->alpha = boy_randf(0.92f, 1.0f);
        f->vy = boy_randf(52.0f, 78.0f);
        f->vx = boy_randf(-10.0f, 10.0f);
    } else {
        f->scale = boy_randf(0.70f, 1.20f);
        f->alpha = boy_randf(0.84f, 1.0f);
        f->vy = boy_randf(44.0f, 64.0f);
        f->vx = boy_randf(-6.0f, 6.0f);
    }
    f->x = boy_randf(24, bw - 24);
    if (scatter)
        f->y = boy_randf(-40, bh);
    else
        f->y = boy_randf(-(_baseH + 40.0f), -36.0f);
    f->rot = boy_randf(-0.18f, 0.18f);
    f->vrot = boy_randf(-0.28f, 0.28f);
    f->h = _baseH * f->scale;
    f->w = f->h;
    if (_sprite && _sprite.size.height > 1)
        f->w = f->h * (_sprite.size.width / _sprite.size.height);
    [self applyFlake:f];
}

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.userInteractionEnabled = NO;
        self.backgroundColor = [UIColor clearColor];
        self.clipsToBounds = YES;
        self.opaque = NO;
        self.layer.shouldRasterize = NO;
        _isPad = BoyIsPad();
        _count = _isPad ? kBoyMaxPad : kBoyMaxPhone;
        _baseH = _isPad ? 110.0f : 70.0f; /* display pt at scale 1 */
        _paused = NO;
        _lastTs = 0;
        _sprite = [SenkoBoykisserSprite(_isPad ? 160.0f : 64.0f) retain];
        memset(_flakes, 0, sizeof _flakes);
        for (int i = 0; i < _count; ++i) {
            CALayer *L = [CALayer layer];
            if (_sprite) {
                L.contents = (id)_sprite.CGImage;
                L.contentsGravity = kCAGravityResizeAspect;
            }
            L.contentsScale = 1.0;
            L.magnificationFilter = kCAFilterLinear;
            L.minificationFilter = kCAFilterLinear;
            L.edgeAntialiasingMask = 0;
            L.opaque = NO;
            L.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                         [NSNull null], @"position",
                         [NSNull null], @"transform",
                         [NSNull null], @"bounds",
                         [NSNull null], @"opacity",
                         [NSNull null], @"contents", nil];
            [self.layer addSublayer:L];
            _flakes[i].layer = L;
            [self resetFlake:&_flakes[i] scatter:YES];
        }
    }
    return self;
}

- (void)dealloc {
    [self stop];
    for (int i = 0; i < _count; ++i)
        [_flakes[i].layer removeFromSuperlayer];
    [_sprite release];
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    if (!CGSizeEqualToSize(_laidSize, self.bounds.size) &&
        self.bounds.size.width > 1 && self.bounds.size.height > 1) {
        _laidSize = self.bounds.size;
        for (int i = 0; i < _count; ++i)
            [self resetFlake:&_flakes[i] scatter:YES];
    }
}

- (void)tick:(CGFloat)dt {
    if (_paused) return;
    if (dt < 1.0f / 90.0f) dt = 1.0f / 90.0f;
    if (dt > 1.0f / 20.0f) dt = 1.0f / 30.0f;
    CGFloat bw = self.bounds.size.width;
    CGFloat bh = self.bounds.size.height;
    for (int i = 0; i < _count; ++i) {
        boy_flake_t *f = &_flakes[i];
        f->y += f->vy * dt;
        f->x += f->vx * dt;
        f->rot += f->vrot * dt;
        if (f->y > bh + f->h || f->x < -f->w || f->x > bw + f->w) {
            [self resetFlake:f scatter:NO];
            continue;
        }
        f->layer.position = CGPointMake(f->x, f->y);
        f->layer.affineTransform = CGAffineTransformMakeRotation(f->rot);
    }
}

- (void)linkFire:(CADisplayLink *)link {
    CFTimeInterval now = link.timestamp;
    CGFloat dt;
    if (_lastTs <= 0)
        dt = 1.0f / 30.0f;
    else
        dt = (CGFloat)(now - _lastTs);
    _lastTs = now;
    [self tick:dt];
}

- (void)start {
    self.hidden = NO;
    _paused = NO;
    if (_running) return;
    _running = YES;
    _lastTs = 0;
    _link = [[CADisplayLink displayLinkWithTarget:self selector:@selector(linkFire:)] retain];
    if ([_link respondsToSelector:@selector(setFrameInterval:)])
        _link.frameInterval = 2; /* 30fps */
    [_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    for (int i = 0; i < _count; ++i)
        [self applyFlake:&_flakes[i]];
}

- (void)stop {
    _running = NO;
    _paused = NO;
    _lastTs = 0;
    [_link invalidate];
    [_link release];
    _link = nil;
    self.hidden = YES;
}

- (void)setPaused:(BOOL)paused {
    _paused = paused ? YES : NO;
    if (_paused)
        _lastTs = 0;
    if (_link)
        _link.paused = _paused;
}

- (BOOL)isRunning {
    return _running;
}

@end
