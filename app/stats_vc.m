#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>
#import "app_common.h"
#import "control_client.h"
#import "native_vpn.h"
#import "server_cell.h"
#import "ui_theme.h"
#import "crash_report.h"
#import "card_kit.h"

enum {
    SenkoStatsState = 0,
    SenkoStatsServer,
    SenkoStatsUptime,
    SenkoStatsDown,
    SenkoStatsUp,
    SenkoStatsDownRate,
    SenkoStatsUpRate,
    SenkoStatsSessionRows
};

static NSString *SenkoStatsUptimeText(long seconds) {
    if (seconds < 0) return nil;
    long h = seconds / 3600;
    long m = (seconds % 3600) / 60;
    long s = seconds % 60;
    if (h > 0) return [NSString stringWithFormat:@"%ld:%02ld:%02ld", h, m, s];
    return [NSString stringWithFormat:@"%ld:%02ld", m, s];
}

/* the plan left on a subscription, as the panel reports it in its headers */
static NSString *SenkoStatsUsageText(SenkoSub *sub) {
    unsigned long long used = sub->upload + sub->download;
    if (sub->total)
        return [NSString stringWithFormat:SenkoLocalizedText(@"%@ of %@"),
                SenkoFormatBytes(used), SenkoFormatBytes(sub->total)];
    return used ? SenkoFormatBytes(used) : @"—";
}

static NSString *SenkoStatsExpiryText(SenkoSub *sub) {
    if (!sub->expire) return @"";
    NSDate *date = [NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)sub->expire];
    if ([date timeIntervalSinceNow] <= 0.0) return SenkoLocalizedText(@"expired");
    NSDateFormatter *fmt = [[[NSDateFormatter alloc] init] autorelease];
    fmt.dateFormat = SenkoLanguageIsRussian() ? @"dd.MM.yy" : @"yyyy-MM-dd";
    return [fmt stringFromDate:date];
}

static const CGFloat kStatsGap = 12.0f;

/* one subscription: its name, what is used of the plan, a meter and the date it
   ends. the meter turns warm when little of the plan is left */
@interface SenkoStatsSubCard : SenkoCardView {
@public
    UILabel *name;
    UILabel *usage;
    UILabel *expiry;
    SenkoMeterView *meter;
}
- (void)fillWith:(SenkoSub *)sub;
@end

@implementation SenkoStatsSubCard

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        name = SenkoCardLabel(self, SenkoFontBody(16.0f, YES), kInk, NSTextAlignmentLeft);
        name.lineBreakMode = NSLineBreakByTruncatingTail;
        usage = SenkoCardLabel(self, SenkoFontBody(14.0f, NO), kInkMuted, NSTextAlignmentLeft);
        expiry = SenkoCardLabel(self, SenkoFontBody(14.0f, NO), kInkMuted, NSTextAlignmentRight);
        meter = [[[SenkoMeterView alloc] initWithFrame:CGRectZero] autorelease];
        [self addSubview:meter];
    }
    return self;
}

- (void)fillWith:(SenkoSub *)sub {
    NSString *title = [sub->name length] ? sub->name : SenkoLocalizedText(@"Subscription");
    name.text = [title stringByReplacingOccurrencesOfString:@"_" withString:@" "];
    usage.text = SenkoStatsUsageText(sub);
    expiry.text = SenkoStatsExpiryText(sub);
    name.textColor = kInk;
    usage.textColor = kInkMuted;
    expiry.textColor = [expiry.text isEqualToString:SenkoLocalizedText(@"expired")]
        ? [UIColor colorWithRed:0.86f green:0.27f blue:0.22f alpha:1.0f] : kInkMuted;
    unsigned long long used = sub->upload + sub->download;
    meter.hidden = sub->total == 0;
    if (sub->total) {
        CGFloat used_fraction = (CGFloat)((double)used / (double)sub->total);
        meter.fraction = used_fraction;
        meter.fill = used_fraction >= 0.9f
            ? [UIColor colorWithRed:0.90f green:0.45f blue:0.15f alpha:1.0f] : kAccentBlue;
    }
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat w = self.bounds.size.width, pad = 16.0f;
    name.frame = CGRectMake(pad, 12.0f, w - pad * 2.0f, 22.0f);
    CGFloat expiryWidth = [expiry.text length] ? SenkoTextWidth(expiry.text, expiry.font) + 4.0f : 0.0f;
    usage.frame = CGRectMake(pad, 36.0f, w - pad * 2.0f - expiryWidth - 8.0f, 20.0f);
    expiry.frame = CGRectMake(w - pad - expiryWidth, 36.0f, expiryWidth, 20.0f);
    meter.frame = CGRectMake(pad, 64.0f, w - pad * 2.0f, 6.0f);
}

@end

@implementation StatsVC {
    UIScrollView *_scroll;
    SenkoCardView *_hero;
    UIView *_heroDot;
    UILabel *_heroState;
    UILabel *_heroUptime;
    UILabel *_heroServer;
    SenkoCardView *_downCard;
    SenkoCardView *_upCard;
    UIImageView *_downIcon;
    UIImageView *_upIcon;
    UILabel *_downCaption;
    UILabel *_upCaption;
    UILabel *_downValue;
    UILabel *_upValue;
    UILabel *_downRateLabel;
    UILabel *_upRateLabel;
    UILabel *_subsHeader;
    NSMutableArray *_subCards;
    SenkoControl *_ctl;
    SenkoNativeVPN *_native;
    NSTimer *_timer;
    NSUInteger _generation;
    BOOL _polling;
    CFTimeInterval _pollAt;
    NSString *_state;
    NSString *_serverName;
    NSArray *_subs;
    long _uptime;
    CFTimeInterval _uptimeAt;
    BOOL _uptimeKnown;
    BOOL _trafficKnown;
    uint64_t _up;
    uint64_t _down;
    double _upRate;
    double _downRate;
    CFTimeInterval _sampleAt;
    BOOL _awg;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_timer invalidate];
    [_scroll release];
    [_hero release];
    [_heroDot release];
    [_heroState release];
    [_heroUptime release];
    [_heroServer release];
    [_downCard release];
    [_upCard release];
    [_downIcon release];
    [_upIcon release];
    [_downCaption release];
    [_upCaption release];
    [_downValue release];
    [_upValue release];
    [_downRateLabel release];
    [_upRateLabel release];
    [_subsHeader release];
    [_subCards release];
    [_ctl release];
    [_native release];
    [_state release];
    [_serverName release];
    [_subs release];
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"Statistics");
    _ctl = [[SenkoControl alloc] initWithSocketPath:SENKO_SOCK];
    if ([SenkoNativeVPN available]) _native = [[SenkoNativeVPN alloc] init];
    _state = [@"idle" copy];
    _subCards = [[NSMutableArray alloc] init];
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setAutomaticallyAdjustsScrollViewInsets:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setAutomaticallyAdjustsScrollViewInsets:), NO);
    self.navigationItem.leftBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                       target:self
                                                       action:@selector(donePressed)] autorelease];
    SenkoApplyScreenChrome(self.view);

    _scroll = [[UIScrollView alloc] initWithFrame:SenkoViewBounds(self.view)];
    _scroll.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _scroll.alwaysBounceVertical = YES;
    _scroll.backgroundColor = [UIColor clearColor];
    [self.view addSubview:_scroll];

    _hero = [[SenkoCardView alloc] initWithFrame:CGRectZero];
    [_scroll addSubview:_hero];
    _heroDot = [[UIView alloc] initWithFrame:CGRectZero];
    _heroDot.layer.cornerRadius = 5.0f;
    [_hero addSubview:_heroDot];
    _heroState = [SenkoCardLabel(_hero, SenkoFontBody(15.0f, YES), kInkMuted,
                                 NSTextAlignmentLeft) retain];
    _heroUptime = [SenkoCardLabel(_hero, SenkoFontDisplay(44.0f), kInk,
                                  NSTextAlignmentLeft) retain];
    _heroUptime.adjustsFontSizeToFitWidth = YES;
    _heroServer = [SenkoCardLabel(_hero, SenkoFontBody(15.0f, NO), kInkMuted,
                                  NSTextAlignmentLeft) retain];
    _heroServer.lineBreakMode = NSLineBreakByTruncatingTail;

    _downCard = [[SenkoCardView alloc] initWithFrame:CGRectZero];
    _upCard = [[SenkoCardView alloc] initWithFrame:CGRectZero];
    [_scroll addSubview:_downCard];
    [_scroll addSubview:_upCard];
    _downIcon = [[UIImageView alloc] initWithFrame:CGRectZero];
    _upIcon = [[UIImageView alloc] initWithFrame:CGRectZero];
    [_downCard addSubview:_downIcon];
    [_upCard addSubview:_upIcon];
    _downCaption = [SenkoCardLabel(_downCard, SenkoFontBody(14.0f, YES), kInkMuted,
                                   NSTextAlignmentLeft) retain];
    _upCaption = [SenkoCardLabel(_upCard, SenkoFontBody(14.0f, YES), kInkMuted,
                                 NSTextAlignmentLeft) retain];
    _downValue = [SenkoCardLabel(_downCard, SenkoFontBody(26.0f, YES), kInk,
                                 NSTextAlignmentLeft) retain];
    _upValue = [SenkoCardLabel(_upCard, SenkoFontBody(26.0f, YES), kInk,
                               NSTextAlignmentLeft) retain];
    _downValue.adjustsFontSizeToFitWidth = YES;
    _upValue.adjustsFontSizeToFitWidth = YES;
    _downRateLabel = [SenkoCardLabel(_downCard, SenkoFontBody(14.0f, NO), kInkMuted,
                                     NSTextAlignmentLeft) retain];
    _upRateLabel = [SenkoCardLabel(_upCard, SenkoFontBody(14.0f, NO), kInkMuted,
                                   NSTextAlignmentLeft) retain];

    _subsHeader = [SenkoCardLabel(_scroll, SenkoFontBody(14.0f, YES), kInkMuted,
                                  NSTextAlignmentLeft) retain];
    _subsHeader.hidden = YES;

    [self applyColors];
    [self refreshValues];
    [self layoutStats];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification object:nil];
}

/* the text, dot and icons take their colours from the palette, which a theme
   switch replaces */
- (void)applyColors {
    [_hero restyle];
    [_downCard restyle];
    [_upCard restyle];
    _heroState.textColor = kInkMuted;
    _heroUptime.textColor = kInk;
    _heroServer.textColor = kInkMuted;
    _downCaption.textColor = _upCaption.textColor = kInkMuted;
    _downValue.textColor = _upValue.textColor = kInk;
    _downRateLabel.textColor = _upRateLabel.textColor = kInkMuted;
    _subsHeader.textColor = kInkMuted;
    _downCaption.text = SenkoLocalizedText(@"Downloaded");
    _upCaption.text = SenkoLocalizedText(@"Uploaded");
    _downIcon.image = TintedIconNamed(@"glyph-arrow-down.png", 20.0f, kConnOn);
    _upIcon.image = TintedIconNamed(@"glyph-arrow-up.png", 20.0f, kAccentBlue);
    _subsHeader.text = SenkoLocalizedText(@"Subscriptions");
    for (SenkoStatsSubCard *card in _subCards) [card restyle];
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    SenkoApplyScreenChrome(self.view);
    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    [self applyColors];
    [self rebuildSubscriptionCards];
    [self refreshValues];
    [self layoutStats];
}

- (void)rebuildSubscriptionCards {
    for (SenkoStatsSubCard *card in _subCards) [card removeFromSuperview];
    [_subCards removeAllObjects];
    for (SenkoSub *sub in _subs) {
        SenkoStatsSubCard *card = [[[SenkoStatsSubCard alloc] initWithFrame:CGRectZero] autorelease];
        [card fillWith:sub];
        [_scroll addSubview:card];
        [_subCards addObject:card];
    }
    _subsHeader.hidden = [_subCards count] == 0;
}

- (void)layoutStats {
    CGRect b = SenkoViewBounds(self.view);
    if (b.size.width < 2.0f) return;
    UIEdgeInsets safe = SenkoSafeAreaInsets(self.view);
    CGFloat col = SenkoCardColumnWidth(b.size.width);
    CGFloat x = floorf((b.size.width - col) * 0.5f);
    CGFloat y = 16.0f;

    _hero.frame = CGRectMake(x, y, col, 132.0f);
    _heroDot.frame = CGRectMake(18.0f, 21.0f, 10.0f, 10.0f);
    _heroState.frame = CGRectMake(36.0f, 15.0f, col - 54.0f, 22.0f);
    _heroUptime.frame = CGRectMake(16.0f, 40.0f, col - 32.0f, 52.0f);
    _heroServer.frame = CGRectMake(18.0f, 98.0f, col - 36.0f, 22.0f);
    y += 132.0f + kStatsGap;

    CGFloat half = floorf((col - kStatsGap) * 0.5f);
    CGRect left = CGRectMake(x, y, half, 112.0f);
    CGRect right = CGRectMake(x + col - half, y, half, 112.0f);
    _downCard.frame = left;
    _upCard.frame = right;
    UIView *cards[2] = { _downCard, _upCard };
    UIImageView *icons[2] = { _downIcon, _upIcon };
    UILabel *caps[2] = { _downCaption, _upCaption };
    UILabel *vals[2] = { _downValue, _upValue };
    UILabel *rates[2] = { _downRateLabel, _upRateLabel };
    for (int i = 0; i < 2; ++i) {
        CGFloat w = cards[i].bounds.size.width;
        icons[i].frame = CGRectMake(14.0f, 14.0f, 20.0f, 20.0f);
        caps[i].frame = CGRectMake(40.0f, 13.0f, w - 52.0f, 22.0f);
        vals[i].frame = CGRectMake(14.0f, 44.0f, w - 28.0f, 32.0f);
        rates[i].frame = CGRectMake(14.0f, 80.0f, w - 28.0f, 20.0f);
    }
    y += 112.0f + kStatsGap;

    if ([_subCards count]) {
        _subsHeader.frame = CGRectMake(x + 4.0f, y + 4.0f, col - 8.0f, 20.0f);
        y += 32.0f;
        for (SenkoStatsSubCard *card in _subCards) {
            card.frame = CGRectMake(x, y, col, 86.0f);
            y += 86.0f + kStatsGap;
        }
    }
    _scroll.contentSize = CGSizeMake(b.size.width, y + 8.0f + safe.bottom);
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    _scroll.frame = SenkoViewBounds(self.view);
    [self layoutStats];
}

/* everything that changes once a second is a label: nothing is rebuilt */
- (void)refreshValues {
    BOOL connected = [_state isEqualToString:@"connected"];
    BOOL connecting = [_state isEqualToString:@"connecting"];
    _heroDot.backgroundColor = connected ? kConnOn
        : connecting ? [UIColor colorWithRed:0.95f green:0.65f blue:0.15f alpha:1.0f]
        : kIdleGrey;
    _heroState.text = connected ? SenkoLocalizedText(@"Connected")
        : connecting ? SenkoLocalizedText(@"Connecting") : SenkoLocalizedText(@"Disconnected");
    NSString *uptime = nil;
    if (_uptimeKnown)
        uptime = SenkoStatsUptimeText(_uptime + (long)(CACurrentMediaTime() - _uptimeAt));
    _heroUptime.text = uptime ? uptime : @"—";
    NSString *server = _awg ? @"AmneziaWG" : ([_serverName length] ? _serverName : nil);
    _heroServer.text = connected && server ? server : (server ? server : @"—");
    _downValue.text = _trafficKnown ? SenkoFormatBytes(_down) : @"—";
    _upValue.text = _trafficKnown ? SenkoFormatBytes(_up) : @"—";
    _downRateLabel.text = _trafficKnown ? SenkoFormatRate(_downRate) : @"—";
    _upRateLabel.text = _trafficKnown ? SenkoFormatRate(_upRate) : @"—";
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("statistics");
    [super viewWillAppear:animated];
    [_ctl listCatalog:^(NSArray *servers, NSArray *subs, NSArray *order) {
        (void)order;
        [_subs release];
        _subs = [subs copy];
        for (SenkoServer *sv in servers) {
            if (!sv->selected) continue;
            NSString *name = SenkoServerDisplayName(sv->remark);
            [_serverName release];
            _serverName = [([name length] ? name : sv->host) copy];
            break;
        }
        [self rebuildSubscriptionCards];
        [self refreshValues];
        [self layoutStats];
    }];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self poll];
    [_timer invalidate];
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self
                                            selector:@selector(poll)
                                            userInfo:nil repeats:YES];
}

/* the timer holds this controller, so it goes with the screen */
- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [_timer invalidate];
    _timer = nil;
    _generation++;
    _polling = NO;
}

- (void)donePressed {
    SenkoDismissModal(self, nil);
}

- (void)setStateText:(NSString *)state {
    [_state release];
    _state = [(state ? state : @"idle") copy];
}

- (void)applyTrafficKnown:(BOOL)known up:(uint64_t)up down:(uint64_t)down {
    CFTimeInterval now = CACurrentMediaTime();
    if (known && _trafficKnown && _sampleAt > 0.0 && now - _sampleAt > 0.2 &&
        up >= _up && down >= _down) {
        _upRate = (double)(up - _up) / (now - _sampleAt);
        _downRate = (double)(down - _down) / (now - _sampleAt);
    } else if (!known) {
        _upRate = _downRate = 0.0;
    }
    _trafficKnown = known;
    _up = up;
    _down = down;
    _sampleAt = known ? now : 0.0;
}

/* one status and one counter request per second at most: a poll still in
   flight when the timer fires again is not doubled up, unless it has been
   silent for long enough that its reply is not coming (a packet tunnel
   extension that restarted never answers its provider message) */
- (void)poll {
    if (_polling) {
        if (CACurrentMediaTime() - _pollAt < 5.0) return;
        _polling = NO;
    }
    _polling = YES;
    _pollAt = CACurrentMediaTime();
    NSUInteger generation = ++_generation;
    /* a negative uptime is a backend that keeps no clock */
    void (^gotState)(NSString *, long, BOOL) = ^(NSString *state, long uptime, BOOL viaNative) {
        if (generation != _generation) return;
        [self setStateText:state];
        BOOL connected = [state isEqualToString:@"connected"];
        _uptimeKnown = connected && uptime >= 0;
        _uptime = uptime;
        _uptimeAt = CACurrentMediaTime();
        if (!connected) {
            _polling = NO;
            [self applyTrafficKnown:NO up:0 down:0];
            [self refreshValues];
            return;
        }
        void (^received)(BOOL, uint64_t, uint64_t) = ^(BOOL known, uint64_t up, uint64_t down) {
            if (generation != _generation) return;
            _polling = NO;
            [self applyTrafficKnown:known up:up down:down];
            [self refreshValues];
        };
        if (viaNative) [_native traffic:received];
        else [_ctl traffic:received];
    };
    /* the daemon carries the amneziawg tunnel and, where no packet tunnel
       extension exists, the server tunnel. its counters are the running
       backend's whichever it is */
    void (^askDaemon)(void) = ^{
        [_ctl statusStateWithUptime:^(NSString *state, long uptime) {
            if ([state isEqualToString:@"connected"] || [state isEqualToString:@"connecting"]) {
                _awg = NO;
                gotState(state, uptime, NO);
                return;
            }
/* the server backend idles while amneziawg runs the tunnel, so the profile's
   own status is the one to show then */
            [_ctl awgStatusWithUptime:^(NSString *awg, long awgUptime) {
                NSString *clean = [awg stringByTrimmingCharactersInSet:
                                   [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                _awg = [clean isEqualToString:@"connected"] || [clean isEqualToString:@"connecting"];
                gotState(_awg ? clean : (state ? state : @"idle"), _awg ? awgUptime : -1, NO);
            }];
        }];
    };
    if (!_native) {
        askDaemon();
        return;
    }
    [_native status:^(NSInteger status, NSDate *connectedDate) {
        BOOL live = status == 3 || status == 2 || status == 4;
        if (!live) {
            askDaemon();
            return;
        }
        _awg = NO;
        long uptime = connectedDate ? (long)(-[connectedDate timeIntervalSinceNow]) : 0;
        gotState(status == 3 ? @"connected" : @"connecting", uptime, YES);
    }];
}

@end
