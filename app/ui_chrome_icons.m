#import "ui_theme.h"
#include <math.h>
#include <objc/runtime.h>
#include <objc/message.h>
#import "ui_chrome_priv.h"

static NSMutableDictionary *gIconCache;
static UIImage *gGaugeCache;
static CGFloat gGaugeSide;
static void *gGaugeTint;

void SenkoThemeFlushImageCaches(void) {
    [gIconCache removeAllObjects];
    [gGaugeCache release];
    gGaugeCache = nil;
    gGaugeSide = 0;
    gGaugeTint = NULL;
}

UIImage *TintedIconNamed(NSString *name, CGFloat side, UIColor *tint) {
    UIImage *src = [UIImage imageNamed:name];
    if (!src || side <= 0) return src;
    if (!gIconCache) gIconCache = [[NSMutableDictionary alloc] init];
    NSString *key = [NSString stringWithFormat:@"%@|%.1f|%p", name, side, tint];
    UIImage *hit = [gIconCache objectForKey:key];
    if (hit) return hit;

    CGRect r = CGRectMake(0, 0, side, side);
    UIGraphicsBeginImageContextWithOptions(r.size, NO, 0);
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
    [gIconCache setObject:out forKey:key];
    return out;
}

UIImage *GaugeIcon(CGFloat side, UIColor *tint) {
    if (side <= 0) return nil;
    if (gGaugeCache && fabs(gGaugeSide - side) < 0.01f && gGaugeTint == (void *)tint)
        return gGaugeCache;
    CGRect r = CGRectMake(0, 0, side, side);
    UIGraphicsBeginImageContextWithOptions(r.size, NO, 0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGContextSetStrokeColorWithColor(ctx, tint.CGColor);
    CGContextSetFillColorWithColor(ctx, tint.CGColor);
    CGContextSetLineCap(ctx, kCGLineCapRound);
    CGContextSetLineWidth(ctx, MAX(1.7f, side * 0.10f));
    CGPoint c = CGPointMake(side * 0.50f, side * 0.70f);
    CGFloat radius = side * 0.36f;
    CGContextAddArc(ctx, c.x, c.y, radius, (CGFloat)M_PI, (CGFloat)(M_PI * 2.0), 0);
    CGContextStrokePath(ctx);
    CGContextSetLineWidth(ctx, MAX(1.4f, side * 0.075f));
    CGFloat needle = (CGFloat)(M_PI * 1.70);
    CGContextMoveToPoint(ctx, c.x, c.y);
    CGContextAddLineToPoint(ctx, c.x + cosf(needle) * radius * 0.76f,
                                 c.y + sinf(needle) * radius * 0.76f);
    CGContextStrokePath(ctx);
    CGContextAddArc(ctx, c.x, c.y, MAX(1.5f, side * 0.08f), 0, (CGFloat)(M_PI * 2.0), 0);
    CGContextFillPath(ctx);
    UIImage *out = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    [gGaugeCache release];
    gGaugeCache = [out retain];
    gGaugeSide = side;
    gGaugeTint = (void *)tint;
    return out;
}
