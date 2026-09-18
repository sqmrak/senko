#import "ui_theme.h"
#include <objc/runtime.h>
#include <string.h>
#include <math.h>
#import "ui_chrome_priv.h"

char SenkoStyleSizeKey;
char SenkoStyleTopKey;
char SenkoStyleBotKey;
static char kSenkoShadowShapeKey;

typedef struct {
    CGRect bounds;
    CGFloat radius;
} SenkoShadowShape;

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

@implementation SenkoGroupCellBackground

@synthesize roundedCorners = _roundedCorners;
@synthesize showsSeparator = _showsSeparator;

- (void)dealloc {
    [_grooveDark release];
    [_grooveLight release];
    [super dealloc];
}

- (void)setRoundedCorners:(UIRectCorner)corners {
    if (_roundedCorners == corners) return;
    _roundedCorners = corners;
    [self setNeedsLayout];
}

- (void)setShowsSeparator:(BOOL)shows {
    if (_showsSeparator == shows) return;
    _showsSeparator = shows;
    [self setNeedsLayout];
}

/* an incision rather than a line: one dark hairline with a light one under it
   is how a groove is cut, and it suits the themes that carry relief. the flat
   themes get the dark half alone */
- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    CGFloat radius = SenkoThemeCardRadius();
    BOOL light = SenkoThemeIsLight();
    CGFloat scale = [UIScreen mainScreen].scale;
    CGFloat hair = scale > 1.0f ? 1.0f / scale : 1.0f;
    CGFloat inset = 16.0f;

    if (_roundedCorners) {
        CAShapeLayer *mask = [CAShapeLayer layer];
        mask.frame = b;
        mask.path = [UIBezierPath bezierPathWithRoundedRect:b
                                          byRoundingCorners:_roundedCorners
                                                cornerRadii:CGSizeMake(radius, radius)].CGPath;
        self.layer.mask = mask;
    } else {
        self.layer.mask = nil;
    }

    if (!_showsSeparator || b.size.width <= inset * 2.0f) {
        _grooveDark.hidden = YES;
        _grooveLight.hidden = YES;
        return;
    }
    if (!_grooveDark) {
        _grooveDark = [[CALayer layer] retain];
        _grooveDark.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                               [NSNull null], @"bounds",
                               [NSNull null], @"position",
                               [NSNull null], @"backgroundColor",
                               [NSNull null], @"hidden", nil];
        [self.layer addSublayer:_grooveDark];
        _grooveLight = [[CALayer layer] retain];
        _grooveLight.actions = _grooveDark.actions;
        [self.layer addSublayer:_grooveLight];
    }
    _grooveDark.hidden = NO;
    _grooveDark.frame = CGRectMake(inset, b.size.height - hair * 2.0f,
                                   b.size.width - inset * 2.0f, hair);
    _grooveDark.backgroundColor = light
        ? [UIColor colorWithWhite:0 alpha:0.14f].CGColor
        : [UIColor colorWithWhite:0 alpha:0.34f].CGColor;
    _grooveLight.hidden = SenkoThemeIsFlat();
    _grooveLight.frame = CGRectMake(inset, b.size.height - hair,
                                    b.size.width - inset * 2.0f, hair);
    _grooveLight.backgroundColor = light
        ? [UIColor colorWithWhite:1 alpha:0.85f].CGColor
        : [UIColor colorWithWhite:1 alpha:0.10f].CGColor;
}

@end

void SenkoStyleGroupCell(UITableViewCell *cell, NSIndexPath *ip, NSInteger rows) {
    SenkoGroupCellBackground *bg = nil;
    UIRectCorner corners = 0;
    if (!cell) return;
    if ([cell.backgroundView isKindOfClass:[SenkoGroupCellBackground class]]) {
        bg = (SenkoGroupCellBackground *)cell.backgroundView;
    } else {
        bg = [[[SenkoGroupCellBackground alloc] initWithFrame:CGRectZero] autorelease];
        cell.backgroundView = bg;
    }
    if (ip.row == 0) corners |= UIRectCornerTopLeft | UIRectCornerTopRight;
    if (ip.row == rows - 1) corners |= UIRectCornerBottomLeft | UIRectCornerBottomRight;
    bg.roundedCorners = corners;
    bg.showsSeparator = ip.row < rows - 1;
    bg.backgroundColor = kCellHi;
    bg.opaque = !SenkoThemeIsIos26();
    cell.backgroundColor = [UIColor clearColor];
    cell.opaque = !SenkoThemeIsIos26();
    cell.contentView.backgroundColor = [UIColor clearColor];
    /* reused cells keep their old separator colors unless a theme change asks
       the background to lay itself out again */
    [bg setNeedsLayout];
}

void SenkoApplyRelief(UIButton *button, CAGradientLayer *fill,
                      UIColor *top, UIColor *bottom, CGFloat radius) {
    CGRect b;
    BOOL flat;
    BOOL light;
    BOOL ios16;
    BOOL ios26;
    BOOL trulyFlat;
    CAGradientLayer *sheen;
    if (!button || !fill || !top || !bottom) return;
    b = button.bounds;
    if (b.size.width < 2.0f || b.size.height < 2.0f) return;
    flat = SenkoThemeIsFlat();
    light = SenkoThemeIsLight();
    ios16 = SenkoThemeIsIos16();
    ios26 = SenkoThemeIsIos26();
/* ios7 is the one theme with no light source at all. ios16 and ios26 carry
   SenkoThemeCapFlat too, but their own dome and capsule controls already wear
   a thin top highlight and a soft shadow, so a filled pill styled through
   here should not go flatter than the controls sitting next to it */
    trulyFlat = flat && !ios16;

/* a left to right ramp is not a light source. the body runs top to bottom so
   the control reads as lit from above, which is the whole of the effect */
    fill.startPoint = CGPointMake(0.5f, 0.0f);
    fill.endPoint = CGPointMake(0.5f, 1.0f);
    fill.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)bottom.CGColor, nil];

    sheen = SenkoNamedGradientLayer(button.layer, @"reliefSheen");
    if (!sheen) {
        sheen = [CAGradientLayer layer];
        sheen.name = @"reliefSheen";
        sheen.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                         [NSNull null], @"colors",
                         [NSNull null], @"bounds",
                         [NSNull null], @"position",
                         [NSNull null], @"cornerRadius",
                         [NSNull null], @"hidden", nil];
        [button.layer insertSublayer:sheen above:fill];
    }
    sheen.hidden = trulyFlat;
    SenkoSetLayerFrame(sheen, CGRectMake(1.0f, 1.0f, b.size.width - 2.0f,
                                         b.size.height * (ios16 ? 0.40f : 0.5f)));
    sheen.cornerRadius = radius > 1.0f ? radius - 1.0f : 0.0f;
    sheen.startPoint = CGPointMake(0.5f, 0.0f);
    sheen.endPoint = CGPointMake(0.5f, 1.0f);
    sheen.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1 alpha:ios16 ? 0.22f : 0.38f].CGColor,
                    (id)[UIColor colorWithWhite:1 alpha:0.02f].CGColor, nil];

    if (trulyFlat) {
        button.layer.shadowOpacity = 0.0f;
        button.layer.shadowPath = NULL;
        return;
    }
/* the rim belongs to the caller: the check pill wears an accent one and the
   connect pill a dark one, and both would be lost if this set its own */
    button.layer.shadowColor = [UIColor blackColor].CGColor;
    button.layer.masksToBounds = NO;
    if (ios26) {
        button.layer.shadowOffset = CGSizeMake(0.0f, 4.0f);
        button.layer.shadowOpacity = light ? 0.14f : 0.34f;
        button.layer.shadowRadius = 8.0f;
    } else if (ios16) {
        button.layer.shadowOffset = CGSizeMake(0.0f, 2.0f);
        button.layer.shadowOpacity = light ? 0.16f : 0.30f;
        button.layer.shadowRadius = 5.0f;
    } else {
        button.layer.shadowOffset = CGSizeMake(0.0f, 1.0f);
        button.layer.shadowOpacity = light ? 0.22f : 0.34f;
        button.layer.shadowRadius = 2.0f;
    }
    SenkoApplyShadowPath(button.layer, radius);
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
/* shadowpath avoids per-frame shadow recompute. the classic collapse asks for
   it on every scroll tick even when the button has not changed shape. */
    if (!layer) return;
    SenkoShadowShape wanted;
    wanted.bounds = layer.bounds;
    wanted.radius = radius;
    NSValue *old = objc_getAssociatedObject(layer, &kSenkoShadowShapeKey);
    if (old && strcmp([old objCType], @encode(SenkoShadowShape)) == 0) {
        SenkoShadowShape previous;
        [old getValue:&previous];
        if (layer.shadowPath &&
            CGRectEqualToRect(previous.bounds, wanted.bounds) &&
            fabsf((float)(previous.radius - wanted.radius)) < 0.01f)
            return;
    }
    UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:layer.bounds
                                                    cornerRadius:radius];
    layer.shadowPath = path.CGPath;
    objc_setAssociatedObject(layer, &kSenkoShadowShapeKey,
                             [NSValue valueWithBytes:&wanted
                                            objCType:@encode(SenkoShadowShape)],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}
