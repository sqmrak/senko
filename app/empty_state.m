#import "empty_state.h"

#import "app_common.h"
#import "ui_theme.h"
#import "home_layout.h"

@implementation SenkoEmptyStateView {
    NSString *_hwid;
    BOOL      _showingCopied;
}

/* two buttons share the row, so each gets half a 320pt screen. uikit truncates
   a button title in the middle by default, which turned a russian caption into
   "Встави...буфера"; the face shrinks instead, and a title that still does not
   fit loses its tail rather than its middle */
/* the accent is carried as a gradient, the same way the connect pill above
   these buttons carries it, so the bottom row reads as part of the same family
   instead of a pair of flat slabs */
static CAGradientLayer *ActionFill(UIButton *button) {
    for (CALayer *layer in button.layer.sublayers) {
        if ([layer.name isEqualToString:@"actionFill"] &&
            [layer isKindOfClass:[CAGradientLayer class]])
            return (CAGradientLayer *)layer;
    }
    CAGradientLayer *fill = [CAGradientLayer layer];
    fill.name = @"actionFill";
/* the layer is repositioned on every layout pass, and an implicit animation on
   each of those turns a rotation into a visible slide */
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

static void SyncActionFill(UIButton *button) {
    if (!button) return;
    CGFloat radius = SenkoThemeCardRadius();
    button.layer.cornerRadius = radius;
    CAGradientLayer *fill = ActionFill(button);
    SenkoSetLayerFrame(fill, button.bounds);
    fill.cornerRadius = radius;
    UIColor *base = kAccentBlue ? kAccentBlue : [UIColor colorWithWhite:0.4 alpha:1];
    SenkoApplyRelief(button, fill, SenkoShadeColor(base, 0.20f),
                     SenkoShadeColor(base, -0.20f), radius);
    button.layer.borderWidth = SenkoThemeIsFlat() ? 0.0f : 0.5f;
    button.layer.borderColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.22f].CGColor
        : [UIColor colorWithWhite:0 alpha:0.38f].CGColor;
}

static void StyleActionButton(UIButton *button) {
    UIColor *fill = kAccentBlue ? kAccentBlue : [UIColor colorWithWhite:0.4 alpha:1];
    [button setTitleColor:SenkoPillLabelColor(fill) forState:UIControlStateNormal];
    button.titleLabel.font = SenkoFontBody(15.0f, YES);
/* the gradient layer is the fill now, so a background colour underneath it
   would only show through the corner radius */
    button.backgroundColor = [UIColor clearColor];
    CAGradientLayer *grad = ActionFill(button);
    grad.colors = [NSArray arrayWithObjects:
                   (id)SenkoShadeColor(fill, 0.20f).CGColor,
                   (id)SenkoShadeColor(fill, -0.20f).CGColor, nil];
    button.layer.cornerRadius = SenkoThemeCardRadius();
/* the relief hangs a shadow outside the bounds, so the fill and the sheen carry
   their own corner radius instead of being clipped by the button */
    button.layer.masksToBounds = NO;
    button.titleLabel.shadowColor = nil;
    button.titleLabel.shadowOffset = CGSizeZero;
    button.titleLabel.numberOfLines = 1;
    button.titleLabel.textAlignment = NSTextAlignmentCenter;
    button.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    button.titleLabel.adjustsFontSizeToFitWidth = YES;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    button.titleLabel.minimumFontSize = 11.0f;
#pragma clang diagnostic pop
    if ([button.titleLabel respondsToSelector:@selector(setMinimumScaleFactor:)])
        button.titleLabel.minimumScaleFactor = 0.72f;
    button.contentEdgeInsets = UIEdgeInsetsMake(0.0f, 8.0f, 0.0f, 8.0f);
}

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.backgroundColor = [UIColor clearColor];
        self.opaque = NO;

        scroll = [[UIScrollView alloc] initWithFrame:CGRectZero];
        scroll.backgroundColor = [UIColor clearColor];
        scroll.showsHorizontalScrollIndicator = NO;
        scroll.showsVerticalScrollIndicator = YES;
        /* the open card leaves too little travel for the empty panel to feel
           scrollable on a 3.5 inch screen, so it keeps one deliberate drag of
           travel even when the text happens to fit exactly */
        scroll.alwaysBounceVertical = YES;
        [self addSubview:scroll];

        textPlate = [[UIView alloc] initWithFrame:CGRectZero];
        [scroll addSubview:textPlate];

        headline = [[UILabel alloc] initWithFrame:CGRectZero];
        headline.backgroundColor = [UIColor clearColor];
        headline.textAlignment = NSTextAlignmentCenter;
        headline.numberOfLines = 2;
        headline.lineBreakMode = NSLineBreakByWordWrapping;
        [textPlate addSubview:headline];

        body = [[UILabel alloc] initWithFrame:CGRectZero];
        body.backgroundColor = [UIColor clearColor];
        body.textAlignment = NSTextAlignmentCenter;
        body.numberOfLines = 0;
        body.lineBreakMode = NSLineBreakByWordWrapping;
        [textPlate addSubview:body];

        hwidPlate = [[UIView alloc] initWithFrame:CGRectZero];
        hwidPlate.backgroundColor = [UIColor clearColor];
        [scroll addSubview:hwidPlate];

        hwidCaption = [[UILabel alloc] initWithFrame:CGRectZero];
        hwidCaption.backgroundColor = [UIColor clearColor];
        hwidCaption.textAlignment = NSTextAlignmentCenter;
        [hwidPlate addSubview:hwidCaption];

        hwidValue = [[UILabel alloc] initWithFrame:CGRectZero];
        hwidValue.backgroundColor = [UIColor clearColor];
        hwidValue.textAlignment = NSTextAlignmentCenter;
        hwidValue.adjustsFontSizeToFitWidth = YES;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        hwidValue.minimumFontSize = 8.0f;
#pragma clang diagnostic pop
        hwidValue.lineBreakMode = NSLineBreakByTruncatingTail;
        [hwidPlate addSubview:hwidValue];

        hwidTap = [UIButton buttonWithType:UIButtonTypeCustom];
        [hwidPlate addSubview:hwidTap];

        pasteButton = [UIButton buttonWithType:UIButtonTypeCustom];
        [self addSubview:pasteButton];

        scanButton = [UIButton buttonWithType:UIButtonTypeCustom];
        [self addSubview:scanButton];

        [self applyTheme];
        [self setHWID:nil];
    }
    return self;
}

- (void)dealloc {
    [headline release];
    [body release];
    [hwidCaption release];
    [hwidValue release];
    [hwidPlate release];
    [textPlate release];
    [scroll release];
    [_hwid release];
    [super dealloc];
}

- (void)applyTheme {
    headline.font = SenkoFontBody(19.0f, YES);
    SenkoStyleInkLabel(headline);
    headline.textAlignment = NSTextAlignmentCenter;
    headline.text = SenkoLocalizedText(@"No servers yet");

    body.font = SenkoFontBody(14.0f, NO);
    SenkoStyleMutedLabel(body);
    body.textAlignment = NSTextAlignmentCenter;
    body.text = SenkoLocalizedText(@"To use a server add a proxy link or a subscription.");

    hwidCaption.font = SenkoFontBody(11.0f, NO);
    SenkoStyleMutedLabel(hwidCaption);
    hwidCaption.textAlignment = NSTextAlignmentCenter;
    hwidCaption.text = _showingCopied
        ? SenkoLocalizedText(@"Device ID copied")
        : SenkoLocalizedText(@"Device ID (tap to copy)");

    hwidValue.font = [UIFont fontWithName:@"Courier" size:13.0f];
    if (!hwidValue.font) hwidValue.font = SenkoFontBody(13.0f, NO);
    SenkoStyleInkLabel(hwidValue);
    hwidValue.textAlignment = NSTextAlignmentCenter;

    SenkoStyleSectionPlate(hwidPlate);
/* a plain caption over a patterned wallpaper is unreadable, so the block gets
   the same plate the device id row already sits on, at a lighter weight */
    textPlate.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:1 alpha:0.62f]
        : [UIColor colorWithWhite:0 alpha:0.38f];
    textPlate.layer.cornerRadius = SenkoThemeCardRadius();
    textPlate.layer.borderWidth = 0.5f;
    textPlate.layer.borderColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14f].CGColor
        : [UIColor colorWithWhite:1 alpha:0.16f].CGColor;

    /* the sentence above already says what to paste, so the button carries the
       verb alone and fits without shrinking */
    [pasteButton setTitle:SenkoLocalizedText(@"Paste") forState:UIControlStateNormal];
    [scanButton setTitle:SenkoLocalizedText(@"QR code") forState:UIControlStateNormal];
    StyleActionButton(pasteButton);
    StyleActionButton(scanButton);
    [self setNeedsLayout];
}

- (void)setHWID:(NSString *)hwid {
    NSString *copied = [hwid copy];
    [_hwid release];
    _hwid = copied;
    hwidValue.text = [_hwid length] ? _hwid : SenkoLocalizedText(@"not available yet");
    hwidTap.enabled = [_hwid length] > 0;
    [self setNeedsLayout];
}

- (void)restoreCopiedNotice {
    _showingCopied = NO;
    hwidCaption.text = SenkoLocalizedText(@"Device ID (tap to copy)");
}

- (void)flashCopiedNotice {
    _showingCopied = YES;
    hwidCaption.text = SenkoLocalizedText(@"Device ID copied");
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(restoreCopiedNotice)
                                               object:nil];
    [self performSelector:@selector(restoreCopiedNotice) withObject:nil afterDelay:1.6];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    if (b.size.width < 2.0f || b.size.height < 2.0f) return;

    CGFloat side = SENKO_LIST_PLATE_INSET;
    CGFloat w = b.size.width - side * 2.0f;
    if (w < 80.0f) w = 80.0f;

    CGFloat buttonH = 44.0f;
    CGFloat gap = 10.0f;
    CGFloat bottom = b.size.height - gap;
/* a pair of 44pt buttons stretched across an ipad reads as two slabs rather
   than two buttons, so the row keeps a sane width and centres itself */
    CGFloat rowW = w;
    CGFloat rowX = side;
    CGFloat buttonW = (rowW - gap) * 0.5f;
    pasteButton.frame = CGRectMake(rowX, bottom - buttonH, buttonW, buttonH);
    scanButton.frame = CGRectMake(rowX + buttonW + gap, bottom - buttonH,
                                  buttonW, buttonH);
/* a sublayer does not follow its view, so the fill has to be recut here or it
   keeps the width the button had before the rotation */
    SyncActionFill(pasteButton);
    SyncActionFill(scanButton);

/* everything above the button row scrolls, because on a 3.5 inch screen the
   block does not fit under the status card and used to be drawn over */
    CGFloat scrollH = bottom - buttonH - gap * 2.0f;
    if (scrollH < 40.0f) scrollH = 40.0f;
    scroll.frame = CGRectMake(0.0f, 0.0f, b.size.width, scrollH);

    CGFloat textPad = 12.0f;
    CGFloat textW = w - textPad * 2.0f;
    if (textW < 60.0f) textW = 60.0f;
    CGFloat headH = SenkoTextSize(headline.text, headline.font, textW).height;
    if (headH < 24.0f) headH = 24.0f;
    CGFloat bodyH = SenkoTextSize(body.text, body.font, textW).height;
    if (bodyH < 18.0f) bodyH = 18.0f;
    CGFloat plateH = 58.0f;
    CGFloat textPlateH = textPad + headH + 6.0f + bodyH + textPad;
    CGFloat blockH = textPlateH + 16.0f + plateH;

    CGFloat y = (scrollH - blockH) * 0.5f;
    if (y < gap) y = gap;

    textPlate.frame = CGRectMake(side, y, w, textPlateH);
    headline.frame = CGRectMake(textPad, textPad, textW, headH);
    body.frame = CGRectMake(textPad, textPad + headH + 6.0f, textW, bodyH);
    y += textPlateH + 16.0f;

    CGFloat plateW = w;
    hwidPlate.frame = CGRectMake(side, y, plateW, plateH);
    hwidCaption.frame = CGRectMake(8.0f, 8.0f, plateW - 16.0f, 14.0f);
    hwidValue.frame = CGRectMake(8.0f, 26.0f, plateW - 16.0f, 24.0f);
    hwidTap.frame = hwidPlate.bounds;

    CGFloat contentH = y + plateH + gap;
    if (contentH < scrollH + 44.0f) contentH = scrollH + 44.0f;
    if (!CGSizeEqualToSize(scroll.contentSize,
                           CGSizeMake(b.size.width, contentH)))
        scroll.contentSize = CGSizeMake(b.size.width, contentH);
}

@end
