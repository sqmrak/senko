#import "ui_theme.h"
#include <math.h>
#include <objc/runtime.h>
#include <objc/message.h>
#import "ui_chrome_priv.h"

static BOOL DomeIsActiveColor(UIColor *top) {
    if (!top || !kConnOn) return NO;
    CGFloat r1,g1,b1,a1,r2,g2,b2,a2;
    if (![top getRed:&r1 green:&g1 blue:&b1 alpha:&a1]) return NO;
    if (![kConnOn getRed:&r2 green:&g2 blue:&b2 alpha:&a2]) return NO;
    return (fabsf((float)(r1-r2)) + fabsf((float)(g1-g2)) + fabsf((float)(b1-b2))) < 0.12f;
}

/* find/create named shape on button */
static CAShapeLayer *MisideShape(UIButton *button, NSString *name, CALayer *above) {
    for (CALayer *L in button.layer.sublayers) {
        if ([L.name isEqualToString:name] && [L isKindOfClass:[CAShapeLayer class]])
            return (CAShapeLayer *)L;
    }
    CAShapeLayer *s = [CAShapeLayer layer];
    s.name = name;
    if (above)
        [button.layer insertSublayer:s above:above];
    else
        [button.layer addSublayer:s];
    return s;
}

/* hide miside/ios16-only chrome left on the connect button */
static void StyleDomeHideThemeChrome(UIButton *button) {
    if (!button) return;
    for (CALayer *L in button.layer.sublayers) {
        if ([L.name isEqualToString:@"ios16ring"]) L.hidden = YES;
        if ([L.name isEqualToString:@"ios26ring2"]) L.hidden = YES;
        if ([L.name isEqualToString:@"misideHeartStroke"]) L.hidden = YES;
        if ([L.name isEqualToString:@"misideHeartOuter"]) L.hidden = YES;
        if ([L.name isEqualToString:@"misideShade"]) L.hidden = YES;
        if ([L.name isEqualToString:@"misideSpec"]) L.hidden = YES;
        if ([L.name isEqualToString:@"misideSpecHold"]) L.hidden = YES;
    }
}

/* miside connect: candy 3d heart (logo gloss + rim + depth) */
static void StyleDomeMiside(UIButton *button, UIColor *top, UIColor *bottom) {
    CGFloat d = button.bounds.size.width;
    if (d < 1) return;
    BOOL on = DomeIsActiveColor(top);

    CAGradientLayer *rim = SenkoNamedGradientLayer(button.layer, @"rim");
    CAGradientLayer *body = SenkoNamedGradientLayer(button.layer, @"body");
    CAGradientLayer *shade = SenkoNamedGradientLayer(button.layer, @"misideShade");
    CAGradientLayer *gloss = SenkoNamedGradientLayer(button.layer, @"gloss");
    CAGradientLayer *spec = SenkoNamedGradientLayer(button.layer, @"misideSpec");

    if (!body) {
        body = [CAGradientLayer layer];
        body.name = @"body";
        [button.layer insertSublayer:body atIndex:0];
    }
    if (!shade) {
        shade = [CAGradientLayer layer];
        shade.name = @"misideShade";
        [button.layer insertSublayer:shade above:body];
    }
    if (!gloss) {
        gloss = [CAGradientLayer layer];
        gloss.name = @"gloss";
        [button.layer insertSublayer:gloss above:shade];
    }
    if (!spec) {
        spec = [CAGradientLayer layer];
        spec.name = @"misideSpec";
        [button.layer insertSublayer:spec above:gloss];
    }
    CAShapeLayer *outer = MisideShape(button, @"misideHeartOuter", spec);
    CAShapeLayer *inner = MisideShape(button, @"misideHeartStroke", outer);
    if (rim) rim.hidden = YES;
    for (CALayer *L in button.layer.sublayers) {
        if ([L.name isEqualToString:@"ios16ring"]) L.hidden = YES;
    }

    SenkoBeginSilentLayers();
    button.layer.cornerRadius = 0;
    button.layer.masksToBounds = NO;
    button.layer.borderWidth = 0;
    button.layer.mask = nil;
    button.layer.shouldRasterize = YES;
    button.layer.rasterizationScale = [UIScreen mainScreen].scale;

    CGRect br = button.bounds;
    UIBezierPath *heart = SenkoHeartPath(br);
    CGPathRef hp = heart.CGPath;

/* body: diagonal candy pink (logo-like volume) */
    body.frame = br;
    body.cornerRadius = 0;
    body.hidden = NO;
    body.mask = SenkoHeartMaskLayer(br);
    body.startPoint = CGPointMake(0.15f, 0.05f);
    body.endPoint = CGPointMake(0.85f, 0.95f);
    if (on) {
        body.colors = [NSArray arrayWithObjects:
                       (id)[UIColor colorWithRed:1.00 green:0.78 blue:0.92 alpha:1].CGColor,
                       (id)[UIColor colorWithRed:1.00 green:0.42 blue:0.72 alpha:1].CGColor,
                       (id)[UIColor colorWithRed:0.88 green:0.14 blue:0.52 alpha:1].CGColor, nil];
        body.locations = [NSArray arrayWithObjects:
                          [NSNumber numberWithFloat:0.0f],
                          [NSNumber numberWithFloat:0.42f],
                          [NSNumber numberWithFloat:1.0f], nil];
        button.layer.shadowColor = [UIColor colorWithRed:1.0 green:0.20 blue:0.60 alpha:1].CGColor;
        button.layer.shadowOpacity = 0.70f;
        button.layer.shadowRadius = 16.0f;
        button.layer.shadowOffset = CGSizeMake(0, 5);
    } else {
        body.colors = [NSArray arrayWithObjects:
                       (id)[UIColor colorWithRed:0.72 green:0.48 blue:0.72 alpha:1].CGColor,
                       (id)[UIColor colorWithRed:0.52 green:0.28 blue:0.56 alpha:1].CGColor,
                       (id)[UIColor colorWithRed:0.34 green:0.14 blue:0.38 alpha:1].CGColor, nil];
        body.locations = [NSArray arrayWithObjects:
                          [NSNumber numberWithFloat:0.0f],
                          [NSNumber numberWithFloat:0.48f],
                          [NSNumber numberWithFloat:1.0f], nil];
        button.layer.shadowColor = [UIColor colorWithRed:0.50 green:0.05 blue:0.40 alpha:1].CGColor;
        button.layer.shadowOpacity = 0.45f;
        button.layer.shadowRadius = 12.0f;
        button.layer.shadowOffset = CGSizeMake(0, 4);
    }
    button.layer.shadowPath = hp;

/* bottom depth shade */
    shade.frame = br;
    shade.cornerRadius = 0;
    shade.hidden = NO;
    shade.mask = SenkoHeartMaskLayer(br);
    shade.startPoint = CGPointMake(0.5f, 0.35f);
    shade.endPoint = CGPointMake(0.5f, 1.0f);
    shade.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:0 alpha:0.0f].CGColor,
                    (id)[UIColor colorWithRed:0.35 green:0.00 blue:0.20
                                       alpha:on ? 0.35f : 0.40f].CGColor, nil];
    shade.locations = nil;

/* broad top sheen */
    gloss.frame = br;
    gloss.cornerRadius = 0;
    gloss.hidden = NO;
    gloss.mask = SenkoHeartMaskLayer(br);
    gloss.startPoint = CGPointMake(0.35f, 0.0f);
    gloss.endPoint = CGPointMake(0.65f, 0.58f);
    gloss.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1 alpha:on ? 0.58f : 0.32f].CGColor,
                    (id)[UIColor colorWithWhite:1 alpha:0.0f].CGColor, nil];
    gloss.locations = [NSArray arrayWithObjects:
                       [NSNumber numberWithFloat:0.0f],
                       [NSNumber numberWithFloat:1.0f], nil];

/* tight specular blob (upper-left lobe, logo candy highlight) */
    CGFloat sx = d * 0.18f, sy = d * 0.14f, sw = d * 0.42f, sh = d * 0.30f;
    spec.frame = CGRectMake(sx, sy, sw, sh);
    spec.cornerRadius = sh * 0.5f;
    spec.hidden = NO;
    spec.mask = nil;
    spec.startPoint = CGPointMake(0.5f, 0.0f);
    spec.endPoint = CGPointMake(0.5f, 1.0f);
    spec.colors = [NSArray arrayWithObjects:
                   (id)[UIColor colorWithWhite:1 alpha:on ? 0.72f : 0.40f].CGColor,
                   (id)[UIColor colorWithWhite:1 alpha:0.0f].CGColor, nil];
/* clip specular to heart */
    CAShapeLayer *specClip = [CAShapeLayer layer];
    specClip.frame = br;
    specClip.path = hp;
    specClip.fillColor = [UIColor blackColor].CGColor;
/* mask must be in button coords: host a container */
    {
/* re-parent: put spec inside a heart-masked holder */
        CALayer *holder = nil;
        for (CALayer *L in button.layer.sublayers) {
            if ([L.name isEqualToString:@"misideSpecHold"]) { holder = L; break; }
        }
        if (!holder) {
            holder = [CALayer layer];
            holder.name = @"misideSpecHold";
            [button.layer insertSublayer:holder above:gloss];
        }
        holder.frame = br;
        holder.mask = SenkoHeartMaskLayer(br);
        if (spec.superlayer != holder) {
            [spec removeFromSuperlayer];
            [holder addSublayer:spec];
        }
        spec.frame = CGRectMake(sx, sy, sw, sh);
        (void)specClip;
    }

/* thick dark-pink outer rim (logo bevel) */
    outer.frame = br;
    outer.path = hp;
    outer.fillColor = [UIColor clearColor].CGColor;
    outer.strokeColor = on
        ? [UIColor colorWithRed:0.72 green:0.08 blue:0.42 alpha:0.95].CGColor
        : [UIColor colorWithRed:0.40 green:0.12 blue:0.40 alpha:0.85].CGColor;
    outer.lineWidth = MAX(3.5f, d * 0.045f);
    outer.lineJoin = kCALineJoinRound;
    outer.hidden = NO;

/* inner light rim */
    inner.frame = br;
    inner.path = hp;
    inner.fillColor = [UIColor clearColor].CGColor;
    inner.strokeColor = on
        ? [UIColor colorWithRed:1.0 green:0.88 blue:0.95 alpha:0.85].CGColor
        : [UIColor colorWithRed:1.0 green:0.70 blue:0.88 alpha:0.45].CGColor;
    inner.lineWidth = MAX(1.5f, d * 0.018f);
    inner.lineJoin = kCALineJoinRound;
    inner.hidden = NO;

    button.titleLabel.font = [UIFont boldSystemFontOfSize:18];
    [button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    button.titleLabel.shadowColor = [UIColor colorWithRed:0.50 green:0.00 blue:0.30 alpha:0.55f];
    button.titleLabel.shadowOffset = CGSizeMake(0, 1);
    button.titleEdgeInsets = UIEdgeInsetsMake(d * 0.10f, 0, 0, 0);
    [button bringSubviewToFront:button.titleLabel];
    SenkoEndSilentLayers();
    SenkoStyleRemember(button, button.bounds.size, top, bottom);
    (void)bottom;
}

static void StyleDomeIos16(UIButton *button, UIColor *top, UIColor *bottom) {
    CGFloat d = button.bounds.size.width;
    if (d < 1) return;
    CGFloat cr = d * 0.5f;
    BOOL on = DomeIsActiveColor(top);
    BOOL light = SenkoThemeIsLight();
    BOOL glass = SenkoThemeIsIos26();

    CAGradientLayer *rim = SenkoNamedGradientLayer(button.layer, @"rim");
    CAGradientLayer *body = SenkoNamedGradientLayer(button.layer, @"body");
    CAGradientLayer *gloss = SenkoNamedGradientLayer(button.layer, @"gloss");
    CALayer *ring = nil;
    CALayer *ring2 = nil;
    for (CALayer *L in button.layer.sublayers) {
        if ([L.name isEqualToString:@"ios16ring"]) ring = L;
        if ([L.name isEqualToString:@"ios26ring2"]) ring2 = L;
    }
    if (!ring) {
        ring = [CALayer layer];
        ring.name = @"ios16ring";
        [button.layer addSublayer:ring];
    }
    if (glass && !ring2) {
        ring2 = [CALayer layer];
        ring2.name = @"ios26ring2";
        [button.layer addSublayer:ring2];
    }
    if (!body) {
        body = [CAGradientLayer layer];
        body.name = @"body";
        [button.layer insertSublayer:body atIndex:0];
    }
    if (!gloss) {
        gloss = [CAGradientLayer layer];
        gloss.name = @"gloss";
        [button.layer insertSublayer:gloss above:body];
    }
    if (rim) rim.hidden = YES;
    if (!glass && ring2) ring2.hidden = YES;

    SenkoBeginSilentLayers();
    StyleDomeHideThemeChrome(button);
    body.mask = nil;
    if (gloss) gloss.mask = nil;
    button.layer.cornerRadius = cr;
    button.layer.masksToBounds = NO;
    button.layer.borderWidth = 0;
/* single dome: rasterize after style (glass is wash+layers, not per-frame blur) */
    button.layer.shouldRasterize = YES;
    button.layer.rasterizationScale = [UIScreen mainScreen].scale;
    button.titleEdgeInsets = UIEdgeInsetsZero;

    body.frame = button.bounds;
    body.cornerRadius = cr;
    body.hidden = NO;
    body.locations = nil;
    body.startPoint = CGPointMake(0.5f, 0.0f);
    body.endPoint = CGPointMake(0.5f, 1.0f);

    if (on && glass) {
/* soft green liquid glass - thin wash so live blur reads through */
        body.colors = [NSArray arrayWithObjects:
                       (id)[UIColor colorWithRed:0.45 green:0.95 blue:0.62 alpha:light ? 0.32f : 0.26f].CGColor,
                       (id)[UIColor colorWithRed:0.18 green:0.78 blue:0.42 alpha:light ? 0.14f : 0.12f].CGColor, nil];
        button.layer.shadowColor = [UIColor colorWithRed:0.10 green:0.70 blue:0.35 alpha:1].CGColor;
        button.layer.shadowOpacity = light ? 0.24f : 0.36f;
        button.layer.shadowRadius = 12.0f; /* keep green glow, cut blur cost */
        button.layer.shadowOffset = CGSizeMake(0, 6);
    } else if (on) {
        body.colors = [NSArray arrayWithObjects:
                       (id)[UIColor colorWithRed:0.30 green:0.90 blue:0.50 alpha:0.92].CGColor,
                       (id)[UIColor colorWithRed:0.10 green:0.70 blue:0.35 alpha:0.88].CGColor, nil];
        button.layer.shadowColor = [UIColor colorWithRed:0.15 green:0.80 blue:0.40 alpha:1].CGColor;
        button.layer.shadowOpacity = 0.55f;
        button.layer.shadowRadius = 14.0f;
        button.layer.shadowOffset = CGSizeMake(0, 4);
    } else if (glass) {
/* liquid lens: ultra-clear fill + specular edge. solid vertical fill reads as 2010s/ios6 disc */
        if (light) {
            body.colors = [NSArray arrayWithObjects:
                           (id)[UIColor colorWithWhite:1.0 alpha:0.38].CGColor,
                           (id)[UIColor colorWithWhite:1.0 alpha:0.12].CGColor, nil];
            button.layer.shadowColor = [UIColor colorWithWhite:0 alpha:1].CGColor;
            button.layer.shadowOpacity = 0.14f;
        } else {
            body.colors = [NSArray arrayWithObjects:
                           (id)[UIColor colorWithWhite:1.0 alpha:0.12].CGColor,
                           (id)[UIColor colorWithWhite:1.0 alpha:0.02].CGColor, nil];
            button.layer.shadowColor = [UIColor colorWithWhite:0 alpha:1].CGColor;
            button.layer.shadowOpacity = 0.38f;
        }
        button.layer.shadowRadius = 14.0f;
        button.layer.shadowOffset = CGSizeMake(0, 8);
    } else if (light) {
        body.colors = [NSArray arrayWithObjects:
                       (id)[UIColor colorWithWhite:1.0 alpha:0.78].CGColor,
                       (id)[UIColor colorWithWhite:0.92 alpha:0.62].CGColor, nil];
        button.layer.shadowColor = [UIColor blackColor].CGColor;
        button.layer.shadowOpacity = 0.16f;
        button.layer.shadowRadius = 10.0f;
        button.layer.shadowOffset = CGSizeMake(0, 4);
    } else {
        body.colors = [NSArray arrayWithObjects:
                       (id)[UIColor colorWithWhite:1.0 alpha:0.22].CGColor,
                       (id)[UIColor colorWithWhite:1.0 alpha:0.08].CGColor, nil];
        button.layer.shadowColor = [UIColor blackColor].CGColor;
        button.layer.shadowOpacity = 0.40f;
        button.layer.shadowRadius = 12.0f;
        button.layer.shadowOffset = CGSizeMake(0, 5);
    }
    SenkoApplyShadowPath(button.layer, cr);

    if (glass) {
/* outer soft rim - green edge when on */
        ring.frame = CGRectInset(button.bounds, 0.5f, 0.5f);
        ring.cornerRadius = (d - 1.0f) * 0.5f;
        ring.borderWidth = 1.0f;
        if (on) {
            ring.borderColor = light
                ? [UIColor colorWithRed:0.55 green:0.98 blue:0.70 alpha:0.95].CGColor
                : [UIColor colorWithRed:0.40 green:0.95 blue:0.60 alpha:0.70].CGColor;
        } else {
            ring.borderColor = light
                ? [UIColor colorWithWhite:1 alpha:0.95].CGColor
                : [UIColor colorWithWhite:1 alpha:0.55].CGColor;
        }
        ring.backgroundColor = [UIColor clearColor].CGColor;
        ring.hidden = NO;
/* inner highlight ring */
        if (ring2) {
            ring2.frame = CGRectInset(button.bounds, 4.0f, 4.0f);
            ring2.cornerRadius = (d - 8.0f) * 0.5f;
            ring2.borderWidth = 0.5f;
            if (on) {
                ring2.borderColor = light
                    ? [UIColor colorWithRed:0.70 green:1.0 blue:0.80 alpha:0.55].CGColor
                    : [UIColor colorWithRed:0.50 green:0.95 blue:0.65 alpha:0.35].CGColor;
            } else {
                ring2.borderColor = light
                    ? [UIColor colorWithWhite:1 alpha:0.55].CGColor
                    : [UIColor colorWithWhite:1 alpha:0.25].CGColor;
            }
            ring2.backgroundColor = [UIColor clearColor].CGColor;
            ring2.hidden = NO;
        }
/* top specular only */
        gloss.hidden = NO;
        gloss.frame = CGRectMake(d * 0.18f, d * 0.12f, d * 0.64f, d * 0.28f);
        gloss.cornerRadius = d * 0.28f;
        gloss.startPoint = CGPointMake(0.5f, 0.0f);
        gloss.endPoint = CGPointMake(0.5f, 1.0f);
        gloss.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithWhite:1 alpha:on ? 0.50f : 0.70f].CGColor,
                        (id)[UIColor colorWithWhite:1 alpha:0.0f].CGColor, nil];
    } else {
        CGFloat ringInset = 1.5f;
        ring.frame = CGRectInset(button.bounds, ringInset, ringInset);
        ring.cornerRadius = (d - ringInset * 2.0f) * 0.5f;
        ring.borderWidth = 1.5f;
        ring.borderColor = on
            ? [UIColor colorWithWhite:1 alpha:0.45f].CGColor
            : (light ? [UIColor colorWithWhite:1 alpha:0.70f].CGColor
                     : [UIColor colorWithWhite:1 alpha:0.22f].CGColor);
        ring.backgroundColor = [UIColor clearColor].CGColor;
        ring.hidden = NO;
        gloss.hidden = NO;
        gloss.frame = CGRectMake(d * 0.18f, d * 0.10f, d * 0.64f, d * 0.34f);
        gloss.cornerRadius = d * 0.32f;
        gloss.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithWhite:1 alpha:on ? 0.40f : 0.55f].CGColor,
                        (id)[UIColor colorWithWhite:1 alpha:0.0f].CGColor, nil];
    }

    button.titleLabel.font = glass ? SenkoFontBody(20, YES) : SenkoFontBody(18, YES);
    if (on && glass) {
/* dark-ish ink on light green glass; white on dark */
        [button setTitleColor:(light ? kInk : [UIColor whiteColor])
                     forState:UIControlStateNormal];
        button.titleLabel.shadowColor = nil;
        button.titleLabel.shadowOffset = CGSizeZero;
    } else if (on) {
        [button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
        button.titleLabel.shadowColor = [UIColor colorWithWhite:0 alpha:0.20f];
        button.titleLabel.shadowOffset = CGSizeMake(0, 1);
    } else if (glass && light) {
        [button setTitleColor:kInk forState:UIControlStateNormal];
        button.titleLabel.shadowColor = nil;
        button.titleLabel.shadowOffset = CGSizeZero;
    } else {
        [button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
        button.titleLabel.shadowColor = nil;
        button.titleLabel.shadowOffset = CGSizeZero;
    }
    [button bringSubviewToFront:button.titleLabel];
    SenkoEndSilentLayers();
    if (glass && on) {
/* live blur + thin green wash (frost tag 9107, tint 9108) */
        SenkoInstallFrost(button);
        UIView *frost = [button viewWithTag:9107];
        if (frost) {
            frost.frame = button.bounds;
            frost.layer.cornerRadius = cr;
            frost.clipsToBounds = YES;
            UIColor *greenTint = light
                ? [UIColor colorWithRed:0.50 green:0.95 blue:0.65 alpha:0.28]
                : [UIColor colorWithRed:0.18 green:0.70 blue:0.38 alpha:0.24];
            BOOL tinted = NO;
            for (UIView *sub in frost.subviews) {
                if (sub.tag == 9108) {
                    sub.backgroundColor = greenTint;
                    tinted = YES;
                }
            }
            if (!tinted) {
                UIView *wash = [[[UIView alloc] initWithFrame:frost.bounds] autorelease];
                wash.tag = 9108;
                wash.userInteractionEnabled = NO;
                wash.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                        UIViewAutoresizingFlexibleHeight;
                wash.backgroundColor = greenTint;
                [frost addSubview:wash];
            }
        }
        [button bringSubviewToFront:button.titleLabel];
    } else if (glass) {
/* off: clear lens without muddy toolbar fill */
        SenkoRemoveFrost(button);
    }
    SenkoStyleRemember(button, button.bounds.size, top, bottom);
    (void)bottom;
}

void StyleDomeColors(UIButton *button, UIColor *top, UIColor *bottom) {
    if (!button) return;
    if (SenkoThemeIsMiside()) {
        if (!SenkoNamedGradientLayer(button.layer, @"body")) {
            ApplyGlossyDome(button, top, bottom);
            return;
        }
        StyleDomeMiside(button, top, bottom);
        return;
    }
    if (SenkoThemeIsIos16()) {
        if (!SenkoNamedGradientLayer(button.layer, @"body")) {
            ApplyGlossyDome(button, top, bottom);
            return;
        }
        StyleDomeIos16(button, top, bottom);
        return;
    }
    CAGradientLayer *body = SenkoNamedGradientLayer(button.layer, @"body");
    if (!body) {
        ApplyGlossyDome(button, top, bottom);
        return;
    }
    BOOL flat = SenkoThemeIsFlat();
    CGFloat d = button.bounds.size.width;
    if (d < 1.0f) d = button.frame.size.width;
    CGFloat cr = d * 0.5f;
    if (cr < 1.0f) cr = 1.0f;

    SenkoBeginSilentLayers();
/* leave miside heart: always restore circular disc geometry */
    StyleDomeHideThemeChrome(button);
    body.mask = nil;
    body.frame = button.bounds;
    body.cornerRadius = cr;
    body.locations = nil;
    body.startPoint = CGPointMake(0.5f, 0.0f);
    body.endPoint = CGPointMake(0.5f, 1.0f);
    if (flat)
        body.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)top.CGColor, nil];
    else
        body.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)bottom.CGColor, nil];

    CAGradientLayer *rim = SenkoNamedGradientLayer(button.layer, @"rim");
    CAGradientLayer *gloss = SenkoNamedGradientLayer(button.layer, @"gloss");
    if (gloss) gloss.mask = nil;
    if (rim) {
        rim.hidden = flat;
        rim.frame = CGRectInset(button.bounds, flat ? 0 : -2, flat ? 0 : -2);
        rim.cornerRadius = flat ? cr : (d + 4.0f) * 0.5f;
    }
    if (gloss) {
        gloss.hidden = flat;
        gloss.frame = CGRectMake(d * 0.12f, d * 0.08f, d * 0.76f, d * 0.42f);
        gloss.cornerRadius = d * 0.38f;
        gloss.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithWhite:1 alpha:0.72].CGColor,
                        (id)[UIColor colorWithWhite:1 alpha:0.08].CGColor, nil];
        gloss.startPoint = CGPointMake(0.5f, 0.0f);
        gloss.endPoint = CGPointMake(0.5f, 1.0f);
        gloss.locations = nil;
    }

    button.layer.cornerRadius = cr;
    button.layer.masksToBounds = NO;
    button.titleEdgeInsets = UIEdgeInsetsZero;
    if (flat) {
        button.layer.borderWidth = 0;
        button.layer.shadowOpacity = 0.10f;
        button.layer.shadowRadius = 3.0f;
        button.layer.shadowOffset = CGSizeMake(0, 1);
        button.layer.shadowColor = [UIColor blackColor].CGColor;
        button.titleLabel.font = SenkoFontTitle(20);
    } else {
        button.layer.borderWidth = 0;
        button.layer.borderColor = [UIColor clearColor].CGColor;
        button.layer.shadowOpacity = 0.45;
        button.layer.shadowRadius = 5;
        button.layer.shadowOffset = CGSizeMake(0, 4);
        button.layer.shadowColor = [UIColor blackColor].CGColor;
        button.titleLabel.font = [UIFont boldSystemFontOfSize:20];
    }
    SenkoApplyShadowPath(button.layer, cr);
    SenkoStyleChromeTitle(button);
    SenkoEndSilentLayers();
    SenkoStyleRemember(button, button.bounds.size, top, bottom);
}

CAGradientLayer *ApplyGlossyDome(UIButton *button, UIColor *top, UIColor *bottom) {
    if (!button) return nil;
    CGFloat d = button.bounds.size.width;
    if (d < 1) return nil;

    if (SenkoThemeIsMiside()) {
        if (!SenkoNamedGradientLayer(button.layer, @"body")) {
            CAGradientLayer *body = [CAGradientLayer layer];
            body.name = @"body";
            [button.layer insertSublayer:body atIndex:0];
            CAGradientLayer *gloss = [CAGradientLayer layer];
            gloss.name = @"gloss";
            [button.layer insertSublayer:gloss above:body];
        }
        StyleDomeMiside(button, top, bottom);
        return SenkoNamedGradientLayer(button.layer, @"body");
    }

    if (SenkoThemeIsIos16()) {
        if (!SenkoNamedGradientLayer(button.layer, @"body")) {
            CAGradientLayer *body = [CAGradientLayer layer];
            body.name = @"body";
            [button.layer insertSublayer:body atIndex:0];
            CAGradientLayer *gloss = [CAGradientLayer layer];
            gloss.name = @"gloss";
            [button.layer insertSublayer:gloss above:body];
        }
        StyleDomeIos16(button, top, bottom);
        return SenkoNamedGradientLayer(button.layer, @"body");
    }

    BOOL flat = SenkoThemeIsFlat();

    if (SenkoNamedGradientLayer(button.layer, @"body") && SenkoStyleSizeMatches(button, button.bounds.size)) {
        StyleDomeColors(button, top, bottom);
        CAGradientLayer *body = SenkoNamedGradientLayer(button.layer, @"body");
        CGFloat cr = d / 2;
        SenkoBeginSilentLayers();
        body.frame = button.bounds;
        body.cornerRadius = cr;
        CAGradientLayer *rim = SenkoNamedGradientLayer(button.layer, @"rim");
        if (rim) {
            rim.frame = CGRectInset(button.bounds, (flat) ? 0 : -2,
                                    (flat) ? 0 : -2);
            rim.cornerRadius = (flat) ? cr : (d + 4) / 2;
            rim.hidden = flat;
        }
        CAGradientLayer *gloss = SenkoNamedGradientLayer(button.layer, @"gloss");
        if (gloss) {
            gloss.frame = CGRectMake(d * 0.12, d * 0.08, d * 0.76, d * 0.42);
            gloss.cornerRadius = d * 0.38;
            gloss.hidden = flat;
        }
        button.layer.cornerRadius = cr;
        if (flat) {
            button.layer.shadowOpacity = 0.10f;
            button.layer.shadowRadius = 3;
            button.layer.shadowOffset = CGSizeMake(0, 1);
            button.layer.borderWidth = 0;
            SenkoApplyShadowPath(button.layer, cr);
        } else {
            SenkoApplyShadowPath(button.layer, cr);
        }
        SenkoEndSilentLayers();
        return body;
    }

    SenkoBeginSilentLayers();
    button.layer.cornerRadius = d / 2;
    button.layer.masksToBounds = NO;
    if (flat) {
        button.layer.borderWidth = 0;
        button.layer.borderColor = [UIColor clearColor].CGColor;
        button.layer.shadowColor = [UIColor blackColor].CGColor;
        button.layer.shadowOffset = CGSizeMake(0, 1);
        button.layer.shadowRadius = 3;
        button.layer.shadowOpacity = 0.10f;
    } else {
        button.layer.borderWidth = 0;
        button.layer.borderColor = [UIColor clearColor].CGColor;
        button.layer.shadowColor = [UIColor blackColor].CGColor;
        button.layer.shadowOffset = CGSizeMake(0, 4);
        button.layer.shadowRadius = 5;
        button.layer.shadowOpacity = 0.45;
    }
    SenkoApplyShadowPath(button.layer, d / 2);

    CAGradientLayer *rim = SenkoNamedGradientLayer(button.layer, @"rim");
    if (!rim) {
        rim = [CAGradientLayer layer];
        rim.name = @"rim";
        [button.layer insertSublayer:rim atIndex:0];
    }
    rim.frame = CGRectInset(button.bounds, (flat) ? 0 : -2,
                            (flat) ? 0 : -2);
    rim.cornerRadius = (flat) ? d / 2 : (d + 4) / 2;
    rim.hidden = flat;
    rim.colors = [NSArray arrayWithObjects:
                  (id)[UIColor colorWithWhite:0.95 alpha:1].CGColor,
                  (id)[UIColor colorWithWhite:0.35 alpha:1].CGColor, nil];

    CAGradientLayer *body = SenkoNamedGradientLayer(button.layer, @"body");
    if (!body) {
        body = [CAGradientLayer layer];
        body.name = @"body";
        [button.layer insertSublayer:body above:rim];
    }
    body.frame = button.bounds;
    body.cornerRadius = d / 2;
    if (flat)
        body.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)top.CGColor, nil];
    else
        body.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)bottom.CGColor, nil];

    CAGradientLayer *gloss = SenkoNamedGradientLayer(button.layer, @"gloss");
    if (!gloss) {
        gloss = [CAGradientLayer layer];
        gloss.name = @"gloss";
        [button.layer insertSublayer:gloss above:body];
    }
    gloss.frame = CGRectMake(d * 0.12, d * 0.08, d * 0.76, d * 0.42);
    gloss.cornerRadius = d * 0.38;
    gloss.hidden = flat;
    gloss.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1 alpha:0.72].CGColor,
                    (id)[UIColor colorWithWhite:1 alpha:0.08].CGColor, nil];
/* rasterize; dome is expensive on armv7 */
    button.layer.shouldRasterize = YES;
    button.layer.rasterizationScale = [UIScreen mainScreen].scale;
    SenkoEndSilentLayers();
    SenkoStyleChromeTitle(button);
    if (flat)
        button.titleLabel.font = SenkoFontTitle(20);
    SenkoStyleRemember(button, button.bounds.size, top, bottom);
    return body;
}
