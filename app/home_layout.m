#import "home_layout.h"
#import "main_layout.h"

/* the dome carries the tunnel state in its own colours. the status pass below
   computes them once; the classic layout needs them again on every rotation,
   because the dome is restyled from its new size */
static UIColor *gClassicDomeTop = nil;
static UIColor *gClassicDomeBottom = nil;

/* the two heroes decorate the same two buttons with different layer sets and
   neither knows about the other's. leaving them behind is what drew a pill
   behind the dome and a dome behind the pill after a switch */
static void StripDecor(UIButton *button, NSArray *names) {
    NSArray *sublayers;
    if (!button) return;
    sublayers = [[button.layer.sublayers copy] autorelease];
    for (CALayer *layer in sublayers)
        if (layer.name && [names containsObject:layer.name]) [layer removeFromSuperlayer];
}

/* the dome does not only add sublayers: it paints a coloured glow straight onto
   the button's own layer and cuts that glow to a heart on the miside theme.
   removing the sublayers left the glow behind, which is the pink haze that
   stayed under the pill until senko was killed */
static void StripHeroGlow(UIButton *button) {
    if (!button) return;
    button.layer.shadowOpacity = 0.0f;
    button.layer.shadowRadius = 0.0f;
    button.layer.shadowOffset = CGSizeZero;
    button.layer.shadowColor = [UIColor clearColor].CGColor;
    button.layer.shadowPath = NULL;
    button.layer.mask = nil;
    button.titleLabel.shadowColor = nil;
    button.titleLabel.shadowOffset = CGSizeZero;
}

/* the same set StyleDomeHideThemeChrome hides, plus the plain gradient pair.
   the miside heart draws its outline in two of these, and leaving them behind
   is what kept a heart rim around the pill after switching back */
static NSArray *DomeLayerNames(void) {
    return [NSArray arrayWithObjects:@"rim", @"body", @"gloss", @"misideShade",
                                     @"misideSpec", @"misideSpecHold",
                                     @"misideHeartStroke", @"misideHeartOuter",
                                     @"ios16ring", @"ios26ring2", nil];
}

static NSArray *PillLayerNames(void) {
    /* the sheen keeps the pill's old wide frame, so leaving it attached when
       the same button becomes a dome draws a capsule across the header */
    return [NSArray arrayWithObjects:@"pillFill", @"reliefSheen", nil];
}

#import "ui_theme.h"
#import "app_common.h"
#import "ui_chrome_priv.h"

#include <math.h>
#include <objc/runtime.h>

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
   an attributes dictionary the run loop then has to drain. the cache used to
   key on the text alone, so the classic hero's 20pt title and the card's
   15-18pt one could hand back each other's width across a mode switch,
   sizing the shut pill for the wrong font and clipping "Подключить" into
   "Подкч...ть". comparing the font too makes a stale hit impossible instead
   of relying on every caller to null the cache when a font might have
   changed. */
static NSString *gPillTitle;
static UIFont *gPillFont;
static CGFloat gPillWidth;

static CGFloat PillTextWidth(NSString *text, UIFont *font) {
    if (![text length]) return 0.0f;
    if (gPillTitle && [gPillTitle isEqualToString:text] &&
        gPillFont && [gPillFont isEqual:font])
        return gPillWidth;
    [gPillTitle release];
    gPillTitle = [text copy];
    [gPillFont release];
    gPillFont = [font retain];
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

/* a scroll tick changes the heart by fractions of a point. rebuilding three
   bezier paths for that invisible delta stalls the miside theme on iphone 4 */
static BOOL SenkoLayerNeedsHeartPath(CALayer *layer, CGRect bounds) {
    if (!layer) return YES;
    return fabsf((float)(layer.bounds.size.width - bounds.size.width)) > 1.25f ||
           fabsf((float)(layer.bounds.size.height - bounds.size.height)) > 1.25f;
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

/* the detail line is one line in nearly every state ("no server selected"), and
   reserving two for it on every screen is most of what made the card stand a
   third of the way down a 480pt phone. measuring allocates, and this runs on
   every scroll frame, so the answer is cached against the text it was made for */
static NSString *gDetailText;
static CGFloat gDetailWidth;
static int gDetailLines = 1;

static int DetailLinesFor(UILabel *detail, CGFloat width) {
    CGFloat lineH;
    CGSize size;
    if (!detail || ![detail.text length] || width < 40.0f) return 1;
    if (gDetailText && gDetailWidth == width &&
        [gDetailText isEqualToString:detail.text])
        return gDetailLines;
    lineH = ceilf((CGFloat)detail.font.lineHeight);
    if (lineH < 1.0f) return 1;
    size = SenkoTextSize(detail.text, detail.font, width);
    [gDetailText release];
    gDetailText = [detail.text copy];
    gDetailWidth = width;
    gDetailLines = size.height > lineH * 1.5f ? 2 : 1;
    return gDetailLines;
}

static CardMetrics MetricsFor(CGFloat width, CGFloat height, int detailLines) {
    BOOL pad = IsPad();
    BOOL compact = IsCompact(height);
    /* a phone on its side has barely 320pt of height; the open card has to give
       most of it back to the list */
    BOOL land = !pad && width > height;
    CardMetrics m;
    m.cardPad = pad ? 18.0f : (land ? 10.0f : (compact ? 12.0f : 15.0f));
    m.orbOpen = pad ? 52.0f : (land ? 32.0f : (compact ? 38.0f : 44.0f));
    m.orbShut = pad ? 32.0f : (compact ? 24.0f : 28.0f);
    m.stateSizeOpen = pad ? 22.0f : (compact ? 17.0f : 19.0f);
    m.stateSizeShut = pad ? 18.0f : (compact ? 14.0f : 15.0f);
    m.stateH = ceilf(m.stateSizeOpen * 1.25f);
    /* the detail line is written by SetStatusDefault at 14pt, so the box has to
       be sized for that face and not for the one this file would pick */
    m.detailLines = land ? 1 : detailLines;
    if (m.detailLines < 1) m.detailLines = 1;
    if (m.detailLines > 2) m.detailLines = 2;
    m.detailH = ceilf((pad ? 16.0f : 14.0f) * 1.3f) * (CGFloat)m.detailLines;
    m.btnOpen = pad ? 48.0f : (land ? 36.0f : (compact ? 40.0f : 44.0f));
    m.btnShut = pad ? 36.0f : (compact ? 30.0f : 32.0f);
    m.gap = land ? 8.0f : (compact ? 8.0f : 10.0f);

    CGFloat textBlock = m.stateH + 3.0f + m.detailH + 17.0f;
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

    card->traffic = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    card->traffic.backgroundColor = [UIColor clearColor];
    card->traffic.font = SenkoFontBody(12.0f, NO);
    card->traffic.hidden = YES;
    [card addSubview:card->traffic];

    return card;
}

UIColor *SenkoPillLabelColor(UIColor *fill) {
    CGFloat r = 0, g = 0, b = 0, a = 1;
/* on a dark theme every filled control carries white, whatever its own fill
   does: a bright accent under dark text was the one thing on the screen reading
   as a light control, and the screen is not a light one */
    if (!SenkoThemeIsLight()) return [UIColor whiteColor];
    if (![fill respondsToSelector:@selector(getRed:green:blue:alpha:)] ||
        ![fill getRed:&r green:&g blue:&b alpha:&a]) {
        CGFloat w = 0;
        if ([fill respondsToSelector:@selector(getWhite:alpha:)] &&
            [fill getWhite:&w alpha:&a])
            r = g = b = w;
    }
/* a light theme still picks by the fill, because a pale pill there needs dark
   text and a saturated one does not */
    CGFloat luma = 0.299f * r + 0.587f * g + 0.114f * b;
    return luma > 0.62f ? [UIColor colorWithWhite:0.10f alpha:1.0f]
                        : [UIColor whiteColor];
}

/* SyncPill runs on every layout frame and the style pass runs on state changes,
   so the two ends of the ramp are remembered here rather than recomputed from a
   palette the layout pass has no business reading */
static UIColor *PillReliefTop(UIButton *button) {
    return objc_getAssociatedObject(button, &SenkoStyleTopKey);
}

static UIColor *PillReliefBottom(UIButton *button) {
    return objc_getAssociatedObject(button, &SenkoStyleBotKey);
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
/* the pill carried a flat left to right ramp; the relief turns it into a lit
   control and leaves the flat themes as they were */
    SenkoApplyRelief(button, fill, PillReliefTop(button), PillReliefBottom(button),
                     radius);
/* the fill and sheen are sublayers stacked fresh on every pass, so the
   button's own content has to be reasserted above them each time or the
   next relief redraw buries the icon again */
    if (button.imageView) [button bringSubviewToFront:button.imageView];
    if (button.titleLabel) [button bringSubviewToFront:button.titleLabel];
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
    button.backgroundColor = [UIColor clearColor];
    BOOL flat = SenkoThemeIsFlat();
    BOOL light = SenkoThemeIsLight();
    CAGradientLayer *fill = SenkoNamedGradientLayer(button.layer, @"headerFill");
    if (!fill) {
        fill = [CAGradientLayer layer];
        fill.name = @"headerFill";
        [button.layer insertSublayer:fill atIndex:0];
    }
    /* chrome colours can match the wallpaper in custom themes. the header
       actions need an accent field, otherwise their glyphs disappear */
    UIColor *top = filled ? SenkoShadeColor(kAccentBlue, 0.12f)
                          : [UIColor clearColor];
    UIColor *bottom = filled ? SenkoShadeColor(kAccentBlue, -0.16f)
                             : [UIColor clearColor];
    fill.startPoint = CGPointMake(0.5f, 0.0f);
    fill.endPoint = CGPointMake(0.5f, 1.0f);
    fill.colors = [NSArray arrayWithObjects:(id)top.CGColor,
                                             (id)bottom.CGColor, nil];
    fill.frame = button.bounds;
    fill.cornerRadius = button.bounds.size.height * 0.5f;

    /* the same lit-from-above sheen every other filled control wears: without
       it the three circles read as flat accent tiles next to the domed
       connect button and glossy pills */
    CAGradientLayer *sheen = SenkoNamedGradientLayer(button.layer, @"headerSheen");
    if (!sheen) {
        sheen = [CAGradientLayer layer];
        sheen.name = @"headerSheen";
        [button.layer insertSublayer:sheen above:fill];
    }
    sheen.hidden = !filled || flat;
    sheen.startPoint = CGPointMake(0.5f, 0.0f);
    sheen.endPoint = CGPointMake(0.5f, 1.0f);
    CGRect sb = CGRectInset(button.bounds, 1.5f, 1.5f);
    sheen.frame = CGRectMake(sb.origin.x, sb.origin.y, sb.size.width, sb.size.height * 0.52f);
    sheen.cornerRadius = sheen.frame.size.height;
    sheen.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1 alpha:0.40f].CGColor,
                    (id)[UIColor colorWithWhite:1 alpha:0.02f].CGColor, nil];

    button.layer.borderWidth = filled ? 1.0f : 0.0f;
    button.layer.borderColor = filled
        ? [UIColor colorWithWhite:1.0f alpha:0.34f].CGColor
        : [UIColor clearColor].CGColor;

    /* the fill and sheen clip themselves through their own corner radius, so
       the button layer can stay unmasked and cast a real drop shadow instead
       of the flat accent tile a masked layer is stuck with */
    button.layer.masksToBounds = NO;
    if (filled) {
        button.layer.shadowColor = [UIColor blackColor].CGColor;
        button.layer.shadowOffset = CGSizeMake(0, flat ? 1.0f : 2.0f);
        button.layer.shadowOpacity = flat ? (light ? 0.12f : 0.28f)
                                          : (light ? 0.22f : 0.42f);
        button.layer.shadowRadius = flat ? 2.0f : 3.0f;
        SenkoApplyShadowPath(button.layer, button.bounds.size.height * 0.5f);
    } else {
        button.layer.shadowOpacity = 0.0f;
        button.layer.shadowPath = nil;
    }
}

static void LayoutHeaderButton(UIButton *button) {
    if (!button) return;
    CGFloat r = button.bounds.size.height * 0.5f;
    for (CALayer *layer in button.layer.sublayers) {
        if ([layer.name isEqualToString:@"headerFill"]) {
            layer.frame = button.bounds;
            layer.cornerRadius = r;
        } else if ([layer.name isEqualToString:@"headerSheen"]) {
            CGRect sb = CGRectInset(button.bounds, 1.5f, 1.5f);
            layer.frame = CGRectMake(sb.origin.x, sb.origin.y,
                                     sb.size.width, sb.size.height * 0.52f);
            layer.cornerRadius = layer.frame.size.height;
        }
    }
    if (button.layer.shadowOpacity > 0.0f)
        SenkoApplyShadowPath(button.layer, r);
}

void SenkoHomeStyleChrome(const SenkoHomeChrome *ui) {
    if (!ui || !ui->card) return;
    SenkoHomeCard *card = ui->card;
    BOOL flat = SenkoThemeIsFlat();
    BOOL ios16 = SenkoThemeIsIos16();
    BOOL ios26 = SenkoThemeIsIos26();
    BOOL light = SenkoThemeIsLight();
/* ios7 is the only theme with no light source at all; every other theme,
   glass included, still lights this card the way it lights its own dome and
   capsule controls */
    BOOL trulyFlat = flat && !ios16;

    CGFloat radius = SenkoThemeCardRadius();
    if (radius < 16.0f) radius = 16.0f;
    card.layer.cornerRadius = radius;
    /* the fill, sheen and frost each clip themselves through their own corner
       radius, so the card layer only needs to mask when it casts no shadow */
    card.layer.masksToBounds = trulyFlat;

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
    cardFill.cornerRadius = radius;
    /* a light theme cannot go far toward black before the ink stops reading, so
       the two depths differ */
    CGFloat sink = SenkoThemeIsLight() ? -0.10f : -0.34f;
    UIColor *top = SenkoShadeColor([kCellHi colorWithAlphaComponent:1.0f], sink);
    UIColor *bottom = SenkoShadeColor([kCellLo colorWithAlphaComponent:1.0f],
                                      sink - 0.06f);
    cardFill.colors = [NSArray arrayWithObjects:(id)top.CGColor,
                                                (id)bottom.CGColor, nil];
    card.backgroundColor = [UIColor clearColor];
    if (ios26 || SenkoThemeUsesFrost()) {
        SenkoInstallFrostLite(card);
        cardFill.opacity = 0.82f;
    } else {
        SenkoRemoveFrost(card);
        cardFill.opacity = 1.0f;
    }
    card.layer.borderWidth = 1.0f;
    card.layer.borderColor = (light
        ? [UIColor colorWithWhite:0.0f alpha:0.08f]
        : [UIColor colorWithWhite:1.0f alpha:0.13f]).CGColor;

    /* the same lit-from-above sheen every filled control wears, so the pill
       reads as a raised plate instead of a flat slab dropped under the header */
    CAGradientLayer *cardSheen = SenkoNamedGradientLayer(card.layer, @"cardSheen");
    if (!cardSheen) {
        cardSheen = [CAGradientLayer layer];
        cardSheen.name = @"cardSheen";
        [card.layer insertSublayer:cardSheen above:cardFill];
    }
    cardSheen.hidden = trulyFlat;
    CGRect sb = CGRectInset(card.bounds, 1.0f, 1.0f);
    cardSheen.frame = CGRectMake(sb.origin.x, sb.origin.y, sb.size.width,
                                 sb.size.height * (ios16 ? 0.34f : 0.42f));
    cardSheen.cornerRadius = radius > 1.0f ? radius - 1.0f : 0.0f;
    cardSheen.startPoint = CGPointMake(0.5f, 0.0f);
    cardSheen.endPoint = CGPointMake(0.5f, 1.0f);
    cardSheen.colors = [NSArray arrayWithObjects:
                        (id)[UIColor colorWithWhite:1 alpha:ios16 ? 0.16f : 0.24f].CGColor,
                        (id)[UIColor colorWithWhite:1 alpha:0.0f].CGColor, nil];

    if (trulyFlat) {
        card.layer.shadowOpacity = 0.0f;
        card.layer.shadowPath = NULL;
    } else {
        card.layer.shadowColor = [UIColor blackColor].CGColor;
        if (ios26) {
            card.layer.shadowOffset = CGSizeMake(0.0f, 6.0f);
            card.layer.shadowOpacity = light ? 0.14f : 0.34f;
            card.layer.shadowRadius = 12.0f;
        } else if (ios16) {
            card.layer.shadowOffset = CGSizeMake(0.0f, 3.0f);
            card.layer.shadowOpacity = light ? 0.14f : 0.30f;
            card.layer.shadowRadius = 7.0f;
        } else {
            card.layer.shadowOffset = CGSizeMake(0.0f, 2.0f);
            card.layer.shadowOpacity = light ? 0.20f : 0.34f;
            card.layer.shadowRadius = 4.0f;
        }
        SenkoApplyShadowPath(card.layer, radius);
    }

    card->state.textColor = kInk;
    card->traffic.textColor = kInk;
    card->state.shadowColor = nil;
    card->state.shadowOffset = CGSizeZero;

    /* the two header controls share one shape */
    UIColor *headerIcon = [UIColor whiteColor];
    StyleHeaderButton(ui->gear, SenkoGearIcon(22.0f, headerIcon), YES);
    StyleHeaderButton(ui->plus, SenkoPlusIcon(22.0f, headerIcon), YES);

/* the classic hero styles its own connect and check from the status pass: a
   glossy capsule and a dome, not the flat pill pair the card wears */
    BOOL classic = SenkoClassicHomeEnabled();
    if (ui->check && !classic) {
/* translucent accent over a light card turns grey instead of taking the
   palette colour. keep this control opaque, like the connect action */
        [ui->check setImage:GaugeIcon(20.0f, [UIColor whiteColor]) forState:UIControlStateNormal];
        UIColor *veilTop = SenkoShadeColor(kAccentBlue, 0.14f);
        UIColor *veilBottom = SenkoShadeColor(kAccentBlueLo, -0.12f);
        CAGradientLayer *fill = PillFill(ui->check);
        fill.colors = [NSArray arrayWithObjects:(id)veilTop.CGColor,
                                                (id)veilBottom.CGColor, nil];
        objc_setAssociatedObject(ui->check, &SenkoStyleTopKey, veilTop,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(ui->check, &SenkoStyleBotKey, veilBottom,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        ui->check.layer.borderWidth = 1.0f;
        ui->check.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.28f].CGColor;
    } else if (ui->check) {
/* the compact classic capsule has room for one clear affordance. its label
   displaced the gauge, leaving the ping action visually blank at small sizes */
        [ui->check setTitle:nil forState:UIControlStateNormal];
        [ui->check setImage:GaugeIcon(20.0f, [UIColor whiteColor])
                    forState:UIControlStateNormal];
    }
    if (ui->connect && !classic) {
        BOOL compactScreen = IsCompact(SenkoViewBounds(ui->card.superview).size.height);
        ui->connect.titleLabel.font = SenkoFontBody(IsPad() ? 18.0f : (compactScreen ? 15.0f : 16.0f), YES);
    }
    if (ui->detail) {
        ui->detail.textAlignment = NSTextAlignmentLeft;
        ui->detail.lineBreakMode = NSLineBreakByTruncatingTail;
    }
}

void SenkoHomeApplyStatus(const SenkoHomeChrome *ui, NSString *state,
                          NSString *title, BOOL animated) {
    if (!ui) return;
    BOOL connecting = [state isEqualToString:@"connecting"];
    BOOL connected = [state isEqualToString:@"connected"];
    BOOL failed = [state isEqualToString:@"error"];

/* the orb carries the failure because the glow behind it already does; the
   connect pill stays neutral, because a button that is red until the next
   success reads as broken rather than as a report of one refused attempt */
    UIColor *orbTint = connected ? kConnOn
                     : connecting ? kAccentBlue
                     : failed ? SenkoErrorTint()
                     : kIdleGrey;
/* idle is an available action, not disabled chrome. using the palette accent
   keeps the connect control clean on white themes */
    UIColor *tint = connected ? kConnOn : kAccentBlue;
    UIColor *tintLo = connected ? kConnOnLo
                    : kAccentBlueLo;

    UILabel *label = ui->card ? ui->card->state : nil;
    if (label && title && ![title isEqualToString:label.text]) {
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

    if (SenkoClassicHomeEnabled()) {
        StripDecor(ui->connect, PillLayerNames());
        StripDecor(ui->check, PillLayerNames());
/* the heart mask belongs to one theme's dome, so a switch between two classic
   themes has to drop it before the next one cuts its own */
        ui->connect.layer.mask = nil;
        ui->check.layer.mask = nil;
/* the dome carried two states and nothing else. an error tint turned it orange,
   which is not a colour the classic hero ever showed: a refused connection just
   leaves it idle and puts the reason in the status pill */
        BOOL active = connected || connecting;
        UIColor *domeTop = active ? kConnOn : kIdleGrey;
        UIColor *domeBottom = active ? kConnOnLo : kIdleGreyLo;

        if (domeTop != gClassicDomeTop) {
            [gClassicDomeTop release];
            gClassicDomeTop = [domeTop retain];
        }
        if (domeBottom != gClassicDomeBottom) {
            [gClassicDomeBottom release];
            gClassicDomeBottom = [domeBottom retain];
        }

        if (ui->connect) {
/* the gradient and its mask are cut from bounds, so the scale the layout put on
   the button has to come off while it is restyled and go back afterwards */
            CGAffineTransform t = ui->connect.transform;
            ui->connect.transform = CGAffineTransformIdentity;
            StyleDomeColors(ui->connect, domeTop, domeBottom);
/* the gloss is a sublayer over the fill, so the label is raised above it */
            [ui->connect bringSubviewToFront:ui->connect.titleLabel];
            ui->connect.transform = t;
            [ui->connect setTitle:(connected ? @"ON" : (connecting ? @"..." : @"OFF"))
                         forState:UIControlStateNormal];
            [ui->connect setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
            ui->connect.titleLabel.font = [UIFont boldSystemFontOfSize:20.0f];
        }
/* the check control is a glossy capsule in this hero, not the flat pill the
   card uses */
        if (ui->check) {
            StyleGlossyCapsule(ui->check, kAccentBlue, kAccentBlueLo);
/* the gloss is a sublayer over the fill, so the icon is raised above it,
   same as the connect label just above */
            [ui->check bringSubviewToFront:ui->check.imageView];
        }
        if (title.length) ui->detail.text = title;
        return;
    }

    StyleOrb(ui->card, orbTint, connecting, connected);
/* back from the classic hero: the dome and the capsule leave their own layers,
   and the check control keeps a title the pill has no room for */
    StripDecor(ui->connect, DomeLayerNames());
    StripDecor(ui->check, DomeLayerNames());
    StripHeroGlow(ui->connect);
    StripHeroGlow(ui->check);
    if (ui->check) {
        [ui->check setTitle:nil forState:UIControlStateNormal];
        ui->check.layer.masksToBounds = YES;
    }

    if (ui->connect) {
        CAGradientLayer *fill = PillFill(ui->connect);
        /* kIdleGrey and kIdleGreyLo are the same colour in several palettes,
           which painted the pill as a flat slab */
        UIColor *pillTop = SenkoShadeColor(tint, 0.20f);
        UIColor *pillBottom = SenkoShadeColor(tintLo, -0.20f);
        fill.colors = [NSArray arrayWithObjects:(id)pillTop.CGColor,
                                                (id)pillBottom.CGColor, nil];
        objc_setAssociatedObject(ui->connect, &SenkoStyleTopKey, pillTop,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(ui->connect, &SenkoStyleBotKey, pillBottom,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
/* a hairline under the fill is what separates a bright pill from a bright
   wallpaper; without it the two bleed into each other */
        ui->connect.layer.borderWidth = SenkoThemeIsFlat() ? 0.0f : 0.5f;
        ui->connect.layer.borderColor = SenkoThemeIsLight()
            ? [UIColor colorWithWhite:0 alpha:0.22f].CGColor
            : [UIColor colorWithWhite:0 alpha:0.38f].CGColor;
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

/* the rebuilt header reads the wordmark from the leading edge with the two
   controls grouped opposite it. the classic header splits them around the
   wordmark */
static void LayoutHeader(const SenkoHomeChrome *ui, BOOL pad, BOOL compact,
                         CGFloat top, CGFloat contentX, CGFloat contentW, CGFloat inset,
                         BOOL classic) {
    CGFloat headerH = pad ? 44.0f : (compact ? 34.0f : 38.0f);
    CGFloat headerY = top + (pad ? 14.0f : 10.0f);
    CGFloat side = pad ? 42.0f : (compact ? 32.0f : 36.0f);
    CGFloat gap = compact ? 6.0f : 8.0f;
    CGFloat controlsY = headerY + (headerH - side) * 0.5f;
    CGFloat leftEdge = contentX + inset;
    CGFloat rightEdge = contentX + contentW - inset;

/* the classic header split the controls around the wordmark, which is the only
   arrangement that leaves the middle of the row free for it. the rebuilt header
   groups all three on the trailing edge and reads from the leading one */
    if (classic) {
        SetFrame(ui->gear, CGRectMake(leftEdge, controlsY, side, side));
        SetFrame(ui->plus, CGRectMake(rightEdge - side, controlsY, side, side));
    } else {
        SetFrame(ui->plus, CGRectMake(rightEdge - side, controlsY, side, side));
        SetFrame(ui->gear, CGRectMake(rightEdge - side * 2.0f - gap, controlsY, side, side));
    }
    /* the radius has to follow the frame: styling runs before the first layout,
       when the bounds are still zero and a computed radius leaves them square */
    ui->plus.layer.cornerRadius = side * 0.5f;
    ui->gear.layer.cornerRadius = side * 0.5f;
    /* the generated glyphs already have their final point size */
    ui->plus.imageEdgeInsets = UIEdgeInsetsZero;
    ui->gear.imageEdgeInsets = UIEdgeInsetsZero;
    LayoutHeaderButton(ui->plus);
    LayoutHeaderButton(ui->gear);

    CGFloat titleY = headerY;
    CGFloat titleX;
    CGFloat titleW;
    if (classic) {
/* the trailing pair is the wider side, so it sets the margin on both edges:
   anything narrower on the leading side would centre the label on the gap
   rather than on the header */
        CGFloat margin = side + gap;
        titleX = leftEdge + margin;
        titleW = rightEdge - margin - titleX;
        ui->title.textAlignment = NSTextAlignmentCenter;
/* the miside wordmark used to be a png sitting lower than the text baseline.
   with the image gone the plain label hangs too low for the rest of the row */
        if (SenkoThemeIsMiside()) titleY -= compact ? 4.0f : 6.0f;
    } else {
        CGFloat controlsWidth = side * 3.0f + gap * 2.0f;
        titleX = leftEdge;
        titleW = contentW - inset * 2.0f - controlsWidth - gap;
        ui->title.textAlignment = NSTextAlignmentLeft;
    }
    if (titleW < 60.0f) titleW = 60.0f;
/* the wordmark is the one thing on this row that cannot shrink, so it shortens
   its own face rather than dropping half of itself into an ellipsis */
    ui->title.adjustsFontSizeToFitWidth = classic;
    if (classic && [ui->title respondsToSelector:@selector(setMinimumScaleFactor:)])
        ui->title.minimumScaleFactor = 0.7f;
    SetFrame(ui->title, CGRectMake(titleX, titleY, titleW, headerH));
}

/* the connect button and both pills are built as the card's subviews, because
   the rebuilt hero draws them inside it. the classic hero places them in root
   coordinates and hides the card, so they have to change parent first or they
   go invisible along with it */
static void MoveHeroControls(const SenkoHomeChrome *ui, UIView *parent) {
    UIView *controls[3];
    size_t i;
    if (!parent) return;
    controls[0] = ui->connect;
    controls[1] = ui->check;
    controls[2] = ui->detail;
    for (i = 0; i < 3; i++)
        if (controls[i] && controls[i].superview != parent) [parent addSubview:controls[i]];
}

void SenkoHomeLayout(UIView *root, const SenkoHomeChrome *ui, CGFloat headerProgress) {
    if (!root || !ui || !ui->table) return;
/* coming back from the classic hero, the card takes its controls and its own
   visibility back */
    MoveHeroControls(ui, ui->card);
    ui->card.hidden = NO;
    /* classic mode scales the same controls in place. clear that transform
       before the card receives its local frames, otherwise connect and ping
       visually share one enlarged hit target after a mode switch */
    if (ui->connect && !CGAffineTransformIsIdentity(ui->connect.transform))
        ui->connect.transform = CGAffineTransformIdentity;
    if (ui->check && !CGAffineTransformIsIdentity(ui->check.transform))
        ui->check.transform = CGAffineTransformIdentity;
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

    LayoutHeader(ui, pad, compact, top, contentX, contentW, inset, NO);

    /* card: two target shapes, then one interpolation. deriving both from the
       same metrics is what keeps the detail line off the button row */
    CGFloat cardX = contentX + inset;
    CGFloat cardW = contentW - inset * 2.0f;
/* the text column starts after the orb, and the orb width comes from the same
   metrics, so the first pass uses the phone sized orb to measure against */
    CardMetrics m = MetricsFor(W, H, 2);
    m = MetricsFor(W, H, DetailLinesFor(ui->detail,
                                        cardW - (m.cardPad + m.orbOpen + 12.0f)
                                              - m.cardPad));
    CGFloat cardY = HeaderBandHeight(H, top);
    CGFloat cardH = Lerp(m.openH, m.shutH, t);
    SetFrame(ui->card, CGRectMake(cardX, cardY, cardW, cardH));
    {
        CGFloat cardRadius = SenkoThemeCardRadius();
        if (cardRadius < 16.0f) cardRadius = 16.0f;
        BOOL ios16Sheen = SenkoThemeIsIos16();
        for (CALayer *layer in ui->card.layer.sublayers) {
            if ([layer.name isEqualToString:@"cardFill"]) {
                SenkoSetLayerFrame(layer, ui->card.bounds);
            } else if ([layer.name isEqualToString:@"cardSheen"]) {
                CGRect sb = CGRectInset(ui->card.bounds, 1.0f, 1.0f);
                SenkoSetLayerFrame(layer, CGRectMake(sb.origin.x, sb.origin.y,
                                                     sb.size.width,
                                                     sb.size.height * (ios16Sheen ? 0.34f : 0.42f)));
            }
        }
        if (ui->card.layer.shadowOpacity > 0.0f)
            SenkoApplyShadowPath(ui->card.layer, cardRadius);
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
        if (SenkoLayerNeedsHeartPath(card->orb.layer.mask, card->orb.bounds))
            card->orb.layer.mask = SenkoHeartMaskLayer(card->orb.bounds);
        card->core.layer.cornerRadius = 0.0f;
        if (SenkoLayerNeedsHeartPath(card->core.layer.mask, card->core.bounds))
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
        if (SenkoLayerNeedsHeartPath(heartRing, card->ring.bounds)) {
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
    /* the compact row reserves its one action for the connect control. the
       checker returns as the card opens instead of becoming a stray circle */
    CGRect checkShutR = CGRectMake(connectShutR.origin.x - m.btnShut - 6.0f,
                                   shutPad, m.btnShut, m.btnShut);
    CGRect checkFrame = LerpRect(checkOpenR, checkShutR, t);
    checkFrame.size.height = btnH;
    SetFrame(ui->check, checkFrame);
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
/* the classic hero gives this line its own plate; inside the card it sits on
   the card's fill and has to give the plate back */
        if (ui->detail.layer.borderWidth != 0.0f) {
            ui->detail.backgroundColor = [UIColor clearColor];
            ui->detail.layer.borderWidth = 0.0f;
            ui->detail.layer.cornerRadius = 0.0f;
            ui->detail.layer.masksToBounds = NO;
        }
        SetFrame(ui->detail, CGRectMake(textXOpen, m.cardPad + m.stateH + 3.0f,
                                        textWOpen, m.detailH));
        /* the second line goes well before the row height could clip it */
        ui->detail.alpha = t > 0.3f ? 0.0f : (1.0f - t / 0.3f);
    }
    SetFrame(card->traffic, CGRectMake(textXOpen, m.cardPad + m.stateH + 3.0f + m.detailH,
                                      textWOpen, 17.0f));
    card->traffic.alpha = t > 0.3f ? 0.0f : (1.0f - t / 0.3f);

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

void SenkoHomeLayoutClassic(UIView *root, const SenkoHomeChrome *ui, CGFloat headerProgress) {
    if (!root || !ui || !ui->table) return;
    CGRect bounds = root.bounds;
    CGFloat W = bounds.size.width;
    CGFloat H = bounds.size.height;
    if (W < 1.0f || H < 1.0f) return;

    {
        BOOL pad = IsPad();
        BOOL compact = IsCompact(H);
        UIEdgeInsets safe = SenkoSafeAreaInsets(root);
        CGFloat top = GetTopOffset();
        CGFloat usableW = W - safe.left - safe.right;
        CGFloat contentW;
        CGFloat contentX;
        if (safe.top > top) top = safe.top;
        if (usableW < 1.0f) usableW = W;
/* the classic table keeps its own, wider cap (SenkoMainTableWidth). the header
   used the narrower hero cap instead, which left the gear/refresh/plus circles
   inset well past the row cards underneath them on iPad */
        contentW = SenkoMainTableWidth(usableW, H);
        contentX = safe.left + (usableW - contentW) / 2.0f;
        LayoutHeader(ui, pad, compact, top, contentX, contentW, SideInset(contentW), YES);
    }

/* the card belongs to the rebuilt hero and has no place above a dome */
    MoveHeroControls(ui, root);
    ui->card.hidden = YES;
    ui->connect.hidden = NO;

    /* the dome takes the tint the connect pill would have carried, so the classic
       hero follows the same state colours as the card it replaces */
    SenkoLayoutMainContent(root, ui->background, ui->table, ui->check, ui->detail,
                           ui->connect, gClassicDomeTop, gClassicDomeBottom,
                           headerProgress);
}

NSString *SenkoFormatBytes(unsigned long long value) {
    double amount = (double)value;
    NSArray *units = SenkoLanguageIsChinese()
        ? [NSArray arrayWithObjects:@"B", @"KB", @"MB", @"GB", @"TB", nil]
        : SenkoLanguageIsRussian()
        ? [NSArray arrayWithObjects:@"Б", @"КБ", @"МБ", @"ГБ", @"ТБ", nil]
        : [NSArray arrayWithObjects:@"B", @"KB", @"MB", @"GB", @"TB", nil];
    NSUInteger unit = 0;
    while (amount >= 1024.0 && unit + 1 < [units count]) {
        amount /= 1024.0;
        unit++;
    }
    NSString *number = [NSString stringWithFormat:(amount >= 10.0 || unit == 0)
        ? @"%.0f" : @"%.1f", amount];
    if (SenkoLanguageIsRussian())
        number = [number stringByReplacingOccurrencesOfString:@"." withString:@","];
    return [NSString stringWithFormat:@"%@ %@", number, [units objectAtIndex:unit]];
}

void SenkoHomeApplyTraffic(const SenkoHomeChrome *ui, BOOL known,
                           uint64_t up, uint64_t down) {
    if (!ui || !ui->card) return;
    UILabel *label = ui->card->traffic;
    label.hidden = !known;
    label.text = known ? [NSString stringWithFormat:@"↑ %@   ↓ %@",
                          SenkoFormatBytes(up), SenkoFormatBytes(down)] : nil;
}
