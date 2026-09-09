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
static void StyleActionButton(UIButton *button) {
    UIColor *fill = kAccentBlue ? kAccentBlue : [UIColor colorWithWhite:0.4 alpha:1];
    [button setTitleColor:SenkoPillLabelColor(fill) forState:UIControlStateNormal];
    button.titleLabel.font = SenkoFontBody(15.0f, YES);
    button.backgroundColor = fill;
    button.layer.cornerRadius = SenkoThemeCardRadius();
    button.layer.masksToBounds = YES;
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

        headline = [[UILabel alloc] initWithFrame:CGRectZero];
        headline.backgroundColor = [UIColor clearColor];
        headline.textAlignment = NSTextAlignmentCenter;
        headline.numberOfLines = 2;
        headline.lineBreakMode = NSLineBreakByWordWrapping;
        [self addSubview:headline];

        body = [[UILabel alloc] initWithFrame:CGRectZero];
        body.backgroundColor = [UIColor clearColor];
        body.textAlignment = NSTextAlignmentCenter;
        body.numberOfLines = 0;
        body.lineBreakMode = NSLineBreakByWordWrapping;
        [self addSubview:body];

        hwidPlate = [[UIView alloc] initWithFrame:CGRectZero];
        hwidPlate.backgroundColor = [UIColor clearColor];
        [self addSubview:hwidPlate];

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
        hwidValue.lineBreakMode = NSLineBreakByTruncatingMiddle;
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

    CGFloat side = 20.0f;
    CGFloat w = b.size.width - side * 2.0f;
    if (w < 80.0f) w = 80.0f;

    CGFloat buttonH = 44.0f;
    CGFloat gap = 10.0f;
    CGFloat bottom = b.size.height - gap;
    CGFloat buttonW = (w - gap) * 0.5f;
    pasteButton.frame = CGRectMake(side, bottom - buttonH, buttonW, buttonH);
    scanButton.frame = CGRectMake(side + buttonW + gap, bottom - buttonH,
                                  buttonW, buttonH);

    CGFloat headH = SenkoTextSize(headline.text, headline.font, w).height;
    if (headH < 24.0f) headH = 24.0f;
    CGFloat bodyH = SenkoTextSize(body.text, body.font, w).height;
    if (bodyH < 18.0f) bodyH = 18.0f;
    CGFloat plateH = 58.0f;
    CGFloat blockH = headH + 8.0f + bodyH + 18.0f + plateH;

    CGFloat available = bottom - buttonH - gap * 2.0f;
    CGFloat y = (available - blockH) * 0.5f;
    if (y < gap) y = gap;

    headline.frame = CGRectMake(side, y, w, headH);
    y += headH + 8.0f;
    body.frame = CGRectMake(side, y, w, bodyH);
    y += bodyH + 18.0f;

    CGFloat plateW = w;
    if (plateW > 320.0f) plateW = 320.0f;
    hwidPlate.frame = CGRectMake((b.size.width - plateW) * 0.5f, y, plateW, plateH);
    hwidCaption.frame = CGRectMake(8.0f, 8.0f, plateW - 16.0f, 14.0f);
    hwidValue.frame = CGRectMake(8.0f, 26.0f, plateW - 16.0f, 24.0f);
    hwidTap.frame = hwidPlate.bounds;
}

@end
