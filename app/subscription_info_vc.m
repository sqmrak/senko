#import "app_common.h"
#import "ui_theme.h"
#import "crash_report.h"
#import "card_kit.h"
#import "control_client.h"
#include <objc/message.h>
#include "../daemon/core/b64.h"
#include <math.h>
#include <string.h>

static NSString *SenkoMetadataText(NSString *text) {
    if (![text hasPrefix:@"base64:"]) return text;
    const char *encoded = [[text substringFromIndex:7] UTF8String];
    unsigned char decoded[1024];
    size_t n = 0;
    if (!encoded || b64_decode(encoded, strlen(encoded), decoded,
                               sizeof decoded, &n) != 0 || n == 0)
        return SenkoLocalizedText(@"Not provided");
    NSString *value = [[[NSString alloc] initWithBytes:decoded length:n
                                               encoding:NSUTF8StringEncoding] autorelease];
    return [value length] ? value : SenkoLocalizedText(@"Not provided");
}

static NSString *SenkoBytes(unsigned long long value) {
    double n = (double)value;
    NSArray *units = SenkoLanguageIsChinese()
        ? [NSArray arrayWithObjects:@"B", @"KB", @"MB", @"GB", @"TB", nil]
        : SenkoLanguageIsRussian()
        ? [NSArray arrayWithObjects:@"Б", @"КБ", @"МБ", @"ГБ", @"ТБ", nil]
        : [NSArray arrayWithObjects:@"B", @"KB", @"MB", @"GB", @"TB", nil];
    NSUInteger unit = 0;
    while (n >= 1024.0 && unit + 1 < [units count]) { n /= 1024.0; unit++; }
    NSString *number = [NSString stringWithFormat:n >= 10.0 || unit == 0 ? @"%.0f" : @"%.1f", n];
    if (SenkoLanguageIsRussian())
        number = [number stringByReplacingOccurrencesOfString:@"." withString:@","];
    return [NSString stringWithFormat:@"%@ %@", number, [units objectAtIndex:unit]];
}

static const CGFloat kInfoGap = 12.0f;

@interface SubscriptionInfoVC ()
- (void)themeDidChange:(NSNotification *)n;
@end

@implementation SubscriptionInfoVC {
    SenkoSub *_sub;
    UIScrollView *_scroll;
    SenkoCardView *_plan;
    UILabel *_planName;
    UILabel *_planUsed;
    UILabel *_planLimit;
    SenkoMeterView *_planMeter;
    UILabel *_planLeft;
    UILabel *_planExpiry;
    SenkoCardView *_upCard;
    SenkoCardView *_downCard;
    UIImageView *_upIcon;
    UIImageView *_downIcon;
    UILabel *_upCaption;
    UILabel *_downCaption;
    UILabel *_upValue;
    UILabel *_downValue;
    SenkoCardView *_about;
    UILabel *_aboutCaption;
    UILabel *_aboutText;
    SenkoCardView *_support;
    UILabel *_supportCaption;
    UILabel *_supportText;
    UIImageView *_supportChevron;
    UIButton *_supportTap;
}

- (id)initWithSubscription:(SenkoSub *)sub {
    if ((self = [super initWithNibName:nil bundle:nil])) _sub = [sub retain];
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_sub release];
    [_scroll release];
    [_plan release];
    [_planName release];
    [_planUsed release];
    [_planLimit release];
    [_planMeter release];
    [_planLeft release];
    [_planExpiry release];
    [_upCard release];
    [_downCard release];
    [_upIcon release];
    [_downIcon release];
    [_upCaption release];
    [_downCaption release];
    [_upValue release];
    [_downValue release];
    [_about release];
    [_aboutCaption release];
    [_aboutText release];
    [_support release];
    [_supportCaption release];
    [_supportText release];
    [_supportChevron release];
    [_supportTap release];
    [super dealloc];
}

- (UILabel *)labelOn:(UIView *)host size:(CGFloat)size bold:(BOOL)bold
               muted:(BOOL)muted alignment:(NSTextAlignment)alignment {
    return [SenkoCardLabel(host, SenkoFontBody(size, bold), muted ? kInkMuted : kInk,
                           alignment) retain];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"Subscription details");
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setAutomaticallyAdjustsScrollViewInsets:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setAutomaticallyAdjustsScrollViewInsets:), NO);
    self.navigationItem.leftBarButtonItem = [[[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self
        action:@selector(donePressed)] autorelease];
    SenkoApplyScreenChrome(self.view);

    _scroll = [[UIScrollView alloc] initWithFrame:SenkoViewBounds(self.view)];
    _scroll.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _scroll.alwaysBounceVertical = YES;
    _scroll.backgroundColor = [UIColor clearColor];
    [self.view addSubview:_scroll];

    _plan = [[SenkoCardView alloc] initWithFrame:CGRectZero];
    [_scroll addSubview:_plan];
    _planName = [self labelOn:_plan size:15.0f bold:YES muted:YES alignment:NSTextAlignmentLeft];
    _planName.lineBreakMode = NSLineBreakByTruncatingTail;
    _planUsed = [SenkoCardLabel(_plan, SenkoFontDisplay(36.0f), kInk, NSTextAlignmentLeft) retain];
    _planUsed.adjustsFontSizeToFitWidth = YES;
    _planLimit = [self labelOn:_plan size:15.0f bold:NO muted:YES alignment:NSTextAlignmentLeft];
    _planMeter = [[SenkoMeterView alloc] initWithFrame:CGRectZero];
    [_plan addSubview:_planMeter];
    _planLeft = [self labelOn:_plan size:14.0f bold:NO muted:YES alignment:NSTextAlignmentLeft];
    _planExpiry = [self labelOn:_plan size:14.0f bold:NO muted:YES alignment:NSTextAlignmentRight];

    _upCard = [[SenkoCardView alloc] initWithFrame:CGRectZero];
    _downCard = [[SenkoCardView alloc] initWithFrame:CGRectZero];
    [_scroll addSubview:_upCard];
    [_scroll addSubview:_downCard];
    _upIcon = [[UIImageView alloc] initWithFrame:CGRectZero];
    _downIcon = [[UIImageView alloc] initWithFrame:CGRectZero];
    [_upCard addSubview:_upIcon];
    [_downCard addSubview:_downIcon];
    _upCaption = [self labelOn:_upCard size:14.0f bold:YES muted:YES alignment:NSTextAlignmentLeft];
    _downCaption = [self labelOn:_downCard size:14.0f bold:YES muted:YES alignment:NSTextAlignmentLeft];
    _upValue = [self labelOn:_upCard size:22.0f bold:YES muted:NO alignment:NSTextAlignmentLeft];
    _downValue = [self labelOn:_downCard size:22.0f bold:YES muted:NO alignment:NSTextAlignmentLeft];
    _upValue.adjustsFontSizeToFitWidth = YES;
    _downValue.adjustsFontSizeToFitWidth = YES;

    _about = [[SenkoCardView alloc] initWithFrame:CGRectZero];
    [_scroll addSubview:_about];
    _aboutCaption = [self labelOn:_about size:14.0f bold:YES muted:YES alignment:NSTextAlignmentLeft];
    _aboutText = [self labelOn:_about size:15.0f bold:NO muted:NO alignment:NSTextAlignmentLeft];
    _aboutText.numberOfLines = 0;
    _aboutText.lineBreakMode = NSLineBreakByWordWrapping;

    if ([_sub->supportURL length]) {
        _support = [[SenkoCardView alloc] initWithFrame:CGRectZero];
        [_scroll addSubview:_support];
        _supportCaption = [self labelOn:_support size:14.0f bold:YES muted:YES
                              alignment:NSTextAlignmentLeft];
        _supportText = [self labelOn:_support size:15.0f bold:NO muted:NO
                           alignment:NSTextAlignmentLeft];
        _supportText.lineBreakMode = NSLineBreakByTruncatingMiddle;
        _supportChevron = [[UIImageView alloc] initWithFrame:CGRectZero];
        _supportChevron.contentMode = UIViewContentModeCenter;
        [_support addSubview:_supportChevron];
        _supportTap = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
        [_supportTap addTarget:self action:@selector(supportPressed)
              forControlEvents:UIControlEventTouchUpInside];
        [_supportTap addTarget:self action:@selector(pressDown:)
              forControlEvents:UIControlEventTouchDown];
        [_supportTap addTarget:self action:@selector(pressUp:)
              forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                               UIControlEventTouchCancel];
        [_support addSubview:_supportTap];
    }
    [self applyChrome];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
}

- (void)pressDown:(UIView *)sender { (void)sender; SenkoPressPop(_support, YES); }
- (void)pressUp:(UIView *)sender { (void)sender; SenkoPressPop(_support, NO); }

- (NSString *)descriptionText {
    return [_sub->description length] ? SenkoMetadataText(_sub->description)
                                      : SenkoLocalizedText(@"Not provided");
}

/* text, colours and glyphs come from the palette, which a theme switch replaces */
- (void)applyChrome {
    SenkoApplyScreenChrome(self.view);
    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    for (SenkoCardView *card in [NSArray arrayWithObjects:_plan, _upCard, _downCard, _about,
                                 _support, nil])
        [card restyle];
    unsigned long long used = _sub->upload + _sub->download;
    unsigned long long left = _sub->total > used ? _sub->total - used : 0;
    NSString *title = [_sub->name length] ? _sub->name : SenkoLocalizedText(@"Subscription");
    _planName.text = [title stringByReplacingOccurrencesOfString:@"_" withString:@" "];
    _planName.textColor = kInkMuted;
    _planUsed.text = SenkoBytes(used);
    _planUsed.textColor = kInk;
    _planLimit.textColor = kInkMuted;
    _planLimit.text = _sub->total
        ? [NSString stringWithFormat:SenkoLocalizedText(@"%@ of %@"), SenkoBytes(used),
           SenkoBytes(_sub->total)]
        : SenkoLocalizedText(@"Not provided");
    _planMeter.hidden = _sub->total == 0;
    if (_sub->total) {
        CGFloat fraction = (CGFloat)((double)used / (double)_sub->total);
        _planMeter.fraction = fraction;
        _planMeter.fill = fraction >= 0.9f
            ? [UIColor colorWithRed:0.90f green:0.45f blue:0.15f alpha:1.0f] : kAccentBlue;
    }
    _planLeft.textColor = kInkMuted;
    _planLeft.text = _sub->total
        ? [NSString stringWithFormat:@"%@: %@", SenkoLocalizedText(@"Remaining"), SenkoBytes(left)]
        : @"";
    _planExpiry.textColor = kInkMuted;
    _planExpiry.text = _sub->expire
        ? [NSString stringWithFormat:@"%@: %@", SenkoLocalizedText(@"Expires"), [self expiryText]]
        : @"";
    _upCaption.text = SenkoLocalizedText(@"Uploaded");
    _downCaption.text = SenkoLocalizedText(@"Downloaded");
    _upCaption.textColor = _downCaption.textColor = kInkMuted;
    _upValue.text = SenkoBytes(_sub->upload);
    _downValue.text = SenkoBytes(_sub->download);
    _upValue.textColor = _downValue.textColor = kInk;
    _upIcon.image = TintedIconNamed(@"glyph-arrow-up.png", 20.0f, kAccentBlue);
    _downIcon.image = TintedIconNamed(@"glyph-arrow-down.png", 20.0f, kConnOn);
    _aboutCaption.text = SenkoLocalizedText(@"Description");
    _aboutCaption.textColor = kInkMuted;
    _aboutText.text = [self descriptionText];
    _aboutText.textColor = kInk;
    if (_support) {
        _supportCaption.text = SenkoLocalizedText(@"Contact support");
        _supportCaption.textColor = kInkMuted;
        _supportText.text = _sub->supportURL;
        _supportText.textColor = kInk;
        _supportChevron.image = SenkoIconChevron(18.0f, kInkMuted);
    }
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    [self applyChrome];
    [self layoutInfo];
}

- (void)layoutInfo {
    CGRect b = SenkoViewBounds(self.view);
    if (b.size.width < 2.0f) return;
    UIEdgeInsets safe = SenkoSafeAreaInsets(self.view);
    CGFloat col = SenkoCardColumnWidth(b.size.width);
    CGFloat x = floorf((b.size.width - col) * 0.5f);
    CGFloat y = 16.0f, pad = 16.0f;

    CGFloat planHeight = _sub->total ? 156.0f : 104.0f;
    _plan.frame = CGRectMake(x, y, col, planHeight);
    _planName.frame = CGRectMake(pad, 12.0f, col - pad * 2.0f, 20.0f);
    _planUsed.frame = CGRectMake(pad, 34.0f, col - pad * 2.0f, 44.0f);
    _planLimit.frame = CGRectMake(pad, 78.0f, col - pad * 2.0f, 20.0f);
    if (_sub->total) {
        _planMeter.frame = CGRectMake(pad, 106.0f, col - pad * 2.0f, 8.0f);
        CGFloat expiryWidth = [_planExpiry.text length]
            ? SenkoTextWidth(_planExpiry.text, _planExpiry.font) + 4.0f : 0.0f;
        _planLeft.frame = CGRectMake(pad, 124.0f, col - pad * 2.0f - expiryWidth - 8.0f, 20.0f);
        _planExpiry.frame = CGRectMake(col - pad - expiryWidth, 124.0f, expiryWidth, 20.0f);
    } else {
        _planLeft.frame = CGRectZero;
        _planExpiry.frame = CGRectMake(pad, 78.0f, col - pad * 2.0f, 20.0f);
        _planExpiry.textAlignment = NSTextAlignmentLeft;
        _planLimit.frame = CGRectZero;
    }
    y += planHeight + kInfoGap;

    CGFloat half = floorf((col - kInfoGap) * 0.5f);
    _upCard.frame = CGRectMake(x, y, half, 80.0f);
    _downCard.frame = CGRectMake(x + col - half, y, half, 80.0f);
    SenkoCardView *cards[2] = { _upCard, _downCard };
    UIImageView *icons[2] = { _upIcon, _downIcon };
    UILabel *caps[2] = { _upCaption, _downCaption };
    UILabel *vals[2] = { _upValue, _downValue };
    for (int i = 0; i < 2; ++i) {
        CGFloat w = cards[i].bounds.size.width;
        icons[i].frame = CGRectMake(14.0f, 12.0f, 20.0f, 20.0f);
        caps[i].frame = CGRectMake(40.0f, 11.0f, w - 52.0f, 22.0f);
        vals[i].frame = CGRectMake(14.0f, 38.0f, w - 28.0f, 28.0f);
    }
    y += 80.0f + kInfoGap;

    CGSize text = SenkoTextSize([self descriptionText], _aboutText.font, col - pad * 2.0f);
    CGFloat aboutHeight = 40.0f + ceilf(text.height) + 14.0f;
    _about.frame = CGRectMake(x, y, col, aboutHeight);
    _aboutCaption.frame = CGRectMake(pad, 12.0f, col - pad * 2.0f, 20.0f);
    _aboutText.frame = CGRectMake(pad, 38.0f, col - pad * 2.0f, ceilf(text.height));
    y += aboutHeight + kInfoGap;

    if (_support) {
        _support.frame = CGRectMake(x, y, col, 68.0f);
        _supportCaption.frame = CGRectMake(pad, 12.0f, col - pad * 2.0f - 28.0f, 20.0f);
        _supportText.frame = CGRectMake(pad, 36.0f, col - pad * 2.0f - 28.0f, 22.0f);
        _supportChevron.frame = CGRectMake(col - pad - 18.0f, 25.0f, 18.0f, 18.0f);
        _supportTap.frame = _support.bounds;
        y += 68.0f + kInfoGap;
    }
    _scroll.contentSize = CGSizeMake(b.size.width, y + safe.bottom);
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    _scroll.frame = SenkoViewBounds(self.view);
    [self layoutInfo];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    SenkoCrashScreen("subscription details");
    [self applyChrome];
}

- (void)donePressed {
    SenkoDismissModal(self, nil);
}

- (NSString *)expiryText {
    if (!_sub->expire) return SenkoLocalizedText(@"Not provided");
    NSDate *date = [NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)_sub->expire];
    if ([date timeIntervalSinceNow] <= 0.0) return SenkoLocalizedText(@"Expired");
    NSDateFormatter *fmt = [[[NSDateFormatter alloc] init] autorelease];
    fmt.locale = [[[NSLocale alloc] initWithLocaleIdentifier:
        SenkoLanguageIsRussian() ? @"ru_RU" : @"en_US"] autorelease];
    fmt.dateStyle = NSDateFormatterMediumStyle;
    fmt.timeStyle = NSDateFormatterShortStyle;
    return [fmt stringFromDate:date];
}

- (void)supportPressed {
    NSURL *url = [NSURL URLWithString:_sub->supportURL];
    if (url && ([[url scheme] isEqualToString:@"https"] || [[url scheme] isEqualToString:@"http"]))
        [[UIApplication sharedApplication] openURL:url];
}

@end
