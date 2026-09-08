#import "server_sheet.h"
#import "ui_theme.h"
#import "app_common.h"
#import "server_cell.h"
#import "home_layout.h"

#include <math.h>

NSString * const SenkoServerSheetActionConnect = @"connect";
NSString * const SenkoServerSheetActionPing    = @"ping";
NSString * const SenkoServerSheetActionCopy    = @"copy";
NSString * const SenkoServerSheetActionEdit    = @"edit";
NSString * const SenkoServerSheetActionDelete  = @"delete";

enum { kRowHeight = 30 };

static CAGradientLayer *ServerPrimaryFill(UIButton *button) {
    for (CALayer *layer in button.layer.sublayers) {
        if ([layer.name isEqualToString:@"serverPrimaryFill"] &&
            [layer isKindOfClass:[CAGradientLayer class]])
            return (CAGradientLayer *)layer;
    }
    CAGradientLayer *fill = [CAGradientLayer layer];
    fill.name = @"serverPrimaryFill";
    fill.startPoint = CGPointMake(0.0f, 0.5f);
    fill.endPoint = CGPointMake(1.0f, 0.5f);
    fill.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                    [NSNull null], @"colors",
                    [NSNull null], @"bounds",
                    [NSNull null], @"position",
                    [NSNull null], @"cornerRadius", nil];
    [button.layer insertSublayer:fill atIndex:0];
    return fill;
}

@implementation SenkoServerSheet

- (void)dealloc {
    [_server release];
    [_link release];
    [_ping release];
    [_source release];
    [_actionButtons release];
    [super dealloc];
}

- (UILabel *)makeLabel:(CGFloat)size bold:(BOOL)bold muted:(BOOL)muted {
    UILabel *l = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    l.backgroundColor = [UIColor clearColor];
    l.font = SenkoFontBody(size, bold);
    if (muted) SenkoStyleMutedLabel(l); else SenkoStyleInkLabel(l);
    return l;
}

/* the glyph sits above its caption. uikit only stacks image and title through
   edge insets that have to be recomputed for every width, so the two pieces are
   plain subviews laid out in layoutSubviews instead */
- (UIButton *)makeActionButton:(UIImage *)icon
                          title:(NSString *)title
                            sel:(SEL)sel
                    destructive:(BOOL)destructive {
    UIButton *b = [UIButton buttonWithType:UIButtonTypeCustom];
    UIColor *tint = destructive
        ? [UIColor colorWithRed:0.87f green:0.31f blue:0.29f alpha:1.0f]
        : kAccentBlue;
    UIImageView *glyph = [[[UIImageView alloc] initWithImage:icon] autorelease];
    glyph.tag = 901;
    glyph.contentMode = UIViewContentModeScaleAspectFit;
    glyph.userInteractionEnabled = NO;
    [b addSubview:glyph];
    UILabel *caption = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    caption.tag = 902;
    caption.text = title;
    caption.textAlignment = NSTextAlignmentCenter;
    caption.backgroundColor = [UIColor clearColor];
    caption.textColor = tint;
    caption.font = SenkoFontBody(11.0f, NO);
    caption.userInteractionEnabled = NO;
    [b addSubview:caption];
    b.backgroundColor = [tint colorWithAlphaComponent:0.10f];
    b.layer.borderWidth = 1.0f;
    b.layer.borderColor = [tint colorWithAlphaComponent:0.24f].CGColor;
    b.layer.masksToBounds = YES;
    [b addTarget:self action:sel forControlEvents:UIControlEventTouchUpInside];
    [b addTarget:self action:@selector(buttonDown:) forControlEvents:UIControlEventTouchDown];
    [b addTarget:self action:@selector(buttonUp:)
        forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                         UIControlEventTouchCancel];
    return b;
}

- (void)addRowTitle:(NSString *)title value:(NSString *)value store:(UILabel **)store {
    UILabel *key = [self makeLabel:13.0f bold:NO muted:YES];
    key.text = SenkoLocalizedText(title);
    [_rows addSubview:key];

    UILabel *val = [self makeLabel:13.0f bold:YES muted:NO];
    val.text = [value length] ? value : @"—";
    val.textAlignment = NSTextAlignmentRight;
    val.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [_rows addSubview:val];
    if (store) *store = val;
}

- (id)initWithServer:(SenkoServer *)server
                ping:(NSNumber *)ping
              source:(NSString *)source
              active:(BOOL)active
           canMutate:(BOOL)canMutate
            delegate:(id<SenkoServerSheetDelegate>)delegate {
    if (!(self = [super initWithFrame:CGRectZero])) return nil;
    if (!server) { [self release]; return nil; }
    _server = [server retain];
    _ping = [ping retain];
    _source = [source copy];
    _active = active;
    _canMutate = canMutate;
    _delegate = delegate;
    self.backgroundColor = [UIColor clearColor];

    _backdrop = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    _backdrop.backgroundColor = [UIColor colorWithWhite:0.0f alpha:0.45f];
    _backdrop.alpha = 0.0f;
    [self addSubview:_backdrop];
    UITapGestureRecognizer *tap = [[[UITapGestureRecognizer alloc]
                                    initWithTarget:self action:@selector(dismiss)] autorelease];
    [_backdrop addGestureRecognizer:tap];

    _card = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    _card.layer.masksToBounds = YES;
    [self addSubview:_card];
    UIPanGestureRecognizer *pan = [[[UIPanGestureRecognizer alloc]
                                    initWithTarget:self action:@selector(panned:)] autorelease];
    [_card addGestureRecognizer:pan];

    _grabber = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    _grabber.layer.cornerRadius = 2.5f;
    [_card addSubview:_grabber];

    _close = [UIButton buttonWithType:UIButtonTypeCustom];
    _close.tag = 903;
    [_close setImage:SenkoIconClose(16.0f, kInkMuted) forState:UIControlStateNormal];
    [_close addTarget:self action:@selector(dismiss)
     forControlEvents:UIControlEventTouchUpInside];
    [_card addSubview:_close];

    _badge = [[[UIImageView alloc] initWithFrame:CGRectZero] autorelease];
    _badge.contentMode = UIViewContentModeScaleAspectFit;
    NSString *code = SenkoServerFlagCode(server->remark);
    UIImage *flag = [code length]
        ? [UIImage imageNamed:[NSString stringWithFormat:@"flag-%@.png", code]]
        : nil;
    /* a drawn globe stands in for the missing flag, so a server without a
       country marker still gets the same badge geometry */
    _badge.image = flag ? flag : SenkoIconGlobe(30.0f, kAccentBlue);
    [_card addSubview:_badge];

    _title = [self makeLabel:19.0f bold:YES muted:NO];
    NSString *plain = SenkoServerDisplayName(server->remark);
    _title.text = [plain length] ? plain : (server->host ? server->host : @"server");
    [_card addSubview:_title];

    _subtitle = [self makeLabel:13.0f bold:NO muted:YES];
    _subtitle.text = [NSString stringWithFormat:@"%@ · %@ · %@",
                      server->proto ? server->proto : @"?",
                      [server->net length] ? server->net : @"tcp",
                      [server->security length] ? server->security : @"none"];
    [_card addSubview:_subtitle];

    _rows = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    [_card addSubview:_rows];

    [self addRowTitle:@"Host"
                value:[NSString stringWithFormat:@"%@:%d",
                       server->host ? server->host : @"", server->port]
                store:NULL];
    [self addRowTitle:@"Protocol" value:server->proto store:NULL];
    [self addRowTitle:@"Transport" value:server->net store:NULL];
    [self addRowTitle:@"Security" value:server->security store:NULL];
    [self addRowTitle:@"Latency"
                value:ping ? [NSString stringWithFormat:@"%d ms", [ping intValue]] : nil
                store:&_pingValue];
    [self addRowTitle:@"Source" value:source store:NULL];
    [self addRowTitle:@"Link" value:SenkoLocalizedText(@"loading") store:&_linkValue];
    if (!server->supported) {
        UILabel *warn = [self makeLabel:12.0f bold:YES muted:NO];
        warn.tag = 771;
        warn.numberOfLines = 2;
        warn.textColor = [UIColor colorWithRed:0.87f green:0.31f blue:0.29f alpha:1.0f];
        warn.text = SenkoLocalizedText(@"This build cannot dial this profile");
        [_card addSubview:warn];
    }

    _primary = [UIButton buttonWithType:UIButtonTypeCustom];
    NSString *primaryText = SenkoLocalizedText(active ? @"Disconnect" : @"Connect");
    _primary.accessibilityLabel = primaryText;
    UIColor *primaryTop = active ? kIdleGrey : kConnOn;
    UIColor *primaryBottom = active ? kIdleGreyLo : kConnOnLo;
    _primary.backgroundColor = [UIColor clearColor];
    CAGradientLayer *primaryFill = ServerPrimaryFill(_primary);
    primaryFill.colors = [NSArray arrayWithObjects:(id)primaryTop.CGColor,
                          (id)primaryBottom.CGColor, nil];
    UIColor *primaryInk = SenkoPillLabelColor(primaryTop);
    _primary.layer.masksToBounds = YES;

    /* the label owns the full button width, so its centre is the pill centre.
       UIButton otherwise centres the icon and title as one run and shifts the
       word to the right by half the icon width */
    _primaryTitle = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    _primaryTitle.backgroundColor = [UIColor clearColor];
    _primaryTitle.font = SenkoFontBody(17.0f, YES);
    _primaryTitle.text = primaryText;
    _primaryTitle.textColor = primaryInk;
    _primaryTitle.textAlignment = NSTextAlignmentCenter;
    _primaryTitle.userInteractionEnabled = NO;
    [_primary addSubview:_primaryTitle];

    _primaryGlyph = [[[UIImageView alloc]
        initWithImage:SenkoIconPower(19.0f, primaryInk)] autorelease];
    _primaryGlyph.contentMode = UIViewContentModeScaleAspectFit;
    _primaryGlyph.userInteractionEnabled = NO;
    [_primary addSubview:_primaryGlyph];
    _primary.enabled = server->supported || active;
    if (!_primary.enabled) _primary.alpha = 0.45f;
    [_primary addTarget:self action:@selector(primaryTapped)
       forControlEvents:UIControlEventTouchUpInside];
    [_primary addTarget:self action:@selector(buttonDown:) forControlEvents:UIControlEventTouchDown];
    [_primary addTarget:self action:@selector(buttonUp:)
       forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                        UIControlEventTouchCancel];
    [_card addSubview:_primary];

    NSMutableArray *actions = [NSMutableArray array];
    [actions addObject:[self makeActionButton:GaugeIcon(20.0f, kAccentBlue)
                                        title:SenkoLocalizedText(@"Ping")
                                          sel:@selector(pingTapped)
                                  destructive:NO]];
    [actions addObject:[self makeActionButton:SenkoIconCopy(20.0f, kAccentBlue)
                                        title:SenkoLocalizedText(@"Copy")
                                          sel:@selector(copyTapped)
                                  destructive:NO]];
    if (canMutate) {
        [actions addObject:[self makeActionButton:SenkoIconPencil(20.0f, kAccentBlue)
                                            title:SenkoLocalizedText(@"Edit")
                                              sel:@selector(editTapped)
                                      destructive:NO]];
        UIColor *red = [UIColor colorWithRed:0.87f green:0.31f blue:0.29f alpha:1.0f];
        [actions addObject:[self makeActionButton:SenkoIconTrash(20.0f, red)
                                            title:SenkoLocalizedText(@"Remove")
                                              sel:@selector(deleteTapped)
                                      destructive:YES]];
    }
    for (UIButton *b in actions) [_card addSubview:b];
    _actionButtons = [actions copy];

    [self applyTheme];
    return self;
}

- (void)applyTheme {
    CGFloat radius = SenkoThemeCardRadius();
    if (radius < 18.0f) radius = 18.0f;
    _card.layer.cornerRadius = radius;
    if (SenkoThemeIsIos26() || SenkoThemeUsesFrost()) {
        SenkoInstallFrostLite(_card);
        _card.backgroundColor = SenkoThemeIsLight()
            ? [UIColor colorWithWhite:1.0f alpha:0.82f]
            : [UIColor colorWithWhite:0.10f alpha:0.86f];
    } else {
        _card.backgroundColor = SenkoThemeIsLight()
            ? [UIColor colorWithWhite:0.99f alpha:1.0f]
            : [UIColor colorWithWhite:0.11f alpha:1.0f];
    }
    _card.layer.borderWidth = 1.0f;
    _card.layer.borderColor = (SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0.0f alpha:0.08f]
        : [UIColor colorWithWhite:1.0f alpha:0.14f]).CGColor;
    _grabber.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0.0f alpha:0.18f]
        : [UIColor colorWithWhite:1.0f alpha:0.26f];
}

- (void)setLink:(NSString *)link {
    [_link release];
    _link = [link copy];
    _linkValue.text = [link length] ? link : SenkoLocalizedText(@"unavailable");
}

- (void)setPingResult:(NSNumber *)ms {
    [_ping release];
    _ping = [ms retain];
    _pingValue.text = ms && [ms intValue] >= 0
        ? [NSString stringWithFormat:@"%d ms", [ms intValue]]
        : SenkoLocalizedText(@"unreachable");
}

- (CGFloat)cardHeightForWidth:(CGFloat)width {
    (void)width;
    NSUInteger rowCount = [_rows.subviews count] / 2;
    CGFloat height = 16.0f          /* grabber band */
                   + 46.0f          /* header */
                   + 14.0f
                   + rowCount * (CGFloat)kRowHeight
                   + 16.0f
                   + 48.0f          /* primary pill */
                   + 12.0f
                   + 60.0f          /* action row */
                   + 16.0f;
    if (!_server->supported) height += 32.0f;
    return height;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    if (b.size.width < 1.0f) return;
    _backdrop.frame = b;

    UIEdgeInsets safe = SenkoSafeAreaInsets(self);
    CGFloat side = 16.0f;
    CGFloat maxW = 520.0f;
    CGFloat cardW = b.size.width - safe.left - safe.right - side * 2.0f;
    if (cardW > maxW) cardW = maxW;
    CGFloat cardH = [self cardHeightForWidth:cardW] + safe.bottom;
    CGFloat cardX = (b.size.width - cardW) * 0.5f;
    CGFloat cardY = b.size.height - cardH - side;
    /* a tall sheet on a 480pt screen would otherwise start above the header;
       clamping the top keeps the whole card reachable */
    if (cardY < safe.top + 8.0f) {
        cardY = safe.top + 8.0f;
        cardH = b.size.height - cardY - side;
    }
    CGAffineTransform t = _card.transform;
    _card.transform = CGAffineTransformIdentity;
    _card.frame = CGRectMake(cardX, cardY, cardW, cardH);
    _card.transform = t;

    CGFloat pad = 18.0f;
    CGFloat innerW = cardW - pad * 2.0f;
    _grabber.frame = CGRectMake((cardW - 38.0f) * 0.5f, 7.0f, 38.0f, 5.0f);
    _close.frame = CGRectMake(cardW - 44.0f, 12.0f, 32.0f, 32.0f);
    _close.layer.cornerRadius = 16.0f;

    CGFloat y = 20.0f;
    _badge.frame = CGRectMake(pad, y + 2.0f, 34.0f, 34.0f);
    CGFloat headTextX = pad + 34.0f + 12.0f;
    CGFloat headTextW = cardW - headTextX - 48.0f;
    if (headTextW < 40.0f) headTextW = 40.0f;
    _title.frame = CGRectMake(headTextX, y, headTextW, 22.0f);
    _subtitle.frame = CGRectMake(headTextX, y + 22.0f, headTextW, 18.0f);
    y += 46.0f + 14.0f;

    NSUInteger rowCount = [_rows.subviews count] / 2;
    _rows.frame = CGRectMake(pad, y, innerW, rowCount * (CGFloat)kRowHeight);
    for (NSUInteger i = 0; i < rowCount; ++i) {
        UIView *key = [_rows.subviews objectAtIndex:i * 2];
        UIView *val = [_rows.subviews objectAtIndex:i * 2 + 1];
        CGFloat rowY = i * (CGFloat)kRowHeight;
        CGFloat keyW = innerW * 0.38f;
        key.frame = CGRectMake(0, rowY, keyW, (CGFloat)kRowHeight);
        val.frame = CGRectMake(keyW + 8.0f, rowY, innerW - keyW - 8.0f, (CGFloat)kRowHeight);
    }
    y += rowCount * (CGFloat)kRowHeight;

    UIView *warn = [_card viewWithTag:771];
    if (warn) {
        warn.frame = CGRectMake(pad, y, innerW, 30.0f);
        y += 32.0f;
    }
    y += 16.0f;

    _primary.frame = CGRectMake(pad, y, innerW, 48.0f);
    _primary.layer.cornerRadius = 24.0f;
    CAGradientLayer *primaryFill = ServerPrimaryFill(_primary);
    SenkoSetLayerFrame(primaryFill, _primary.bounds);
    primaryFill.cornerRadius = 24.0f;
    _primaryTitle.frame = _primary.bounds;
    CGFloat primaryTextW = ceilf(SenkoTextWidth(_primaryTitle.text, _primaryTitle.font));
    CGFloat primaryGlyphSide = 19.0f;
    CGFloat primaryGlyphX = floorf(CGRectGetMidX(_primary.bounds) -
                                   primaryTextW * 0.5f - 8.0f - primaryGlyphSide);
    if (primaryGlyphX < 12.0f) primaryGlyphX = 12.0f;
    _primaryGlyph.frame = CGRectMake(primaryGlyphX,
        floorf((_primary.bounds.size.height - primaryGlyphSide) * 0.5f),
        primaryGlyphSide, primaryGlyphSide);
    y += 48.0f + 12.0f;

    NSUInteger count = [_actionButtons count];
    if (count) {
        CGFloat gap = 8.0f;
        CGFloat each = (innerW - gap * (count - 1)) / count;
        for (NSUInteger i = 0; i < count; ++i) {
            UIButton *b = [_actionButtons objectAtIndex:i];
            b.frame = CGRectMake(pad + i * (each + gap), y, each, 56.0f);
            b.layer.cornerRadius = 14.0f;
            UIView *glyph = [b viewWithTag:901];
            UIView *caption = [b viewWithTag:902];
            glyph.frame = CGRectMake((each - 20.0f) * 0.5f, 12.0f, 20.0f, 20.0f);
            caption.frame = CGRectMake(2.0f, 34.0f, each - 4.0f, 14.0f);
        }
    }
}

- (void)presentInView:(UIView *)host {
    if (!host) return;
    self.frame = host.bounds;
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [host addSubview:self];
    [self layoutIfNeeded];
    CGFloat travel = self.bounds.size.height - _card.frame.origin.y;
    _card.transform = CGAffineTransformMakeTranslation(0, travel);
    SenkoAnimate(0.20, ^{ _backdrop.alpha = 1.0f; }, NULL);
    SenkoAnimateSpring(0.42, 0, ^{
        _card.transform = CGAffineTransformIdentity;
    }, NULL);
}

- (void)dismiss {
    if (_dismissing) return;
    _dismissing = YES;
    CGFloat travel = self.bounds.size.height - _card.frame.origin.y;
    SenkoAnimate(0.24, ^{
        _backdrop.alpha = 0.0f;
        _card.transform = CGAffineTransformMakeTranslation(0, travel);
    }, ^(BOOL done) {
        (void)done;
        [self removeFromSuperview];
    });
}

- (void)panned:(UIPanGestureRecognizer *)pan {
    CGFloat dy = [pan translationInView:self].y;
    if (pan.state == UIGestureRecognizerStateBegan) {
        _dragStart = _card.transform.ty;
    } else if (pan.state == UIGestureRecognizerStateChanged) {
        CGFloat offset = _dragStart + dy;
        /* dragging up is resisted rather than blocked, so the sheet still
           answers the finger instead of feeling stuck */
        if (offset < 0) offset *= 0.25f;
        _card.transform = CGAffineTransformMakeTranslation(0, offset);
        CGFloat travel = self.bounds.size.height - _card.frame.origin.y;
        CGFloat fade = travel > 1.0f ? 1.0f - (offset / travel) : 1.0f;
        if (fade < 0.0f) fade = 0.0f;
        if (fade > 1.0f) fade = 1.0f;
        _backdrop.alpha = fade;
    } else if (pan.state == UIGestureRecognizerStateEnded ||
               pan.state == UIGestureRecognizerStateCancelled) {
        CGFloat offset = _card.transform.ty;
        CGFloat velocity = [pan velocityInView:self].y;
        if (offset > 90.0f || velocity > 700.0f) {
            [self dismiss];
            return;
        }
        SenkoAnimateSpring(0.34, 0, ^{
            _card.transform = CGAffineTransformIdentity;
            _backdrop.alpha = 1.0f;
        }, NULL);
    }
}

- (void)buttonDown:(UIButton *)b { SenkoPressPop(b, YES); }
- (void)buttonUp:(UIButton *)b { SenkoPressPop(b, NO); }

- (void)report:(NSString *)action {
    if ([_delegate respondsToSelector:@selector(serverSheet:didChooseAction:serverIndex:)])
        [_delegate serverSheet:self didChooseAction:action serverIndex:_server->index];
}

- (void)primaryTapped {
    [self report:SenkoServerSheetActionConnect];
    [self dismiss];
}

- (void)pingTapped {
    _pingValue.text = SenkoLocalizedText(@"checking");
    [self report:SenkoServerSheetActionPing];
}

- (void)copyTapped {
    if ([_link length]) {
        [[UIPasteboard generalPasteboard] setString:_link];
        _linkValue.text = SenkoLocalizedText(@"copied");
    }
    [self report:SenkoServerSheetActionCopy];
}

- (void)editTapped {
    [self report:SenkoServerSheetActionEdit];
    [self dismiss];
}

- (void)deleteTapped {
    [self report:SenkoServerSheetActionDelete];
    [self dismiss];
}

@end
