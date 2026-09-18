#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/message.h>
#import "ui_theme.h"
#import "app_common.h"
#import "crash_report.h"

@implementation AboutVC {

    UIScrollView *_scroll;
    UIView *_card;
    UIView *_info;
    UILabel *_bodyLbl;
    UIButton *_sponsorButton;
    UILabel  *_sponsorLink;
    UIButton *_sponsorCopy;
    UILabel  *_thanksLbl;
    UIButton *_githubButton;
    UIButton *_telegramButton;
    CAGradientLayer *_cardGrad;
    CAGradientLayer *_infoGrad;

}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"About");
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    SenkoApplyScreenChrome(self.view);

    _scroll = [[UIScrollView alloc] initWithFrame:SenkoViewBounds(self.view)];
    _scroll.backgroundColor = [UIColor clearColor];
    _scroll.alwaysBounceVertical = YES;
    _scroll.showsHorizontalScrollIndicator = NO;
    [self.view addSubview:_scroll];

    _card = [[UIView alloc] initWithFrame:CGRectZero];
    _card.layer.cornerRadius = SenkoThemeCardRadius();
    _card.clipsToBounds = YES;
    _card.opaque = NO;
    _cardGrad = [CAGradientLayer layer];
    _cardGrad.cornerRadius = SenkoThemeCardRadius();
    [_card.layer insertSublayer:_cardGrad atIndex:0];
    [_scroll addSubview:_card];

    UIImageView *avatar = [[[UIImageView alloc] initWithImage:[UIImage imageNamed:@"sqmrak.jpg"]] autorelease];
    avatar.tag = 1;
    avatar.layer.cornerRadius = 10;
    avatar.layer.masksToBounds = YES;
    [_card addSubview:avatar];

    UILabel *name = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    name.tag = 2;
    name.numberOfLines = 4;
    name.backgroundColor = [UIColor clearColor];
    name.font = [UIFont boldSystemFontOfSize:14];
    name.lineBreakMode = NSLineBreakByClipping;
    SenkoStyleInkLabel(name);
    name.text = [NSString stringWithFormat:@"Senko-%@\nby sqmrak\niOS 5-16", SENKO_VERSION];
/* five taps on the version line open the developer section in settings. a
   gesture recognizer is used rather than a button so the label keeps its
   layout, and UITapGestureRecognizer exists from ios 3.2 */
    name.userInteractionEnabled = YES;
    UITapGestureRecognizer *unlock =
        [[[UITapGestureRecognizer alloc] initWithTarget:self
                                                 action:@selector(versionTapped)] autorelease];
    [name addGestureRecognizer:unlock];
    [_card addSubview:name];

    _githubButton = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    [_githubButton setTitle:@"GitHub" forState:UIControlStateNormal];
    [_githubButton addTarget:self action:@selector(githubPressed)
            forControlEvents:UIControlEventTouchUpInside];
    [_card addSubview:_githubButton];

    _telegramButton = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    [_telegramButton setTitle:@"Telegram" forState:UIControlStateNormal];
    [_telegramButton addTarget:self action:@selector(telegramPressed)
              forControlEvents:UIControlEventTouchUpInside];
    [_card addSubview:_telegramButton];
/* the flat tinted pill read as a label with a tap target. StyleGlossyCapsule
   is the same body/gloss/shadow (and, on ios26, frosted glass) treatment the
   connect and check controls use, applied in layoutAbout once the buttons
   have a real frame to build their gradient layers from */
    for (UIButton *link in [NSArray arrayWithObjects:_githubButton, _telegramButton, nil])
        link.titleLabel.lineBreakMode = NSLineBreakByClipping;

    _info = [[UIView alloc] initWithFrame:CGRectZero];
    _info.layer.cornerRadius = SenkoThemeCardRadius();
    _info.clipsToBounds = YES;
    _info.opaque = NO;
    _infoGrad = [CAGradientLayer layer];
    _infoGrad.cornerRadius = SenkoThemeCardRadius();
    [_info.layer insertSublayer:_infoGrad atIndex:0];
    [_scroll addSubview:_info];

    _bodyLbl = [[UILabel alloc] initWithFrame:CGRectZero];
    _bodyLbl.numberOfLines = 0;
    _bodyLbl.backgroundColor = [UIColor clearColor];
    _bodyLbl.font = [UIFont systemFontOfSize:13];
    _bodyLbl.lineBreakMode = NSLineBreakByWordWrapping;
    SenkoStyleInkLabel(_bodyLbl);
    _bodyLbl.text = SenkoAboutAppReport();
    [_info addSubview:_bodyLbl];

    _sponsorButton = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _sponsorButton.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    _sponsorButton.titleLabel.font = [UIFont boldSystemFontOfSize:13];
    _sponsorButton.titleLabel.lineBreakMode = NSLineBreakByWordWrapping;
    _sponsorButton.titleLabel.numberOfLines = 2;
    [_sponsorButton addTarget:self action:@selector(sponsorPressed)
             forControlEvents:UIControlEventTouchUpInside];
    [_info addSubview:_sponsorButton];

/* old safari cannot open the shop, and the address was only reachable by
   guessing that the line was tappable at all. it is printed in full so it can
   be typed on another device, and one button puts it on the clipboard */
    _sponsorLink = [[UILabel alloc] initWithFrame:CGRectZero];
    _sponsorLink.backgroundColor = [UIColor clearColor];
    _sponsorLink.font = [UIFont systemFontOfSize:11];
    _sponsorLink.numberOfLines = 2;
    _sponsorLink.lineBreakMode = NSLineBreakByCharWrapping;
    _sponsorLink.text = SenkoSponsorURL();
    SenkoStyleMutedLabel(_sponsorLink);
    [_info addSubview:_sponsorLink];

    _sponsorCopy = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _sponsorCopy.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [_sponsorCopy addTarget:self action:@selector(sponsorCopyPressed)
           forControlEvents:UIControlEventTouchUpInside];
    [_info addSubview:_sponsorCopy];
    [self updateSponsorButton];

/* a fixed warm orange rather than the theme accent, so the credit reads the
   same regardless of which palette is active */
    _thanksLbl = [[UILabel alloc] initWithFrame:CGRectZero];
    _thanksLbl.backgroundColor = [UIColor clearColor];
    _thanksLbl.font = [UIFont systemFontOfSize:12];
    _thanksLbl.numberOfLines = 0;
    _thanksLbl.lineBreakMode = NSLineBreakByWordWrapping;
    _thanksLbl.textColor = [UIColor colorWithRed:1.00 green:0.58 blue:0.14 alpha:1.0];
    _thanksLbl.text = SenkoLocalizedText(@"Special thanks: @CookieValerka, @inraxx, "
        @"@not_a_modder, @s3dativee, @Lineysom, @shizotoaster");
    [_info addSubview:_thanksLbl];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(languageDidChange:)
                                                 name:SenkoLanguageDidChangeNotification
                                               object:nil];

    [self layoutAbout];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [_scroll release];
    [_card release];
    [_info release];
    [_bodyLbl release];
    [_sponsorButton release];
    [_sponsorLink release];
    [_sponsorCopy release];
    [_thanksLbl release];
    [_githubButton release];
    [_telegramButton release];
    [super dealloc];
}

- (void)languageDidChange:(NSNotification *)n {
    (void)n;
    _bodyLbl.text = SenkoAboutAppReport();
    [self updateSponsorButton];
    [self layoutAbout];
}

- (void)dismissHint:(UIAlertView *)hint {
    [hint dismissWithClickedButtonIndex:0 animated:YES];
}

- (void)versionTapped {
    if (SenkoDevMenuEnabled()) return; /* the section turns itself off */
    int left = SenkoDevMenuTapsLeft();
    if (left > 0) {
/* silence until the third tap, so an accidental double tap says nothing */
        if (left > 2) return;
        UIAlertView *hint = [[[UIAlertView alloc]
            initWithTitle:nil
                  message:[NSString stringWithFormat:
                           SenkoLocalizedText(@"%d more"), left]
                 delegate:nil
        cancelButtonTitle:nil
        otherButtonTitles:nil] autorelease];
        [hint show];
/* dismissWithClickedButtonIndex:animated: takes two scalars, which
   performSelector:withObject: cannot pass, so the delay goes through a method
   that takes the alert itself */
        [self performSelector:@selector(dismissHint:) withObject:hint afterDelay:0.6];
        return;
    }
    SenkoSetDevMenuEnabled(YES);
    UIAlertView *done = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Developer section")
              message:SenkoLocalizedText(@"It is now in settings, under the app section. Five taps on its heading hide it again.")
             delegate:nil
    cancelButtonTitle:@"OK"
    otherButtonTitles:nil] autorelease];
    [done show];
}

- (void)updateSponsorButton {
    NSString *title = SenkoLanguageIsChinese()
        ? @"赞助商：2xvpn.shop，优惠码 SENKO 可享 15% 折扣"
        : SenkoLanguageIsRussian()
        ? @"Спонсор: 2xvpn.shop, промокод SENKO даёт скидку 15%"
        : @"Sponsor: 2xvpn.shop, promo code SENKO takes 15% off";
    [_sponsorButton setTitle:title forState:UIControlStateNormal];
    [_sponsorButton setTitleColor:kAccentBlue forState:UIControlStateNormal];
    [_sponsorCopy setTitle:SenkoLocalizedText(@"Copy link") forState:UIControlStateNormal];
    _sponsorLink.text = SenkoSponsorURL();
}

- (void)sponsorPressed {
    NSURL *url = [NSURL URLWithString:SenkoSponsorURL()];
    if (url) [[UIApplication sharedApplication] openURL:url];
}

- (void)sponsorCopyPressed {
    [[UIPasteboard generalPasteboard] setString:SenkoSponsorURL()];
    [_sponsorCopy setTitle:SenkoLocalizedText(@"Copied") forState:UIControlStateNormal];
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(updateSponsorButton)
                                               object:nil];
    [self performSelector:@selector(updateSponsorButton) withObject:nil afterDelay:1.6];
}

- (void)githubPressed {
    NSURL *url = [NSURL URLWithString:@"https://github.com/sqmrak/Senko"];
    if (url) [[UIApplication sharedApplication] openURL:url];
}

- (void)telegramPressed {
    NSURL *url = [NSURL URLWithString:@"https://t.me/sqmrakdev"];
    if (url) [[UIApplication sharedApplication] openURL:url];
}

- (void)layoutAbout {
    CGRect b = SenkoViewBounds(self.view);
    if (b.size.width < 2.0f || b.size.height < 2.0f) return;

    UIView *bg = [self.view viewWithTag:9111];
    if (bg) bg.frame = CGRectMake(0, 0, b.size.width, b.size.height);

    _scroll.frame = CGRectMake(0, 0, b.size.width, b.size.height);

/* limiting only tablet width keeps phone landscape text from truncating */
    CGFloat contentW = b.size.width;
    if (contentW > 700.0f) contentW = 700.0f;
    CGFloat contentX = floorf((b.size.width - contentW) * 0.5f);
    CGFloat side = 12.0f;
    CGFloat cardW = contentW - side * 2.0f;
    if (cardW < 120.0f) cardW = 120.0f;

    CGFloat heroH = 126.0f;
    _card.frame = CGRectMake(contentX + side, 16, cardW, heroH);
    _cardGrad.frame = CGRectMake(0, 0, cardW, heroH);

    UIImageView *avatar = (UIImageView *)[_card viewWithTag:1];
    UILabel *name = (UILabel *)[_card viewWithTag:2];
    avatar.frame = CGRectMake(12, 12, 68, 68);
    avatar.layer.cornerRadius = 18.0f;
    name.frame = CGRectMake(92, 12, MAX(40.0f, cardW - 104.0f), 76);
    CGFloat linkGap = 8.0f;
    CGFloat linkW = floorf((cardW - 24.0f - linkGap) * 0.5f);
    _githubButton.frame = CGRectMake(12, 90, linkW, 24);
    _telegramButton.frame = CGRectMake(12 + linkW + linkGap, 90, linkW, 24);
    StyleGlossyCapsule(_githubButton, kAccentBlue, kAccentBlueLo);
    StyleGlossyCapsule(_telegramButton, kAccentBlue, kAccentBlueLo);

    CGFloat textPad = 14.0f;
    CGFloat textW = cardW - textPad * 2.0f;
    if (textW < 80.0f) textW = 80.0f;
    NSString *txt = _bodyLbl.text ? _bodyLbl.text : @"";
    CGSize bodySz = SenkoTextSize(txt, _bodyLbl.font, textW);
    if (bodySz.height < 40.0f) bodySz.height = 40.0f;
    if (bodySz.height > 2000.0f) bodySz.height = 2000.0f;

    CGFloat sponsorH = 34.0f;
    CGFloat linkH = 30.0f;
    CGFloat copyH = 28.0f;
    NSString *thanksTxt = _thanksLbl.text ? _thanksLbl.text : @"";
    CGSize thanksSz = SenkoTextSize(thanksTxt, _thanksLbl.font, textW);
    if (thanksSz.height < 14.0f) thanksSz.height = 14.0f;
    CGFloat thanksGap = 10.0f;
    CGFloat infoH = bodySz.height + sponsorH + linkH + copyH +
                    thanksGap + thanksSz.height + 40.0f;
    CGFloat infoY = CGRectGetMaxY(_card.frame) + 12.0f;
    _info.frame = CGRectMake(contentX + side, infoY, cardW, infoH);
    _infoGrad.frame = CGRectMake(0, 0, cardW, infoH);
    _bodyLbl.frame = CGRectMake(textPad, 12, textW, bodySz.height + 2);
    _sponsorButton.frame = CGRectMake(textPad,
                                      14.0f + bodySz.height,
                                      textW,
                                      sponsorH);
/* the button used to sit beside the address and clip its own russian title to
   "Копиро...ссылку"; the address gets the full width and the button its own row */
    _sponsorLink.frame = CGRectMake(textPad,
                                    14.0f + bodySz.height + sponsorH,
                                    textW, linkH);
    _sponsorCopy.frame = CGRectMake(textPad,
                                    14.0f + bodySz.height + sponsorH + linkH + 4.0f,
                                    textW, copyH);
    StyleGlossyCapsule(_sponsorCopy, kAccentBlue, kAccentBlueLo);
    _thanksLbl.frame = CGRectMake(textPad,
                                  14.0f + bodySz.height + sponsorH + linkH + 4.0f + copyH + thanksGap,
                                  textW, thanksSz.height);

    BOOL light = SenkoThemeIsLight();
    if (SenkoThemeIsIos26()) {
        UIColor *hi = light
            ? [UIColor colorWithWhite:1 alpha:0.40]
            : [UIColor colorWithWhite:1 alpha:0.14];
        UIColor *lo = light
            ? [UIColor colorWithWhite:1 alpha:0.18]
            : [UIColor colorWithWhite:1 alpha:0.06];
        _cardGrad.colors = [NSArray arrayWithObjects:(id)hi.CGColor, (id)lo.CGColor, nil];
        _infoGrad.colors = [NSArray arrayWithObjects:(id)hi.CGColor, (id)lo.CGColor, nil];
        _card.layer.borderWidth = 0.5f;
        _info.layer.borderWidth = 0.5f;
        _card.layer.borderColor = light
            ? [UIColor colorWithWhite:1 alpha:0.55].CGColor
            : [UIColor colorWithWhite:1 alpha:0.22].CGColor;
        _info.layer.borderColor = _card.layer.borderColor;
        _card.backgroundColor = [UIColor clearColor];
        _info.backgroundColor = [UIColor clearColor];
    } else {
        _cardGrad.colors = [NSArray arrayWithObjects:(id)kCellHi.CGColor, (id)kCellLo.CGColor, nil];
        _infoGrad.colors = [NSArray arrayWithObjects:(id)kCellHi.CGColor, (id)kCellLo.CGColor, nil];
        _card.layer.borderWidth = 0;
        _info.layer.borderWidth = 0;
    }

    _scroll.contentSize = CGSizeMake(b.size.width, infoY + infoH + 24.0f);
    _scroll.contentOffset = CGPointMake(0, _scroll.contentOffset.y);
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("about");
    [super viewWillAppear:animated];
    SenkoApplyScreenChrome(self.view);
    [self layoutAbout];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutAbout];
}

- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)io
                                         duration:(NSTimeInterval)dur {
    (void)io; (void)dur;
    [self layoutAbout];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io {
    (void)io;
    [self layoutAbout];
}
@end
