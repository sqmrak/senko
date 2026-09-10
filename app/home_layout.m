#import "home_layout.h"
#import "ui_theme.h"
#import "app_common.h"
#import "ui_chrome_priv.h"

#include <math.h>

static NSString * const kOrbPulseKey = @"senko.orb.pulse";

@implementation SenkoHomeCard
@end

/* a theme can repaint every other surface, but a failure has to stay readable
   as a failure, so the error tint is fixed instead of coming from the palette */
static UIColor *SenkoErrorTint(void) {
    return [UIColor colorWithRed:0.87f green:0.31f blue:0.29f alpha:1.0f];
}

static BOOL IsPad(void) {
    return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad;
}

static BOOL IsCompact(CGFloat height) {
    if (IsPad()) return NO;
    return height <= 568.0f;
}

static CGFloat Lerp(CGFloat a, CGFloat b, CGFloat t) {
    return a + (b - a) * t;
}

static CGRect LerpRect(CGRect a, CGRect b, CGFloat t) {
    return CGRectMake(Lerp(a.origin.x, b.origin.x, t),
                      Lerp(a.origin.y, b.origin.y, t),
                      Lerp(a.size.width, b.size.width, t),
                      Lerp(a.size.height, b.size.height, t));
}

/* the layout pass runs on every scroll frame, so anything that allocates has
   to be guarded by a comparison first */
static void SetLabelSize(UILabel *label, CGFloat size, BOOL semibold) {
    if (!label) return;
    if (label.font && fabsf((float)(label.font.pointSize - size)) < 0.25f) return;
    label.font = SenkoFontBody(size, semibold);
}

/* one connect pill, one title at a time: measuring it on every frame allocates
   an attributes dictionary the run loop then has to drain */
static NSString *gPillTitle;
static CGFloat gPillWidth;

static CGFloat PillTextWidth(NSString *text, UIFont *font) {
    if (![text length]) return 0.0f;
    if (gPillTitle && [gPillTitle isEqualToString:text]) return gPillWidth;
    [gPillTitle release];
    gPillTitle = [text copy];
    gPillWidth = ceilf(SenkoTextWidth(text, font));
    return gPillWidth;
}

/* uikit still does per-property work for a frame it already has, and the
   layout pass runs on every scroll frame */
static void SetFrame(UIView *view, CGRect frame) {
    if (!view) return;
    if (CGAffineTransformIsIdentity(view.transform)) {
        if (!CGRectEqualToRect(view.frame, frame)) view.frame = frame;
        return;
    }
    /* frame is derived from bounds, centre and transform, so writing it while a
       press or a reveal is running would fold the animation into the geometry */
    view.bounds = CGRectMake(0, 0, frame.size.width, frame.size.height);
    view.center = CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame));
}

static CGFloat ContentWidth(CGFloat width, CGFloat height) {
    BOOL land = width > height;
    if (IsPad()) {
        CGFloat cap = land ? 720.0f : 640.0f;
        CGFloat want = width - (land ? 80.0f : 56.0f);
        return want > cap ? cap : want;
    }
    if (land && width > 640.0f) return 560.0f;
    return width;
}

static CGFloat SideInset(CGFloat width) {
    if (IsPad()) return 20.0f;
    return width <= 320.0f ? 12.0f : 16.0f;
}

/* every vertical measurement the card needs, derived once so the open height
   is the sum of its parts and nothing can land on top of anything else */
typedef struct {
    CGFloat cardPad;
    CGFloat orbOpen, orbShut;
    CGFloat stateH, detailH;
    CGFloat btnOpen, btnShut;
    CGFloat gap;
    CGFloat openH, shutH;
    CGFloat stateSizeOpen, stateSizeShut;
    int     detailLines;
} CardMetrics;

static CardMetrics MetricsFor(CGFloat width, CGFloat height) {
    BOOL pad = IsPad();
    BOOL compact = IsCompact(height);
    /* a phone on its side has barely 320pt of height; the open card has to give
       most of it back to the list */
    BOOL land = !pad && width > height;
    CardMetrics m;
    m.cardPad = pad ? 18.0f : (land ? 10.0f : (compact ? 13.0f : 16.0f));
    m.orbOpen = pad ? 52.0f : (land ? 32.0f : (compact ? 38.0f : 44.0f));
    m.orbShut = pad ? 32.0f : (compact ? 24.0f : 28.0f);
    m.stateSizeOpen = pad ? 22.0f : (compact ? 17.0f : 19.0f);
    m.stateSizeShut = pad ? 18.0f : (compact ? 14.0f : 15.0f);
    m.stateH = ceilf(m.stateSizeOpen * 1.25f);
    /* the detail line is written by SetStatusDefault at 14pt, so the box has to
       be sized for that face and not for the one this file would pick */
    m.detailLines = land ? 1 : 2;
    m.detailH = ceilf((pad ? 16.0f : 14.0f) * 1.3f) * (CGFloat)m.detailLines;
    m.btnOpen = pad ? 48.0f : (land ? 36.0f : (compact ? 40.0f : 44.0f));
    m.btnShut = pad ? 36.0f : (compact ? 30.0f : 32.0f);
    m.gap = land ? 8.0f : (compact ? 10.0f : 12.0f);

    CGFloat textBlock = m.stateH + 3.0f + m.detailH;
    CGFloat headBlock = m.orbOpen > textBlock ? m.orbOpen : textBlock;
    m.openH = m.cardPad + headBlock + m.gap + m.btnOpen + m.cardPad;

    CGFloat shutPad = compact ? 9.0f : 11.0f;
    CGFloat shutBlock = m.orbShut > m.btnShut ? m.orbShut : m.btnShut;
    m.shutH = shutPad * 2.0f + shutBlock;
    return m;
}

static CGFloat HeaderBandHeight(CGFloat height, CGFloat top) {
    BOOL pad = IsPad();
    BOOL compact = IsCompact(height);
    CGFloat headerH = pad ? 44.0f : (compact ? 34.0f : 38.0f);
    return top + (pad ? 14.0f : 10.0f) + headerH + (compact ? 10.0f : 14.0f);
}


SenkoHomeCard *SenkoHomeBuildStatusCard(UIView *root) {
    if (!root) return nil;
    UIView *existing = [root viewWithTag:SenkoHomeTagCard];
    if ([existing isKindOfClass:[SenkoHomeCard class]])
        return (SenkoHomeCard *)existing;

    SenkoHomeCard *card = [[[SenkoHomeCard alloc] initWithFrame:CGRectZero] autorelease];
    card.tag = SenkoHomeTagCard;
    card.backgroundColor = [UIColor clearColor];
    [root addSubview:card];

    /* the ring only animates while connecting, so an idle screen costs no
       compositing work on an armv7 device */
    card->ring = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    card->ring.userInteractionEnabled = NO;
    card->ring.backgroundColor = [UIColor clearColor];
    card->ring.layer.borderWidth = 2.0f;
    card->ring.alpha = 0.0f;
    [card addSubview:card->ring];

    card->orb = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    card->orb.userInteractionEnabled = NO;
    [card addSubview:card->orb];

    card->core = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    card->core.userInteractionEnabled = NO;
    [card->orb addSubview:card->core];

    /* a dot says "some state"; the shield says the tunnel is carrying traffic,
       which is the one state worth naming without reading */
    card->glyph = [[[UIImageView alloc] initWithFrame:CGRectZero] autorelease];
    card->glyph.contentMode = UIViewContentModeScaleAspectFit;
    card->glyph.userInteractionEnabled = NO;
    card->glyph.hidden = YES;
    [card->orb addSubview:card->glyph];

    card->state = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    card->state.backgroundColor = [UIColor clearColor];
    card->state.numberOfLines = 1;
    /* one line that scales instead of ellipsing: the compact card has no room
       to spare and a clipped state word reads as a bug */
    card->state.adjustsFontSizeToFitWidth = YES;
    card->state.lineBreakMode = NSLineBreakByClipping;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    card->state.minimumFontSize = 11.0f;
#pragma clang diagnostic pop
    if ([card->state respondsToSelector:@selector(setMinimumScaleFactor:)])
        card->state.minimumScaleFactor = 0.7f;
    [card addSubview:card->state];

    return card;
}

UIColor *SenkoPillLabelColor(UIColor *fill) {
    CGFloat r = 0, g = 0, b = 0, a = 1;
    if (![fill respondsToSelector:@selector(getRed:green:blue:alpha:)] ||
        ![fill getRed:&r green:&g blue:&b alpha:&a]) {
        CGFloat w = 0;
        if ([fill respondsToSelector:@selector(getWhite:alpha:)] &&
            [fill getWhite:&w alpha:&a])
            r = g = b = w;
    }
    CGFloat luma = 0.299f * r + 0.587f * g + 0.114f * b;
    return luma > 0.62f ? [UIColor colorWithWhite:0.10f alpha:1.0f]
                        : [UIColor whiteColor];
}

static CAGradientLayer *PillFill(UIButton *button) {
    for (CALayer *layer in button.layer.sublayers) {
        if ([layer.name isEqualToString:@"pillFill"] &&
            [layer isKindOfClass:[CAGradientLayer class]])
            return (CAGradientLayer *)layer;
    }
    CAGradientLayer *fill = [CAGradientLayer layer];
    fill.name = @"pillFill";
    fill.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                    [NSNull null], @"colors",
                    [NSNull null], @"bounds",
                    [NSNull null], @"position",
                    [NSNull null], @"cornerRadius",
                    [NSNull null], @"startPoint",
                    [NSNull null], @"endPoint", nil];
    fill.startPoint = CGPointMake(0.0f, 0.5f);
    fill.endPoint = CGPointMake(1.0f, 0.5f);
    [button.layer insertSublayer:fill atIndex:0];
    return fill;
}

/* the fill layer does not inherit the button's bounds, and the icon has to be
   nudged symmetrically or the title sits off centre inside the pill */
static void SyncPill(UIButton *button, CGFloat iconGap) {
    if (!button) return;
    CGFloat radius = button.bounds.size.height * 0.5f;
    button.layer.cornerRadius = radius;
    CAGradientLayer *fill = PillFill(button);
    SenkoSetLayerFrame(fill, button.bounds);
    fill.cornerRadius = radius;
    /* uikit centres the image and title as one run, so a one sided inset
       pushes the pair off centre; splitting the gap keeps it symmetric */
    button.contentEdgeInsets = UIEdgeInsetsZero;
    button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentCenter;
    button.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
    button.titleLabel.textAlignment = NSTextAlignmentCenter;
    if (button.currentImage && iconGap > 0.0f) {
        CGFloat half = iconGap * 0.5f;
        button.imageEdgeInsets = UIEdgeInsetsMake(0, -half, 0, half);
        button.titleEdgeInsets = UIEdgeInsetsMake(0, half, 0, -half);
    } else {
        button.imageEdgeInsets = UIEdgeInsetsZero;
        button.titleEdgeInsets = UIEdgeInsetsZero;
    }
}

static void StyleOrb(SenkoHomeCard *card, UIColor *tint, BOOL pulsing, BOOL carrying) {
    if (!card || !card->orb) return;
    card->orb.backgroundColor = [tint colorWithAlphaComponent:0.18f];
    card->core.backgroundColor = tint;
    card->ring.layer.borderColor = [tint colorWithAlphaComponent:0.55f].CGColor;
    for (CALayer *sub in card->ring.layer.sublayers) {
        if ([sub.name isEqualToString:@"heartRing"] && [sub isKindOfClass:[CAShapeLayer class]]) {
            ((CAShapeLayer *)sub).strokeColor = [tint colorWithAlphaComponent:0.55f].CGColor;
            break;
        }
    }
    if (SenkoThemeIsMiside()) {
        card->core.hidden = NO;
        card->glyph.hidden = YES;
    } else {
        card->core.hidden = carrying;
        card->glyph.hidden = !carrying;
        /* drawn once at the largest size the orb ever reaches and scaled down by the
           image view, so collapsing the card does not redraw it per frame */
        if (carrying) card->glyph.image = SenkoIconShield(48.0f, tint);
    }

    if (!pulsing) {
        [card->ring.layer removeAnimationForKey:kOrbPulseKey];
        card->ring.alpha = 0.0f;
        return;
    }
    card->ring.alpha = 1.0f;
    if ([card->ring.layer animationForKey:kOrbPulseKey]) return;
    CABasicAnimation *scale = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
    scale.fromValue = [NSNumber numberWithFloat:0.72f];
    scale.toValue = [NSNumber numberWithFloat:1.32f];
    CABasicAnimation *fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
    fade.fromValue = [NSNumber numberWithFloat:0.85f];
    fade.toValue = [NSNumber numberWithFloat:0.0f];
    CAAnimationGroup *group = [CAAnimationGroup animation];
    group.animations = [NSArray arrayWithObjects:scale, fade, nil];
    group.duration = 1.35;
    group.repeatCount = HUGE_VALF;
    group.removedOnCompletion = NO;
    [card->ring.layer addAnimation:group forKey:kOrbPulseKey];
}

static void StyleHeaderButton(UIButton *button, UIImage *icon, BOOL filled) {
    if (!button) return;
    [button setTitle:nil forState:UIControlStateNormal];
    if (icon) [button setImage:icon forState:UIControlStateNormal];
    button.imageView.contentMode = UIViewContentModeScaleAspectFit;
    button.layer.masksToBounds = YES;
    button.backgroundColor = filled ? [kAccentBlue colorWithAlphaComponent:0.12f]
                                     : [UIColor clearColor];
    button.layer.borderWidth = 0.0f;
}

void SenkoHomeStyleChrome(const SenkoHomeChrome *ui) {
    if (!ui || !ui->card) return;
    SenkoHomeCard *card = ui->card;

    CGFloat radius = SenkoThemeCardRadius();
    if (radius < 16.0f) radius = 16.0f;
    card.layer.cornerRadius = radius;
    card.layer.masksToBounds = YES;

    /* rows scroll underneath the card, so the surface has to be opaque, and it
       has to sit deeper than the row cards or the screen reads inside out */
    CAGradientLayer *cardFill = nil;
    for (CALayer *layer in card.layer.sublayers) {
        if ([layer.name isEqualToString:@"cardFill"] &&
            [layer isKindOfClass:[CAGradientLayer class]]) {
            cardFill = (CAGradientLayer *)layer;
            break;
        }
    }
    if (!cardFill) {
        cardFill = [CAGradientLayer layer];
        cardFill.name = @"cardFill";
        cardFill.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                            [NSNull null], @"colors",
                            [NSNull null], @"bounds",
                            [NSNull null], @"position", nil];
        [card.layer insertSublayer:cardFill atIndex:0];
    }
    /* a light theme cannot go far toward black before the ink stops reading, so
       the two depths differ */
    CGFloat sink = SenkoThemeIsLight() ? -0.10f : -0.34f;
    UIColor *top = SenkoShadeColor([kCellHi colorWithAlphaComponent:1.0f], sink);
    UIColor *bottom = SenkoShadeColor([kCellLo colorWithAlphaComponent:1.0f],
                                      sink - 0.06f);
    cardFill.colors = [NSArray arrayWithObjects:(id)top.CGColor,
                                                (id)bottom.CGColor, nil];
    card.backgroundColor = [UIColor clearColor];
    if (SenkoThemeIsIos26() || SenkoThemeUsesFrost()) {
        SenkoInstallFrostLite(card);
        cardFill.opacity = 0.82f;
    } else {
        SenkoRemoveFrost(card);
        cardFill.opacity = 1.0f;
    }
    card.layer.borderWidth = 1.0f;
    card.layer.borderColor = (SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0.0f alpha:0.08f]
        : [UIColor colorWithWhite:1.0f alpha:0.13f]).CGColor;

    card->state.textColor = kInk;
    card->state.shadowColor = nil;
    card->state.shadowOffset = CGSizeZero;

    /* the three header controls share one shape; a bare glyph next to two
       filled circles was what made the refresh button look out of place */
    StyleHeaderButton(ui->refresh, SenkoIconRefresh(22.0f, kAccentBlue), YES);
    StyleHeaderButton(ui->gear, SenkoGearIcon(22.0f, kAccentBlue), YES);
    StyleHeaderButton(ui->plus, SenkoPlusIcon(22.0f, kAccentBlue), YES);

    if (ui->check) {
        [ui->check setImage:GaugeIcon(20.0f, kAccentBlue) forState:UIControlStateNormal];
        UIColor *veil = [kAccentBlue colorWithAlphaComponent:0.12f];
        CAGradientLayer *fill = PillFill(ui->check);
        fill.colors = [NSArray arrayWithObjects:(id)veil.CGColor, (id)veil.CGColor, nil];
        ui->check.layer.borderWidth = 1.0f;
        ui->check.layer.borderColor = [kAccentBlue colorWithAlphaComponent:0.30f].CGColor;
        ui->check.layer.masksToBounds = YES;
    }
    if (ui->connect) {
        BOOL compactScreen = IsCompact(SenkoViewBounds(ui->card.superview).size.height);
        ui->connect.titleLabel.font = SenkoFontBody(IsPad() ? 18.0f : (compactScreen ? 15.0f : 16.0f), YES);
        [gPillTitle release];
        gPillTitle = nil; /* the face changed, so the cached width is stale */
    }
    if (ui->detail) {
        ui->detail.textAlignment = NSTextAlignmentLeft;
        ui->detail.lineBreakMode = NSLineBreakByTruncatingTail;
    }
}

void SenkoHomeApplyStatus(const SenkoHomeChrome *ui, NSString *state,
                          NSString *title, BOOL animated) {
    if (!ui || !ui->card) return;
    BOOL connecting = [state isEqualToString:@"connecting"];
    BOOL connected = [state isEqualToString:@"connected"];
    BOOL failed = [state isEqualToString:@"error"];

    UIColor *tint = connected ? kConnOn
                  : connecting ? kAccentBlue
                  : failed ? SenkoErrorTint()
                  : kIdleGrey;
    UIColor *tintLo = connected ? kConnOnLo
                    : connecting ? kAccentBlueLo
                    : failed ? SenkoErrorTint()
                    : kIdleGreyLo;

    UILabel *label = ui->card->state;
    if (title && ![title isEqualToString:label.text]) {
        if (animated) {
            /* the headline changes on every state hop, so it dissolves instead
               of snapping under an orb that is already moving */
            SenkoAnimate(0.18, ^{ label.alpha = 0.0f; }, ^(BOOL done) {
                (void)done;
                label.text = title;
                SenkoAnimate(0.22, ^{ label.alpha = 1.0f; }, NULL);
            });
        } else {
            label.text = title;
            label.alpha = 1.0f;
        }
    }

    StyleOrb(ui->card, tint, connecting, connected);

    if (ui->connect) {
        CAGradientLayer *fill = PillFill(ui->connect);
        /* kIdleGrey and kIdleGreyLo are the same colour in several palettes,
           which painted the pill as a flat slab */
        UIColor *pillTop = SenkoShadeColor(tint, 0.14f);
        UIColor *pillBottom = SenkoShadeColor(tintLo, -0.14f);
        fill.colors = [NSArray arrayWithObjects:(id)pillTop.CGColor,
                                                (id)pillBottom.CGColor, nil];
        ui->connect.layer.borderWidth = 0.0f;
        UIColor *ink = SenkoPillLabelColor(tint);
        [ui->connect setTitleColor:ink forState:UIControlStateNormal];
        [ui->connect setImage:nil forState:UIControlStateNormal];
        NSString *action = connected ? @"Disconnect"
                         : connecting ? @"Cancel"
                         : @"Connect";
        [ui->connect setTitle:SenkoLocalizedText(action) forState:UIControlStateNormal];
    }
}

CGPoint SenkoHomeOrbCenter(const SenkoHomeChrome *ui) {
    if (!ui || !ui->card || !ui->card->orb) return CGPointZero;
    return CGPointMake(ui->card.frame.origin.x + ui->card->orb.center.x,
                       ui->card.frame.origin.y + ui->card->orb.center.y);
}

void SenkoHomeLayout(UIView *root, const SenkoHomeChrome *ui, CGFloat headerProgress) {
    if (!root || !ui || !ui->table) return;
    CGRect bounds = root.bounds;
    CGFloat W = bounds.size.width;
    CGFloat H = bounds.size.height;
    if (W < 1.0f || H < 1.0f) return;
    BOOL pad = IsPad();
    BOOL compact = IsCompact(H);

    CGFloat t = headerProgress;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    UIEdgeInsets safe = SenkoSafeAreaInsets(root);
    CGFloat top = GetTopOffset();
    if (safe.top > top) top = safe.top;
    CGFloat usableW = W - safe.left - safe.right;
    if (usableW < 1.0f) usableW = W;
    CGFloat contentW = ContentWidth(usableW, H);
    CGFloat contentX = safe.left + (usableW - contentW) / 2.0f;
    CGFloat inset = SideInset(contentW);

    SenkoSetLayerFrame(ui->background, bounds);

    /* header: the wordmark reads from the leading edge, the controls sit
       opposite it, all on one row */
    CGFloat headerH = pad ? 44.0f : (compact ? 34.0f : 38.0f);
    CGFloat headerY = top + (pad ? 14.0f : 10.0f);
    CGFloat side = pad ? 42.0f : (compact ? 32.0f : 36.0f);
    CGFloat gap = compact ? 6.0f : 8.0f;
    CGFloat controlsY = headerY + (headerH - side) * 0.5f;
    CGFloat rightEdge = contentX + contentW - inset;

    SetFrame(ui->plus, CGRectMake(rightEdge - side, controlsY, side, side));
    SetFrame(ui->gear, CGRectMake(rightEdge - side * 2.0f - gap, controlsY, side, side));
    SetFrame(ui->refresh, CGRectMake(rightEdge - side * 3.0f - gap * 2.0f,
                                     controlsY, side, side));
    /* the radius has to follow the frame: styling runs before the first layout,
       when the bounds are still zero and a computed radius leaves them square */
    ui->plus.layer.cornerRadius = side * 0.5f;
    ui->gear.layer.cornerRadius = side * 0.5f;
    ui->refresh.layer.cornerRadius = side * 0.5f;
    /* the generated glyphs already have their final point size. constraining
       the image view a second time clipped the refresh arc on 320pt screens */
    ui->plus.imageEdgeInsets = UIEdgeInsetsZero;
    ui->gear.imageEdgeInsets = UIEdgeInsetsZero;
    ui->refresh.imageEdgeInsets = UIEdgeInsetsZero;

    CGFloat controlsWidth = side * 3.0f + gap * 2.0f;
    CGFloat titleW = contentW - inset * 2.0f - controlsWidth - gap;
    if (titleW < 60.0f) titleW = 60.0f;
    SetFrame(ui->title, CGRectMake(contentX + inset, headerY, titleW, headerH));

    /* card: two target shapes, then one interpolation. deriving both from the
       same metrics is what keeps the detail line off the button row */
    CardMetrics m = MetricsFor(W, H);
    CGFloat cardX = contentX + inset;
    CGFloat cardW = contentW - inset * 2.0f;
    CGFloat cardY = HeaderBandHeight(H, top);
    CGFloat cardH = Lerp(m.openH, m.shutH, t);
    SetFrame(ui->card, CGRectMake(cardX, cardY, cardW, cardH));
    for (CALayer *layer in ui->card.layer.sublayers) {
        if ([layer.name isEqualToString:@"cardFill"])
            SenkoSetLayerFrame(layer, ui->card.bounds);
    }

    CGFloat orbSide = Lerp(m.orbOpen, m.orbShut, t);
    CGFloat shutPad = (m.shutH - (m.orbShut > m.btnShut ? m.orbShut : m.btnShut)) * 0.5f;
    CGRect orbOpenR = CGRectMake(m.cardPad, m.cardPad, m.orbOpen, m.orbOpen);
    CGRect orbShutR = CGRectMake(m.cardPad, (m.shutH - m.orbShut) * 0.5f,
                                 m.orbShut, m.orbShut);
    CGRect orbFrame = LerpRect(orbOpenR, orbShutR, t);
    orbFrame.size.width = orbSide;
    orbFrame.size.height = orbSide;

    SenkoHomeCard *card = ui->card;
    SetFrame(card->orb, orbFrame);
    SetFrame(card->ring, orbFrame);
    CGFloat coreSide = orbSide * 0.46f;
    SetFrame(card->core, CGRectMake((orbSide - coreSide) * 0.5f,
                                    (orbSide - coreSide) * 0.5f, coreSide, coreSide));
    CGFloat glyphSide = orbSide * 0.62f;
    SetFrame(card->glyph, CGRectMake((orbSide - glyphSide) * 0.5f,
                                     (orbSide - glyphSide) * 0.5f, glyphSide, glyphSide));

    CAShapeLayer *heartRing = nil;
    for (CALayer *sub in card->ring.layer.sublayers) {
        if ([sub.name isEqualToString:@"heartRing"]) {
            heartRing = (CAShapeLayer *)sub;
            break;
        }
    }
    if (SenkoThemeIsMiside()) {
        card->orb.layer.cornerRadius = 0.0f;
        /* each of these builds a shape layer over a fresh bezier path, and the
           orb resizes on every frame of the collapse, so they are rebuilt only
           when the shape they were cut for actually changed */
        if (!CGRectEqualToRect(card->orb.layer.mask.bounds, card->orb.bounds))
            card->orb.layer.mask = SenkoHeartMaskLayer(card->orb.bounds);
        card->core.layer.cornerRadius = 0.0f;
        if (!CGRectEqualToRect(card->core.layer.mask.bounds, card->core.bounds))
            card->core.layer.mask = SenkoHeartMaskLayer(card->core.bounds);
        card->ring.layer.cornerRadius = 0.0f;
        card->ring.layer.borderWidth = 0.0f;
        card->ring.layer.mask = nil;
        if (!heartRing) {
            heartRing = [CAShapeLayer layer];
            heartRing.name = @"heartRing";
            heartRing.fillColor = [UIColor clearColor].CGColor;
            heartRing.lineWidth = 2.0f;
            heartRing.contentsScale = [UIScreen mainScreen].scale;
            [card->ring.layer addSublayer:heartRing];
        }
        if (!CGRectEqualToRect(heartRing.frame, card->ring.bounds)) {
            heartRing.frame = card->ring.bounds;
            heartRing.path = SenkoHeartPath(card->ring.bounds).CGPath;
        }
        heartRing.strokeColor = card->ring.layer.borderColor;
    } else {
        if (heartRing) [heartRing removeFromSuperlayer];
        card->orb.layer.mask = nil;
        card->ring.layer.mask = nil;
        card->core.layer.mask = nil;
        card->orb.layer.cornerRadius = orbSide * 0.5f;
        card->ring.layer.cornerRadius = orbSide * 0.5f;
        card->ring.layer.borderWidth = 2.0f;
        card->core.layer.cornerRadius = coreSide * 0.5f;
    }

    CGFloat btnH = Lerp(m.btnOpen, m.btnShut, t);
    CGFloat innerW = cardW - m.cardPad * 2.0f;

    /* the collapsed pill is sized to its own title, and drops the glyph, so a
       russian verb on a 320pt screen never has to ellipse */
    SetLabelSize(card->state, Lerp(m.stateSizeOpen, m.stateSizeShut, t), YES);
    UIFont *pillFont = ui->connect ? ui->connect.titleLabel.font : nil;
    NSString *pillText = [ui->connect titleForState:UIControlStateNormal];
    CGFloat pillTextW = PillTextWidth(pillText, pillFont);
    CGFloat connectWShut = pillTextW + m.btnShut;
    CGFloat maxShut = innerW - m.orbShut - 10.0f - 56.0f;
    if (connectWShut > maxShut) connectWShut = maxShut;
    if (connectWShut < 72.0f) connectWShut = 72.0f;

    CGFloat checkW = m.btnOpen + 14.0f;
    CGFloat connectWOpen = innerW - checkW - 10.0f;

    CGRect connectOpenR = CGRectMake(m.cardPad, m.openH - m.cardPad - m.btnOpen,
                                     connectWOpen, m.btnOpen);
    CGRect connectShutR = CGRectMake(cardW - m.cardPad - connectWShut, shutPad,
                                     connectWShut, m.btnShut);
    CGRect connectFrame = LerpRect(connectOpenR, connectShutR, t);
    connectFrame.size.height = btnH;
    SetFrame(ui->connect, connectFrame);

    CGRect checkOpenR = CGRectMake(cardW - m.cardPad - checkW,
                                   m.openH - m.cardPad - m.btnOpen, checkW, m.btnOpen);
    /* it slides off the trailing edge, where the card's own clip hides it,
       rather than fading out underneath the primary pill */
    CGRect checkShutR = CGRectMake(cardW - m.cardPad, shutPad, checkW, m.btnShut);
    CGRect checkFrame = LerpRect(checkOpenR, checkShutR, t);
    checkFrame.size.height = btnH;
    SetFrame(ui->check, checkFrame);
    /* the secondary pill is the first thing to go: the compact row has room for
       one action, and a half faded button under the primary one reads as a bug */
    ui->check.alpha = t > 0.4f ? 0.0f : (1.0f - t / 0.4f);
    ui->check.userInteractionEnabled = ui->check.alpha > 0.5f;

    /* the pill carries its label alone; setting the image again on every frame
       still dirties the button's layout */
    if ([ui->connect imageForState:UIControlStateNormal])
        [ui->connect setImage:nil forState:UIControlStateNormal];
    SyncPill(ui->connect, 0.0f);
    SyncPill(ui->check, 0.0f);

    CGFloat textXOpen = m.cardPad + m.orbOpen + 12.0f;
    CGFloat textXShut = m.cardPad + m.orbShut + 10.0f;
    CGFloat textX = Lerp(textXOpen, textXShut, t);
    CGFloat textWOpen = cardW - textXOpen - m.cardPad;
    CGFloat textWShut = cardW - textXShut - m.cardPad - connectWShut - 10.0f;
    if (textWShut < 40.0f) textWShut = 40.0f;
    CGFloat textW = Lerp(textWOpen, textWShut, t);

    CGFloat stateYOpen = m.cardPad;
    CGFloat stateYShut = (m.shutH - m.stateH) * 0.5f;
    SetFrame(card->state, CGRectMake(textX, Lerp(stateYOpen, stateYShut, t),
                                     textW, m.stateH));

    if (ui->detail) {
        if (ui->detail.numberOfLines != m.detailLines)
            ui->detail.numberOfLines = m.detailLines;
        SetFrame(ui->detail, CGRectMake(textXOpen, m.cardPad + m.stateH + 3.0f,
                                        textWOpen, m.detailH));
        /* the second line goes well before the row height could clip it */
        ui->detail.alpha = t > 0.3f ? 0.0f : (1.0f - t / 0.3f);
    }

    /* the list keeps one frame for the life of the screen and is pushed down by
       a scrolling spacer instead, so collapsing the card never relayouts a row.
       it takes the card's own column so the well, the rows and the card all
       share one set of edges */
    CGFloat listTop = cardY + m.shutH + (compact ? 10.0f : 14.0f);
    CGFloat listX = cardX;
    CGFloat listW = cardW;
    CGFloat listHeight = H - safe.bottom - listTop;
    if (listHeight < 60.0f) listHeight = 60.0f;
    CGRect listFrame = CGRectMake(listX, listTop, listW, listHeight);
    SetFrame(ui->table, listFrame);
    SetFrame(ui->well, listFrame);

    /* the gap the open card needs is a scrolling spacer rather than a content
       inset: an inset would park every pinned section header that far below the
       table's top edge, leaving a permanent empty band under the card.
       replacing the header re-runs the table's own layout, so it is only ever
       written while the list is at rest: doing it from a scroll frame made the
       table call back into this pass */
    CGFloat spacer = m.openH - m.shutH;
    UIView *head = ui->table.tableHeaderView;
    CGFloat headH = head ? head.bounds.size.height : 0.0f;
    if (fabsf((float)(headH - spacer)) > 0.5f &&
        !ui->table.tracking && !ui->table.decelerating) {
        UIView *fresh = [[[UIView alloc] initWithFrame:
                          CGRectMake(0, 0, listW, spacer)] autorelease];
        fresh.backgroundColor = [UIColor clearColor];
        fresh.userInteractionEnabled = NO;
        ui->table.tableHeaderView = fresh;
    }

    if (ui->well) {
        ui->well.layer.cornerRadius = pad ? 16.0f : 0.0f;
        for (CALayer *layer in ui->well.layer.sublayers) {
            if ([layer.name isEqualToString:@"wellGrad"])
                SenkoSetLayerFrame(layer, ui->well.bounds);
        }
    }

    UIView *boy = [root viewWithTag:9002];
    if (boy) SetFrame(boy, bounds);
}
