#import "boykisser_field.h"

#import <QuartzCore/QuartzCore.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { kBoyMaxPhone = 10 };
enum { kBoyMaxPad   = 16 };
enum { kSnowPhone   = 32 };
enum { kSnowPad     = 48 };
enum { kBoyMax      = 48 };

typedef struct {
    CGFloat x, y;
    CGFloat vx, vy;
    CGFloat rot, vrot;
    CGFloat scale;
    CGFloat alpha;
    CGFloat w, h;
    CGFloat phase, vphase, sway;
    CALayer *layer;
} boy_flake_t;

@implementation SenkoBoykisserField {
    boy_flake_t _flakes[kBoyMax];
    int _count;
    CADisplayLink *_link;
    BOOL _running;
    BOOL _paused;
    BOOL _isPad;
    BOOL _snow;
    UIImage *_sprite;
    CGSize _laidSize;
    CFTimeInterval _lastTs;
    CGFloat _baseH;
}

static BOOL BoyIsPad(void) {
    return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad;
}

/* one scaled copy per sprite and idiom, so a flake never decodes the png */
static UIImage *BoySprite(NSString *name, BOOL pad) {
    static NSMutableDictionary *cache;
    if (![name length]) return nil;
    NSString *key = [name stringByAppendingString:pad ? @"~pad" : @"~phone"];
    UIImage *hit = [cache objectForKey:key];
    if (hit) return hit;

    NSString *path = [[NSBundle mainBundle] pathForResource:name ofType:@"png"];
    UIImage *src = path ? [UIImage imageWithContentsOfFile:path] : nil;
    if (!src || src.size.width < 1.0f || src.size.height < 1.0f) return nil;

/* larger source images prevent blur when iPad scales the particles */
    CGFloat maxSide = pad ? 180.0f : 112.0f;
    CGFloat scale = maxSide / MAX(src.size.width, src.size.height);
    CGSize outSz = CGSizeMake(src.size.width * scale + 10, src.size.height * scale + 10);
    UIGraphicsBeginImageContextWithOptions(outSz, NO, 1.0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGContextSetShadowWithColor(ctx, CGSizeMake(0, 1.5f), pad ? 3.0f : 2.0f,
        [UIColor colorWithRed:0.55 green:0.12 blue:0.35 alpha:0.35].CGColor);
    [src drawInRect:CGRectMake(5, 5, src.size.width * scale, src.size.height * scale)];
    UIImage *out = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    if (!out) return nil;
    if (!cache) cache = [[NSMutableDictionary alloc] init];
    [cache setObject:out forKey:key];
    return out;
}

/* a soft white dot with a faint blue edge, so a flake still shows on a light theme */
static UIImage *SnowSprite(void) {
    static UIImage *sprite;
    if (sprite) return sprite;
    const CGFloat side = 32.0f;
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(side, side), NO, 1.0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGFloat comps[8] = { 1, 1, 1, 1,  1, 1, 1, 0 };
    CGFloat locs[2] = { 0.35f, 1.0f };
    CGGradientRef grad = CGGradientCreateWithColorComponents(space, comps, locs, 2);
    CGContextSetShadowWithColor(ctx, CGSizeZero, 2.0f,
        [UIColor colorWithRed:0.45f green:0.58f blue:0.78f alpha:0.55f].CGColor);
    CGPoint c = CGPointMake(side / 2, side / 2);
    CGContextDrawRadialGradient(ctx, grad, c, 0, c, side / 2 - 3.0f, 0);
    CGGradientRelease(grad);
    CGColorSpaceRelease(space);
    UIImage *out = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    sprite = [out retain];
    return sprite;
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
    if (_snow) {
        f->scale = boy_randf(0.45f, 1.5f);
        f->alpha = boy_randf(0.55f, 1.0f);
/* small flakes fall slower than big ones, which reads as depth */
        f->vy = boy_randf(26.0f, 40.0f) + 22.0f * f->scale;
        f->vx = boy_randf(-5.0f, 5.0f);
        f->sway = boy_randf(6.0f, 18.0f);
        f->phase = boy_randf(0.0f, 6.2832f);
        f->vphase = boy_randf(0.6f, 1.6f);
    } else if (_isPad) {
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
    f->rot = _snow ? 0.0f : boy_randf(-0.18f, 0.18f);
    f->vrot = _snow ? 0.0f : boy_randf(-0.28f, 0.28f);
    f->h = _baseH * f->scale;
    f->w = f->h;
    if (_sprite && _sprite.size.height > 1)
        f->w = f->h * (_sprite.size.width / _sprite.size.height);
    [self applyFlake:f];
}

- (id)initWithFrame:(CGRect)frame {
    return [self initWithFrame:frame spriteName:@"boykisser"];
}

- (id)initSnowWithFrame:(CGRect)frame {
    return [self initWithFrame:frame spriteName:nil snow:YES];
}

- (id)initWithFrame:(CGRect)frame spriteName:(NSString *)name {
    return [self initWithFrame:frame spriteName:name snow:NO];
}

- (id)initWithFrame:(CGRect)frame spriteName:(NSString *)name snow:(BOOL)snow {
    if ((self = [super initWithFrame:frame])) {
        _snow = snow;
        self.userInteractionEnabled = NO;
        self.backgroundColor = [UIColor clearColor];
        self.clipsToBounds = YES;
        self.opaque = NO;
        self.layer.shouldRasterize = NO;
        _isPad = BoyIsPad();
        _count = snow ? (_isPad ? kSnowPad : kSnowPhone) : (_isPad ? kBoyMaxPad : kBoyMaxPhone);
        _baseH = snow ? (_isPad ? 16.0f : 12.0f) : (_isPad ? 110.0f : 70.0f); /* display pt at scale 1 */
        _paused = NO;
        _lastTs = 0;
        _sprite = [(snow ? SnowSprite() : BoySprite(name, _isPad)) retain];
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
        if (_snow) {
            f->phase += f->vphase * dt;
            f->x += sinf(f->phase) * f->sway * dt;
        }
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

- (BOOL)isSnow {
    return _snow;
}

- (BOOL)isRunning {
    return _running;
}

@end
