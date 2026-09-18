#import "ui_theme.h"
#include <math.h>
#include <objc/runtime.h>
#include <objc/message.h>
#import "ui_chrome_priv.h"

/* every glyph is drawn on a 24pt grid and scaled to the requested side, so one
   stroke weight and one optical inset keep the whole set consistent */
#define kIconGrid 24.0f

static NSMutableDictionary *gIconCache;

void SenkoThemeFlushImageCaches(void) {
    [gIconCache removeAllObjects];
}

/* the tint has to be part of the key: two themes hand out different colors at
   the same address once the old palette object is released */
static NSString *IconKey(NSString *name, CGFloat side, UIColor *tint) {
    CGFloat r = 0, g = 0, b = 0, a = 1;
    if (![tint respondsToSelector:@selector(getRed:green:blue:alpha:)] ||
        ![tint getRed:&r green:&g blue:&b alpha:&a]) {
        CGFloat w = 0;
        if ([tint respondsToSelector:@selector(getWhite:alpha:)] &&
            [tint getWhite:&w alpha:&a])
            r = g = b = w;
    }
    return [NSString stringWithFormat:@"%@|%.1f|%.3f,%.3f,%.3f,%.3f",
            name, side, r, g, b, a];
}

static UIImage *CachedIcon(NSString *key) {
    return key ? [gIconCache objectForKey:key] : nil;
}

static UIImage *StoreIcon(NSString *key, UIImage *image) {
    if (!key || !image) return image;
    if (!gIconCache) gIconCache = [[NSMutableDictionary alloc] init];
    /* the set is small and bounded by the sizes the ui asks for, but a runaway
       caller must not be able to grow it without limit */
    if ([gIconCache count] >= 256) [gIconCache removeAllObjects];
    [gIconCache setObject:image forKey:key];
    return image;
}

UIImage *TintedIconNamed(NSString *name, CGFloat side, UIColor *tint) {
    UIImage *src = [UIImage imageNamed:name];
    if (!src || side <= 0) return src;
    NSString *key = IconKey(name, side, tint);
    UIImage *hit = CachedIcon(key);
    if (hit) return hit;

    UIGraphicsBeginImageContextWithOptions(CGSizeMake(side, side), NO, 0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGFloat scale = side / MAX(src.size.width, src.size.height);
    CGFloat w = src.size.width * scale;
    CGFloat h = src.size.height * scale;
    CGRect dst = CGRectMake((side - w) / 2, (side - h) / 2, w, h);
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, 0, side);
    CGContextScaleCTM(ctx, 1, -1);
    CGRect flip = CGRectMake(dst.origin.x, side - dst.origin.y - dst.size.height,
                             dst.size.width, dst.size.height);
    CGContextClipToMask(ctx, flip, src.CGImage);
    CGContextSetFillColorWithColor(ctx, tint.CGColor);
    CGContextFillRect(ctx, flip);
    CGContextRestoreGState(ctx);
    UIImage *out = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    if (!out) return src;
    return StoreIcon(key, out);
}

/* shared setup: unit grid, rounded joints, and a stroke weight that survives
   the 1x armv7 screens where a hairline disappears */
typedef void (^SenkoIconDraw)(CGContextRef ctx, CGFloat u);

static UIImage *DrawIcon(NSString *name, CGFloat side, UIColor *tint,
                         SenkoIconDraw draw) {
    if (side <= 0 || !tint || !draw) return nil;
    NSString *key = IconKey(name, side, tint);
    UIImage *hit = CachedIcon(key);
    if (hit) return hit;

    UIGraphicsBeginImageContextWithOptions(CGSizeMake(side, side), NO, 0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGFloat u = side / kIconGrid; /* one grid unit in points */
    CGContextSetStrokeColorWithColor(ctx, tint.CGColor);
    CGContextSetFillColorWithColor(ctx, tint.CGColor);
    CGContextSetLineCap(ctx, kCGLineCapRound);
    CGContextSetLineJoin(ctx, kCGLineJoinRound);
    CGContextSetLineWidth(ctx, MAX(1.25f, 2.0f * u));
    draw(ctx, u);
    UIImage *out = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return StoreIcon(key, out);
}

UIImage *GaugeIcon(CGFloat side, UIColor *tint) {
    return DrawIcon(@"gauge", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGPoint c = CGPointMake(12.0f * u, 16.0f * u);
        CGFloat radius = 8.0f * u;
/* a half circle and a bare needle at the shared default stroke weight read as
   a smudge next to the solid glyphs beside it (gear, plus, refresh); this one
   glyph needs its own heavier weight to carry the same visual weight */
        CGContextSetLineWidth(ctx, MAX(2.1f, 3.1f * u));
        CGContextAddArc(ctx, c.x, c.y, radius, (CGFloat)M_PI, 0, 0);
        CGContextStrokePath(ctx);
        CGContextSetLineWidth(ctx, MAX(1.8f, 2.6f * u));
        CGFloat needle = (CGFloat)(M_PI * 1.68);
        CGContextMoveToPoint(ctx, c.x, c.y);
        CGContextAddLineToPoint(ctx, c.x + cosf(needle) * radius * 0.78f,
                                     c.y + sinf(needle) * radius * 0.78f);
        CGContextStrokePath(ctx);
        CGContextAddArc(ctx, c.x, c.y, MAX(1.8f, 2.6f * u), 0, (CGFloat)(M_PI * 2.0), 0);
        CGContextFillPath(ctx);
    });
}

UIImage *SenkoGearIcon(CGFloat side, UIColor *tint) {
    return DrawIcon(@"gear", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGPoint c = CGPointMake(12.0f * u, 12.0f * u);
        CGFloat outer = 11.0f * u;
        CGFloat root = 8.2f * u;
        const int points = 32;
        CGContextBeginPath(ctx);
        for (int i = 0; i < points; ++i) {
            CGFloat angle = (CGFloat)(-M_PI_2 + (M_PI * 2.0 * i) / points);
            CGFloat radius = ((i % 4) == 1 || (i % 4) == 2) ? outer : root;
            CGPoint p = CGPointMake(c.x + cosf(angle) * radius,
                                    c.y + sinf(angle) * radius);
            if (i == 0) CGContextMoveToPoint(ctx, p.x, p.y);
            else CGContextAddLineToPoint(ctx, p.x, p.y);
        }
        CGContextClosePath(ctx);
        CGContextFillPath(ctx);
        /* clearing the bore keeps the gear readable at 20pt without a second
           fill in the background color, which no theme can supply here */
        CGContextSetBlendMode(ctx, kCGBlendModeClear);
        CGContextAddArc(ctx, c.x, c.y, 3.5f * u, 0, (CGFloat)(M_PI * 2.0), 0);
        CGContextFillPath(ctx);
    });
}

UIImage *SenkoPlusIcon(CGFloat side, UIColor *tint) {
    return DrawIcon(@"plus", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGContextMoveToPoint(ctx, 12.0f * u, 5.5f * u);
        CGContextAddLineToPoint(ctx, 12.0f * u, 18.5f * u);
        CGContextMoveToPoint(ctx, 5.5f * u, 12.0f * u);
        CGContextAddLineToPoint(ctx, 18.5f * u, 12.0f * u);
        CGContextStrokePath(ctx);
    });
}

UIImage *SenkoIconRefresh(CGFloat side, UIColor *tint) {
    return DrawIcon(@"refresh", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGPoint c = CGPointMake(12.0f * u, 12.0f * u);
        CGFloat radius = 7.2f * u;
        CGFloat weight = MAX(1.25f, 2.0f * u);
        /* arc sweeps clockwise around the circle, leaving a clean top opening */
        CGFloat start = (CGFloat)(-M_PI * 0.16);
        CGFloat end = (CGFloat)(-M_PI * 0.62);
        CGContextAddArc(ctx, c.x, c.y, radius, start, end, 0);
        CGContextStrokePath(ctx);

        /* arrow head sits at the leading end of the clockwise sweep */
        CGFloat hx = c.x + cosf(end) * radius;
        CGFloat hy = c.y + sinf(end) * radius;
        CGFloat tx = -sinf(end);
        CGFloat ty = cosf(end);
        CGFloat nx = cosf(end);
        CGFloat ny = sinf(end);
        CGFloat len = weight * 2.1f;
        CGFloat half = weight * 1.35f;
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, hx + tx * len, hy + ty * len);
        CGContextAddLineToPoint(ctx, hx - nx * half, hy - ny * half);
        CGContextAddLineToPoint(ctx, hx + nx * half, hy + ny * half);
        CGContextClosePath(ctx);
        CGContextFillPath(ctx);
    });
}

UIImage *SenkoIconPower(CGFloat side, UIColor *tint) {
    return DrawIcon(@"power", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGPoint c = CGPointMake(12.0f * u, 13.0f * u);
        CGContextAddArc(ctx, c.x, c.y, 7.2f * u,
                        (CGFloat)(-M_PI * 0.36), (CGFloat)(M_PI * 1.36), 0);
        CGContextStrokePath(ctx);
        CGContextMoveToPoint(ctx, 12.0f * u, 3.6f * u);
        CGContextAddLineToPoint(ctx, 12.0f * u, 11.4f * u);
        CGContextStrokePath(ctx);
    });
}

UIImage *SenkoIconShield(CGFloat side, UIColor *tint) {
    return DrawIcon(@"shield", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGContextMoveToPoint(ctx, 12.0f * u, 3.2f * u);
        CGContextAddLineToPoint(ctx, 19.4f * u, 6.4f * u);
        CGContextAddCurveToPoint(ctx, 19.4f * u, 14.6f * u,
                                      16.4f * u, 19.0f * u,
                                      12.0f * u, 20.8f * u);
        CGContextAddCurveToPoint(ctx, 7.6f * u, 19.0f * u,
                                      4.6f * u, 14.6f * u,
                                      4.6f * u, 6.4f * u);
        CGContextClosePath(ctx);
        CGContextStrokePath(ctx);
    });
}

UIImage *SenkoIconGlobe(CGFloat side, UIColor *tint) {
    return DrawIcon(@"globe", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGPoint c = CGPointMake(12.0f * u, 12.0f * u);
        CGFloat r = 8.4f * u;
        CGContextAddArc(ctx, c.x, c.y, r, 0, (CGFloat)(M_PI * 2.0), 0);
        CGContextStrokePath(ctx);
        CGContextSetLineWidth(ctx, MAX(1.0f, 1.5f * u));
        CGContextMoveToPoint(ctx, c.x - r, c.y);
        CGContextAddLineToPoint(ctx, c.x + r, c.y);
        CGContextStrokePath(ctx);
        /* two meridians drawn as ellipses read as a globe without needing a
           projection the rasteriser cannot hint at small sizes */
        CGContextAddEllipseInRect(ctx, CGRectMake(c.x - r * 0.46f, c.y - r,
                                                  r * 0.92f, r * 2.0f));
        CGContextStrokePath(ctx);
    });
}

UIImage *SenkoIconChevron(CGFloat side, UIColor *tint) {
    return DrawIcon(@"chevron", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGContextSetLineWidth(ctx, MAX(1.2f, 1.9f * u));
        CGContextMoveToPoint(ctx, 9.6f * u, 5.4f * u);
        CGContextAddLineToPoint(ctx, 16.0f * u, 12.0f * u);
        CGContextAddLineToPoint(ctx, 9.6f * u, 18.6f * u);
        CGContextStrokePath(ctx);
    });
}


UIImage *SenkoIconCopy(CGFloat side, UIColor *tint) {
    return DrawIcon(@"copy", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGContextSetLineWidth(ctx, MAX(1.1f, 1.7f * u));
        CGRect back = CGRectMake(4.2f * u, 3.4f * u, 11.0f * u, 13.2f * u);
        CGRect front = CGRectMake(8.8f * u, 7.4f * u, 11.0f * u, 13.2f * u);
        CGContextAddPath(ctx, [UIBezierPath bezierPathWithRoundedRect:back
                                                          cornerRadius:2.4f * u].CGPath);
        CGContextStrokePath(ctx);
        /* clearing under the front sheet keeps the overlap legible on any
           background, which a plain second outline would not */
        CGContextSaveGState(ctx);
        CGContextSetBlendMode(ctx, kCGBlendModeClear);
        CGContextAddPath(ctx, [UIBezierPath bezierPathWithRoundedRect:
                               CGRectInset(front, -1.4f * u, -1.4f * u)
                                                          cornerRadius:3.4f * u].CGPath);
        CGContextFillPath(ctx);
        CGContextRestoreGState(ctx);
        CGContextAddPath(ctx, [UIBezierPath bezierPathWithRoundedRect:front
                                                          cornerRadius:2.4f * u].CGPath);
        CGContextStrokePath(ctx);
    });
}

UIImage *SenkoIconPencil(CGFloat side, UIColor *tint) {
    return DrawIcon(@"pencil", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGContextSetLineWidth(ctx, MAX(1.1f, 1.7f * u));
        CGContextMoveToPoint(ctx, 4.6f * u, 19.4f * u);
        CGContextAddLineToPoint(ctx, 5.6f * u, 15.4f * u);
        CGContextAddLineToPoint(ctx, 15.8f * u, 5.2f * u);
        CGContextAddLineToPoint(ctx, 18.8f * u, 8.2f * u);
        CGContextAddLineToPoint(ctx, 8.6f * u, 18.4f * u);
        CGContextClosePath(ctx);
        CGContextStrokePath(ctx);
        CGContextMoveToPoint(ctx, 13.4f * u, 7.6f * u);
        CGContextAddLineToPoint(ctx, 16.4f * u, 10.6f * u);
        CGContextStrokePath(ctx);
    });
}

UIImage *SenkoIconTrash(CGFloat side, UIColor *tint) {
    return DrawIcon(@"trash", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGContextSetLineWidth(ctx, MAX(1.1f, 1.7f * u));
        CGContextMoveToPoint(ctx, 4.4f * u, 6.6f * u);
        CGContextAddLineToPoint(ctx, 19.6f * u, 6.6f * u);
        CGContextStrokePath(ctx);
        CGContextMoveToPoint(ctx, 9.4f * u, 6.6f * u);
        CGContextAddLineToPoint(ctx, 9.4f * u, 4.2f * u);
        CGContextAddLineToPoint(ctx, 14.6f * u, 4.2f * u);
        CGContextAddLineToPoint(ctx, 14.6f * u, 6.6f * u);
        CGContextStrokePath(ctx);
        CGContextMoveToPoint(ctx, 6.4f * u, 6.6f * u);
        CGContextAddLineToPoint(ctx, 7.4f * u, 19.8f * u);
        CGContextAddLineToPoint(ctx, 16.6f * u, 19.8f * u);
        CGContextAddLineToPoint(ctx, 17.6f * u, 6.6f * u);
        CGContextStrokePath(ctx);
    });
}

UIImage *SenkoIconClose(CGFloat side, UIColor *tint) {
    return DrawIcon(@"close", side, tint, ^(CGContextRef ctx, CGFloat u) {
        CGContextMoveToPoint(ctx, 6.6f * u, 6.6f * u);
        CGContextAddLineToPoint(ctx, 17.4f * u, 17.4f * u);
        CGContextMoveToPoint(ctx, 17.4f * u, 6.6f * u);
        CGContextAddLineToPoint(ctx, 6.6f * u, 17.4f * u);
        CGContextStrokePath(ctx);
    });
}
