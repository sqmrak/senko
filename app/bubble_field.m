#import "bubble_field.h"

#import <QuartzCore/QuartzCore.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { kBubbleMaxPhone = 12 };
enum { kBubbleMaxPad   = 16 };
enum { kBubbleMax      = 16 };

typedef struct {
    CGFloat x, y;
    CGFloat vx, vy;
    CGFloat phase; /* sin drift */
    CGFloat w;
    CGFloat alpha;
    CALayer *body;
    CALayer *shine;
} bubble_t;

@implementation SenkoBubbleField {
    bubble_t _bubbles[kBubbleMax];
    int _count;
    CADisplayLink *_link;
    BOOL _running;
    BOOL _paused;
    BOOL _isPad;
    CGSize _laidSize;
    CFTimeInterval _lastTs;
}

static BOOL BubbleIsPad(void) {
    return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad;
}

static CGFloat bub_randf(CGFloat a, CGFloat b) {
    return a + ((CGFloat)(arc4random() % 10000) / 10000.0f) * (b - a);
}

- (void)applyBubble:(bubble_t *)b {
    if (!b->body) return;
    b->body.bounds = CGRectMake(0, 0, b->w, b->w);
    b->body.cornerRadius = b->w * 0.5f;
    b->body.position = CGPointMake(b->x, b->y);
    b->body.opacity = b->alpha;
    if (b->shine) {
        CGFloat sw = b->w * 0.38f;
        b->shine.bounds = CGRectMake(0, 0, sw, sw * 0.55f);
        b->shine.cornerRadius = sw * 0.35f;
        b->shine.position = CGPointMake(b->w * 0.32f, b->w * 0.28f);
    }
}

- (void)resetBubble:(bubble_t *)b scatter:(BOOL)scatter {
    CGFloat bw = self.bounds.size.width;
    CGFloat bh = self.bounds.size.height;
    if (bw < 1) bw = 320;
    if (bh < 1) bh = 480;
    if (_isPad) {
        b->w = bub_randf(36.0f, 88.0f);
        b->alpha = bub_randf(0.48f, 0.82f);
        b->vy = bub_randf(-38.0f, -18.0f); /* float up */
        b->vx = bub_randf(-8.0f, 8.0f);
    } else {
        b->w = bub_randf(26.0f, 62.0f);
        b->alpha = bub_randf(0.45f, 0.78f);
        b->vy = bub_randf(-32.0f, -14.0f);
        b->vx = bub_randf(-6.0f, 6.0f);
    }
    b->phase = bub_randf(0, (CGFloat)(M_PI * 2.0));
    b->x = bub_randf(b->w, bw - b->w);
    if (scatter)
        b->y = bub_randf(0, bh);
    else
        b->y = bh + b->w + bub_randf(8.0f, 60.0f);
    [self applyBubble:b];
}

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.userInteractionEnabled = NO;
        self.backgroundColor = [UIColor clearColor];
        self.clipsToBounds = YES;
        self.opaque = NO;
        _isPad = BubbleIsPad();
        _count = _isPad ? kBubbleMaxPad : kBubbleMaxPhone;
        _paused = NO;
        _lastTs = 0;
        memset(_bubbles, 0, sizeof _bubbles);
        for (int i = 0; i < _count; ++i) {
            CALayer *body = [CALayer layer];
/* denser cyan glass so bubbles read on sky wallpaper */
            body.backgroundColor =
                [UIColor colorWithRed:0.70 green:0.92 blue:1.00 alpha:0.78].CGColor;
            body.borderWidth = 1.5f;
            body.borderColor =
                [UIColor colorWithWhite:1 alpha:0.88].CGColor;
            body.opaque = NO;
            body.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                            [NSNull null], @"position",
                            [NSNull null], @"bounds",
                            [NSNull null], @"opacity",
                            [NSNull null], @"cornerRadius", nil];
            [self.layer addSublayer:body];

            CALayer *shine = [CALayer layer];
            shine.backgroundColor =
                [UIColor colorWithWhite:1 alpha:0.92].CGColor;
            shine.opaque = NO;
            shine.actions = body.actions;
            [body addSublayer:shine];

            _bubbles[i].body = body;
            _bubbles[i].shine = shine;
            [self resetBubble:&_bubbles[i] scatter:YES];
        }
    }
    return self;
}

- (void)dealloc {
    [self stop];
    for (int i = 0; i < _count; ++i) {
        [_bubbles[i].shine removeFromSuperlayer];
        [_bubbles[i].body removeFromSuperlayer];
    }
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    if (!CGSizeEqualToSize(_laidSize, self.bounds.size) &&
        self.bounds.size.width > 1 && self.bounds.size.height > 1) {
        _laidSize = self.bounds.size;
        for (int i = 0; i < _count; ++i)
            [self resetBubble:&_bubbles[i] scatter:YES];
    }
}

- (void)tick:(CGFloat)dt {
    if (_paused) return;
    if (dt < 1.0f / 90.0f) dt = 1.0f / 90.0f;
    if (dt > 1.0f / 20.0f) dt = 1.0f / 30.0f;
    CGFloat bw = self.bounds.size.width;
    for (int i = 0; i < _count; ++i) {
        bubble_t *b = &_bubbles[i];
        b->phase += dt * 1.1f;
        b->y += b->vy * dt;
        b->x += b->vx * dt + sinf(b->phase) * 10.0f * dt;
        if (b->y < -b->w - 20.0f || b->x < -b->w || b->x > bw + b->w) {
            [self resetBubble:b scatter:NO];
            continue;
        }
        b->body.position = CGPointMake(b->x, b->y);
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
        [self applyBubble:&_bubbles[i]];
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
