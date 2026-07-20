#import "ui_theme.h"
#include <objc/runtime.h>
#import "ui_chrome_priv.h"

char SenkoStyleSizeKey;
char SenkoStyleTopKey;
char SenkoStyleBotKey;

UIBezierPath *SenkoHeartPath(CGRect r) {
    CGFloat pad = MIN(r.size.width, r.size.height) * 0.06f;
    CGRect b = CGRectInset(r, pad, pad);
    CGFloat w = b.size.width;
    CGFloat h = b.size.height;
    CGFloat x0 = b.origin.x;
    CGFloat y0 = b.origin.y;
    UIBezierPath *p = [UIBezierPath bezierPath];
/* closed heart so the dome mask clips fill without holes */
    [p moveToPoint:CGPointMake(x0 + w * 0.50f, y0 + h * 0.94f)];
    [p addCurveToPoint:CGPointMake(x0 + w * 0.02f, y0 + h * 0.34f)
         controlPoint1:CGPointMake(x0 + w * 0.16f, y0 + h * 0.78f)
         controlPoint2:CGPointMake(x0 + w * 0.00f, y0 + h * 0.58f)];
    [p addCurveToPoint:CGPointMake(x0 + w * 0.50f, y0 + h * 0.26f)
         controlPoint1:CGPointMake(x0 + w * 0.04f, y0 + h * 0.02f)
         controlPoint2:CGPointMake(x0 + w * 0.28f, y0 + h * 0.00f)];
    [p addCurveToPoint:CGPointMake(x0 + w * 0.98f, y0 + h * 0.34f)
         controlPoint1:CGPointMake(x0 + w * 0.72f, y0 + h * 0.00f)
         controlPoint2:CGPointMake(x0 + w * 0.96f, y0 + h * 0.02f)];
    [p addCurveToPoint:CGPointMake(x0 + w * 0.50f, y0 + h * 0.94f)
         controlPoint1:CGPointMake(x0 + w * 1.00f, y0 + h * 0.58f)
         controlPoint2:CGPointMake(x0 + w * 0.84f, y0 + h * 0.78f)];
    [p closePath];
    return p;
}

CAShapeLayer *SenkoHeartMaskLayer(CGRect bounds) {
    CAShapeLayer *m = [CAShapeLayer layer];
    m.frame = bounds;
    m.path = SenkoHeartPath(bounds).CGPath;
    m.fillColor = [UIColor blackColor].CGColor;
    m.contentsScale = [UIScreen mainScreen].scale;
    return m;
}

CAGradientLayer *SenkoNamedGradientLayer(CALayer *parent, NSString *name) {
    for (CALayer *layer in parent.sublayers) {
        if ([layer.name isEqualToString:name] &&
            [layer isKindOfClass:[CAGradientLayer class]])
            return (CAGradientLayer *)layer;
    }
    return nil;
}

BOOL SenkoStyleSizeMatches(UIView *v, CGSize sz) {
    NSValue *prev = objc_getAssociatedObject(v, &SenkoStyleSizeKey);
    return prev && CGSizeEqualToSize([prev CGSizeValue], sz);
}

void SenkoStyleRemember(UIView *v, CGSize sz, UIColor *top, UIColor *bottom) {
    objc_setAssociatedObject(v, &SenkoStyleSizeKey,
                             [NSValue valueWithCGSize:sz],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(v, &SenkoStyleTopKey, top, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(v, &SenkoStyleBotKey, bottom, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

void SenkoApplyShadowPath(CALayer *layer, CGFloat radius) {
/* shadowpath avoids per-frame shadow recompute */
    UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:layer.bounds
                                                    cornerRadius:radius];
    layer.shadowPath = path.CGPath;
}
