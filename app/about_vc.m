#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/message.h>
#import "ui_theme.h"
#import "app_common.h"

@implementation AboutVC {

    UIScrollView *_scroll;
    UIView *_card;
    UIView *_info;
    UILabel *_bodyLbl;
    UIButton *_sponsorButton;
    UIButton *_githubButton;
    UIButton *_telegramButton;
    CAGradientLayer *_cardGrad;
    CAGradientLayer *_infoGrad;

}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"about");
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
    name.numberOfLines = 3;
    name.backgroundColor = [UIColor clearColor];
    name.font = [UIFont boldSystemFontOfSize:15];
    SenkoStyleInkLabel(name);
    name.text = [NSString stringWithFormat:@"senko\n%@\nios 5-15 · armv7 + arm64", SENKO_VERSION];
    [_card addSubview:name];

    _githubButton = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    [_githubButton setTitle:@"github" forState:UIControlStateNormal];
    [_githubButton addTarget:self action:@selector(githubPressed)
            forControlEvents:UIControlEventTouchUpInside];
    [_card addSubview:_githubButton];

    _telegramButton = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    [_telegramButton setTitle:@"telegram" forState:UIControlStateNormal];
    [_telegramButton addTarget:self action:@selector(telegramPressed)
              forControlEvents:UIControlEventTouchUpInside];
    [_card addSubview:_telegramButton];
    for (UIButton *link in [NSArray arrayWithObjects:_githubButton, _telegramButton, nil]) {
        link.titleLabel.font = [UIFont boldSystemFontOfSize:12.0f];
        link.titleLabel.lineBreakMode = NSLineBreakByClipping;
        [link setTitleColor:kAccentBlue forState:UIControlStateNormal];
        link.backgroundColor = [kAccentBlue colorWithAlphaComponent:0.12f];
        link.layer.cornerRadius = 12.0f;
        link.layer.borderWidth = 0.5f;
        link.layer.borderColor = [kAccentBlue colorWithAlphaComponent:0.30f].CGColor;
    }

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
    [self updateSponsorButton];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(languageDidChange:)
                                                 name:SenkoLanguageDidChangeNotification
                                               object:nil];

    [self layoutAbout];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_scroll release];
    [_card release];
    [_info release];
    [_bodyLbl release];
    [_sponsorButton release];
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

- (void)updateSponsorButton {
    NSString *title = SenkoLanguageIsRussian()
        ? @"спонсоры: 2xvpn.shop"
        : @"sponsors: 2xvpn.shop";
    [_sponsorButton setTitle:title forState:UIControlStateNormal];
    [_sponsorButton setTitleColor:kAccentBlue forState:UIControlStateNormal];
}

- (void)sponsorPressed {
    NSURL *url = [NSURL URLWithString:@"https://2xvpn.shop/dashboard/buy?promo=SENKO"];
    if (url) [[UIApplication sharedApplication] openURL:url];
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
    name.frame = CGRectMake(92, 12, MAX(40.0f, cardW - 104.0f), 68);
    CGFloat linkGap = 8.0f;
    CGFloat linkW = floorf((cardW - 24.0f - linkGap) * 0.5f);
    _githubButton.frame = CGRectMake(12, 90, linkW, 24);
    _telegramButton.frame = CGRectMake(12 + linkW + linkGap, 90, linkW, 24);

    CGFloat textPad = 14.0f;
    CGFloat textW = cardW - textPad * 2.0f;
    if (textW < 80.0f) textW = 80.0f;
    NSString *txt = _bodyLbl.text ? _bodyLbl.text : @"";
    CGSize bodySz = SenkoTextSize(txt, _bodyLbl.font, textW);
    if (bodySz.height < 40.0f) bodySz.height = 40.0f;
    if (bodySz.height > 2000.0f) bodySz.height = 2000.0f;

    CGFloat sponsorH = 30.0f;
    CGFloat infoH = bodySz.height + sponsorH + 32.0f;
    CGFloat infoY = CGRectGetMaxY(_card.frame) + 12.0f;
    _info.frame = CGRectMake(contentX + side, infoY, cardW, infoH);
    _infoGrad.frame = CGRectMake(0, 0, cardW, infoH);
    _bodyLbl.frame = CGRectMake(textPad, 12, textW, bodySz.height + 2);
    _sponsorButton.frame = CGRectMake(textPad,
                                      14.0f + bodySz.height,
                                      textW,
                                      sponsorH);

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
