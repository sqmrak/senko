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
    CAGradientLayer *_cardGrad;
    CAGradientLayer *_infoGrad;

}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"About";
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    SenkoApplyScreenChrome(self.view);

    _scroll = [[UIScrollView alloc] initWithFrame:SenkoViewBounds(self.view)];
    _scroll.backgroundColor = [UIColor clearColor];
    _scroll.alwaysBounceVertical = YES;
    _scroll.showsHorizontalScrollIndicator = NO;
    [self.view addSubview:_scroll];

    _card = [[UIView alloc] initWithFrame:CGRectZero];
    _card.layer.cornerRadius = 12;
    _card.clipsToBounds = YES;
    _card.opaque = NO;
    _cardGrad = [CAGradientLayer layer];
    _cardGrad.cornerRadius = 12;
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
    name.text = [NSString stringWithFormat:@"Senko %@\ngithub.com/sqmrak", SENKO_VERSION];
    [_card addSubview:name];

    _info = [[UIView alloc] initWithFrame:CGRectZero];
    _info.layer.cornerRadius = 12;
    _info.clipsToBounds = YES;
    _info.opaque = NO;
    _infoGrad = [CAGradientLayer layer];
    _infoGrad.cornerRadius = 12;
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

    [self layoutAbout];
}

- (void)dealloc {
    [_scroll release];
    [_card release];
    [_info release];
    [_bodyLbl release];
    [super dealloc];
}

- (void)layoutAbout {
    CGRect b = SenkoViewBounds(self.view);
    if (b.size.width < 2.0f || b.size.height < 2.0f) return;

/* keep wallpaper sized to real bounds */
    UIView *bg = [self.view viewWithTag:9111];
    if (bg) bg.frame = CGRectMake(0, 0, b.size.width, b.size.height);

    _scroll.frame = CGRectMake(0, 0, b.size.width, b.size.height);

/* use full width on phone landscape; cap only on wide tablets */
    CGFloat contentW = b.size.width;
    if (contentW > 700.0f) contentW = 700.0f;
    CGFloat contentX = floorf((b.size.width - contentW) * 0.5f);
    CGFloat side = 12.0f;
    CGFloat cardW = contentW - side * 2.0f;
    if (cardW < 120.0f) cardW = 120.0f;

    _card.frame = CGRectMake(contentX + side, 16, cardW, 100);
    _cardGrad.frame = CGRectMake(0, 0, cardW, 100);

    UIImageView *avatar = (UIImageView *)[_card viewWithTag:1];
    UILabel *name = (UILabel *)[_card viewWithTag:2];
    avatar.frame = CGRectMake(12, 12, 76, 76);
    name.frame = CGRectMake(100, 22, MAX(40.0f, cardW - 118.0f), 56);

    CGFloat textPad = 14.0f;
    CGFloat textW = cardW - textPad * 2.0f;
    if (textW < 80.0f) textW = 80.0f;
    CGSize bodySz = CGSizeMake(textW, 40);
    NSString *txt = _bodyLbl.text ? _bodyLbl.text : @"";
    if ([txt respondsToSelector:@selector(sizeWithFont:constrainedToSize:lineBreakMode:)]) {
        bodySz = [txt sizeWithFont:_bodyLbl.font
                 constrainedToSize:CGSizeMake(textW, 5000)
                     lineBreakMode:NSLineBreakByWordWrapping];
    } else {
        _bodyLbl.frame = CGRectMake(0, 0, textW, 10);
        [_bodyLbl sizeToFit];
        bodySz = _bodyLbl.bounds.size;
        if (bodySz.width > textW) bodySz.width = textW;
    }
    if (bodySz.height < 40.0f) bodySz.height = 40.0f;
    if (bodySz.height > 2000.0f) bodySz.height = 2000.0f;

    CGFloat infoH = bodySz.height + 28.0f;
    CGFloat infoY = CGRectGetMaxY(_card.frame) + 12.0f;
    _info.frame = CGRectMake(contentX + side, infoY, cardW, infoH);
    _infoGrad.frame = CGRectMake(0, 0, cardW, infoH);
    _bodyLbl.frame = CGRectMake(textPad, 12, textW, bodySz.height + 2);

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
