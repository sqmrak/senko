#import "server_cell.h"
#import "app_common.h"
#import "ui_theme.h"

#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>
#include <arpa/inet.h>

static BOOL SenkoCellRegional(unichar c) {
    return c >= 0xDDE6 && c <= 0xDDFF;
}

static NSString *SenkoCellFlagCode(NSString *flag);
static NSString *SenkoCellRemark(NSString *raw);

static BOOL SenkoCellTechnicalSuffix(NSString *suffix) {
    if (![suffix length]) return NO;
    NSCharacterSet *separators = [NSCharacterSet characterSetWithCharactersInString:@" /+|,"];
    NSArray *parts = [suffix componentsSeparatedByCharactersInSet:separators];
    BOOL found = NO;
    for (NSString *part in parts) {
        NSString *token = [[part stringByTrimmingCharactersInSet:
                            [NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
        if (![token length]) continue;
        if (![token isEqualToString:@"grpc"] &&
            ![token isEqualToString:@"h2"] &&
            ![token isEqualToString:@"http2"] &&
            ![token isEqualToString:@"ws"] &&
            ![token isEqualToString:@"websocket"] &&
            ![token isEqualToString:@"tcp"] &&
            ![token isEqualToString:@"tls"] &&
            ![token isEqualToString:@"reality"] &&
            ![token isEqualToString:@"xhttp"] &&
            ![token isEqualToString:@"vless"] &&
            ![token isEqualToString:@"trojan"] &&
            ![token isEqualToString:@"ss"] &&
            ![token isEqualToString:@"shadowsocks"] &&
            ![token isEqualToString:@"vmess"] &&
            ![token isEqualToString:@"tuic"] &&
            ![token isEqualToString:@"hy2"] &&
            ![token isEqualToString:@"hysteria2"])
            return NO;
        found = YES;
    }
    return found;
}

static NSString *SenkoCellFlag(NSString *raw) {
    if (![raw length]) return nil;
    NSString *text = [raw stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
    if (!text) text = raw;
    for (NSUInteger i = 0; i + 3 < [text length]; ++i) {
        unichar a = [text characterAtIndex:i];
        unichar b = [text characterAtIndex:i + 1];
        unichar c = [text characterAtIndex:i + 2];
        unichar d = [text characterAtIndex:i + 3];
        if (a == 0xD83C && c == 0xD83C &&
            SenkoCellRegional(b) && SenkoCellRegional(d))
            return [text substringWithRange:NSMakeRange(i, 4)];
    }
    return nil;
}

NSString *SenkoServerFlagCode(NSString *remark) {
    return SenkoCellFlagCode(SenkoCellFlag(remark));
}

NSString *SenkoServerDisplayName(NSString *remark) {
    return SenkoCellRemark(remark);
}

static NSString *SenkoCellFlagCode(NSString *flag) {
    if ([flag length] != 4) return nil;
    unichar a = [flag characterAtIndex:1];
    unichar b = [flag characterAtIndex:3];
    if (!SenkoCellRegional(a) || !SenkoCellRegional(b)) return nil;
    return [NSString stringWithFormat:@"%c%c",
            (char)('a' + a - 0xDDE6),
            (char)('a' + b - 0xDDE6)];
}

static NSCache *gFlagImageCache = nil;

static UIImage *SenkoCellCachedImage(NSString *name) {
    if (!name || ![name length]) return nil;
    if (!gFlagImageCache) {
        gFlagImageCache = [[NSCache alloc] init];
        [gFlagImageCache setCountLimit:128];
    }
    UIImage *img = [gFlagImageCache objectForKey:name];
    if (img) return img;
    img = [UIImage imageNamed:name];
    if (img) [gFlagImageCache setObject:img forKey:name];
    return img;
}

static BOOL SenkoCellHasFlag(NSString *raw) {
    NSString *code = SenkoCellFlagCode(SenkoCellFlag(raw));
    return [code length] &&
           SenkoCellCachedImage([NSString stringWithFormat:@"flag-%@.png", code]) != nil;
}

static const CGFloat kSenkoBadgeRadius = 8.0f;
static NSCache *gBadgeCache = nil;

/* the sheen and the rim are baked into one bitmap: a row keeps a single image
   layer, and scrolling on an armv7 device composites nothing extra */
static UIImage *SenkoGlossyTile(UIImage *src, CGFloat side) {
    if (!src || side < 1.0f || src.size.width < 1.0f || src.size.height < 1.0f)
        return src;
    CGRect r = CGRectMake(0.0f, 0.0f, side, side);
    UIGraphicsBeginImageContextWithOptions(r.size, NO, 0.0f);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    UIBezierPath *tile = [UIBezierPath bezierPathWithRoundedRect:r
                                                    cornerRadius:kSenkoBadgeRadius];
    CGContextSaveGState(ctx);
    [tile addClip];
    CGFloat scale = MAX(side / src.size.width, side / src.size.height);
    CGFloat w = src.size.width * scale;
    CGFloat h = src.size.height * scale;
    [src drawInRect:CGRectMake((side - w) * 0.5f, (side - h) * 0.5f, w, h)];

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGFloat colors[8] = { 1.0f, 1.0f, 1.0f, 0.42f, 1.0f, 1.0f, 1.0f, 0.03f };
    CGFloat stops[2] = { 0.0f, 1.0f };
    CGGradientRef sheen = CGGradientCreateWithColorComponents(space, colors, stops, 2);
    CGColorSpaceRelease(space);
    if (sheen) {
        CGContextSaveGState(ctx);
        CGContextClipToRect(ctx, CGRectMake(0.0f, 0.0f, side, floorf(side * 0.5f)));
        CGContextDrawLinearGradient(ctx, sheen, CGPointMake(0.0f, 0.0f),
                                    CGPointMake(0.0f, side * 0.5f), 0);
        CGContextRestoreGState(ctx);
        CGGradientRelease(sheen);
    }
    CGContextRestoreGState(ctx);

    UIBezierPath *rim = [UIBezierPath bezierPathWithRoundedRect:CGRectInset(r, 0.5f, 0.5f)
                                                   cornerRadius:kSenkoBadgeRadius - 0.5f];
    rim.lineWidth = 1.0f;
    [[UIColor colorWithWhite:0.0f alpha:0.18f] setStroke];
    [rim stroke];
    UIImage *out = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return out ? out : src;
}

UIImage *SenkoServerBadgeImage(NSString *remark, CGFloat side) {
    NSString *code = SenkoCellHasFlag(remark) ? SenkoCellFlagCode(SenkoCellFlag(remark)) : nil;
    NSString *name = [code length] ? [NSString stringWithFormat:@"flag-%@.png", code]
                                   : @"server-placeholder.png";
    NSString *key = [NSString stringWithFormat:@"%@@%.0f", name, side];
    if (!gBadgeCache) {
        gBadgeCache = [[NSCache alloc] init];
        [gBadgeCache setCountLimit:96];
    }
    UIImage *hit = [gBadgeCache objectForKey:key];
    if (hit) return hit;
    UIImage *tile = SenkoGlossyTile(SenkoCellCachedImage(name), side);
    if (tile) [gBadgeCache setObject:tile forKey:key];
    return tile;
}

void SenkoStyleBadgeShadow(UIView *view, CGFloat radius) {
    view.clipsToBounds = NO;
    view.layer.masksToBounds = NO;
    view.layer.cornerRadius = 0.0f;
    view.backgroundColor = [UIColor clearColor];
    view.layer.borderWidth = 0.0f;
    view.layer.shadowColor = [UIColor blackColor].CGColor;
    view.layer.shadowOpacity = 0.35f;
    view.layer.shadowRadius = 2.0f;
    view.layer.shadowOffset = CGSizeMake(0.0f, 1.0f);
    if (view.bounds.size.width > 1.0f)
        view.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:view.bounds
                                                           cornerRadius:radius].CGPath;
}

static NSString *SenkoCellRemark(NSString *raw) {
    if (![raw length]) return @"";
    NSString *text = [raw stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
    if (!text) text = raw;
    for (NSUInteger i = 0; i + 3 < [text length]; ++i) {
        unichar a = [text characterAtIndex:i];
        unichar b = [text characterAtIndex:i + 1];
        unichar c = [text characterAtIndex:i + 2];
        unichar d = [text characterAtIndex:i + 3];
        if (a != 0xD83C || c != 0xD83C || !SenkoCellRegional(b) || !SenkoCellRegional(d))
            continue;
        text = [text stringByReplacingCharactersInRange:NSMakeRange(i, 4) withString:@""];
        break;
    }
    text = [text stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    for (;;) {
        NSRange close = [text rangeOfString:@")" options:NSBackwardsSearch];
        if (close.location == NSNotFound || close.location + 1 != [text length]) break;
        NSRange open = [text rangeOfString:@"(" options:NSBackwardsSearch
                                      range:NSMakeRange(0, close.location)];
        if (open.location == NSNotFound || open.location == 0) break;
        NSString *suffix = [text substringWithRange:
                            NSMakeRange(open.location + 1, close.location - open.location - 1)];
        if (!SenkoCellTechnicalSuffix(suffix)) break;
        /* subscriptions append transport labels to a city, but the endpoint
           retains those values separately and the list should group by city */
        text = [[text substringToIndex:open.location]
                stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    }
    /* some panels append the resolved ipv4 address to a country name. the
       endpoint is already shown on the next line, so keeping it here creates
       four visible copies of one country when a balancer rotates addresses */
    NSRange split = [text rangeOfCharacterFromSet:
                     [NSCharacterSet whitespaceCharacterSet]
                                             options:NSBackwardsSearch];
    if (split.location != NSNotFound && split.location + 1 < [text length]) {
        NSString *tail = [text substringFromIndex:split.location + 1];
        NSRange colon = [tail rangeOfString:@":" options:NSBackwardsSearch];
        if (colon.location != NSNotFound && colon.location + 1 < [tail length]) {
            NSString *port = [tail substringFromIndex:colon.location + 1];
            if ([[port stringByTrimmingCharactersInSet:
                  [NSCharacterSet decimalDigitCharacterSet]] length] == 0)
                tail = [tail substringToIndex:colon.location];
        }
        struct in_addr address;
        if (inet_pton(AF_INET, [tail UTF8String], &address) == 1)
            text = [[text substringToIndex:split.location]
                    stringByTrimmingCharactersInSet:
                        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    }
    return text;
}

NSString *SenkoServerProtocolLabel(SenkoServer *server) {
    if ([server->proto isEqualToString:@"vless"] || [server->proto isEqualToString:@"vmess"])
        return [NSString stringWithFormat:@"%@/%@/%@",
                server->proto ? server->proto : @"vless",
                server->net ? server->net : @"tcp",
                server->security ? server->security : @"none"];
    if ([server->proto isEqualToString:@"trojan"])
        return [NSString stringWithFormat:@"%@/%@/%@",
                server->proto,
                server->net ? server->net : @"tcp",
                server->security ? server->security : @"tls"];
    if ([server->proto isEqualToString:@"shadowsocks"] || [server->proto isEqualToString:@"ss"])
        return [NSString stringWithFormat:@"ss/%@",
                server->security ? server->security : @"aead"];
    if ([server->proto isEqualToString:@"hysteria2"])
        return @"hysteria2/quic";
    /* senkod sends a placeholder's reason with spaces turned into dashes */
    if (!server->supported && server->proto)
        return [server->proto stringByReplacingOccurrencesOfString:@"-" withString:@" "];
    return server->proto ? server->proto : @"unknown";
}

static UIColor *SenkoUnsupportedTint(void) {
    return SenkoThemeIsLight()
        ? [UIColor colorWithRed:0.72 green:0.10 blue:0.08 alpha:1.0]
        : [UIColor colorWithRed:1.0 green:0.45 blue:0.36 alpha:1.0];
}

/* the row itself no longer shows this: a named server already carries its
   name up top, and the raw endpoint added nothing beside the protocol line.
   it survives only as the fallback title for a server with no remark, where
   host:port is the one thing left to call it */
static NSString *ServerEndpointLabel(SenkoServer *server) {
    return [NSString stringWithFormat:@"%@:%d",
            server->host ? server->host : @"", server->port];
}

/* a standard spinner identifies the one socket operation that is active. the
   daemon handles checks serially on ios 6, so animating every row suggested
   work that had not started and cost needless compositing time. */
static void SenkoSetRowChecking(UIActivityIndicatorView *activity, BOOL checking) {
    if (!activity) return;
    if (checking) [activity startAnimating];
    else [activity stopAnimating];
}

@implementation SenkoRadioView

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.backgroundColor = [UIColor clearColor];
        self.userInteractionEnabled = NO;
        _ring = [[CAShapeLayer alloc] init];
        _ring.fillColor = [UIColor clearColor].CGColor;
        _ring.lineWidth = 1.5f;
        [self.layer addSublayer:_ring];
        _dot = [[CAShapeLayer alloc] init];
        [self.layer addSublayer:_dot];
        [self applyTheme];
    }
    return self;
}

- (void)dealloc {
    [_ring release];
    [_dot release];
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    CGFloat side = MIN(b.size.width, b.size.height);
    CGPoint c = CGPointMake(CGRectGetMidX(b), CGRectGetMidY(b));
    SenkoBeginSilentLayers();
    _ring.frame = b;
    _dot.frame = b;
    _ring.path = [UIBezierPath bezierPathWithArcCenter:c radius:side * 0.5f - 1.0f
        startAngle:0.0f endAngle:(CGFloat)(M_PI * 2.0) clockwise:YES].CGPath;
    _dot.path = [UIBezierPath bezierPathWithArcCenter:c radius:side * 0.25f
        startAngle:0.0f endAngle:(CGFloat)(M_PI * 2.0) clockwise:YES].CGPath;
    SenkoEndSilentLayers();
}

- (void)applyTheme {
    SenkoBeginSilentLayers();
    _ring.strokeColor = (_checked ? kAccentBlue
                                  : [kInkMuted colorWithAlphaComponent:0.55f]).CGColor;
    _ring.lineWidth = _checked ? 2.0f : 1.5f;
    _dot.fillColor = kAccentBlue.CGColor;
    _dot.hidden = !_checked;
    SenkoEndSilentLayers();
}

- (void)setChecked:(BOOL)checked {
    _checked = checked;
    [self applyTheme];
}

@end

@implementation SenkoSignalBars

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.backgroundColor = [UIColor clearColor];
        self.userInteractionEnabled = NO;
        for (int i = 0; i < 4; ++i) {
            _bars[i] = [[CALayer alloc] init];
            _bars[i].cornerRadius = 1.0f;
            [self.layer addSublayer:_bars[i]];
        }
    }
    return self;
}

- (void)dealloc {
    for (int i = 0; i < 4; ++i) [_bars[i] release];
    [_tint release];
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    CGFloat gap = 2.0f;
    CGFloat w = floorf((b.size.width - gap * 3.0f) / 4.0f);
    if (w < 2.0f) w = 2.0f;
    SenkoBeginSilentLayers();
    for (int i = 0; i < 4; ++i) {
        CGFloat h = floorf(b.size.height * (0.4f + 0.2f * (CGFloat)i));
        _bars[i].frame = CGRectMake((w + gap) * (CGFloat)i, b.size.height - h, w, h);
    }
    SenkoEndSilentLayers();
}

/* the thresholds follow what a proxied page load feels like, not raw tcp: past
   350 ms a handshake plus a request is already a visible wait */
- (void)setLatency:(NSNumber *)ms {
    int value = ms ? [ms intValue] : -1;
    UIColor *tint;
    if (value < 0) {
        _level = 0;
        tint = [UIColor colorWithRed:0.95 green:0.30 blue:0.26 alpha:1.0];
    } else if (value < 350) {
        _level = value < 100 ? 4 : (value < 200 ? 3 : 2);
        tint = value < 200 ? kConnOn
                           : [UIColor colorWithRed:1.0 green:0.62 blue:0.16 alpha:1.0];
    } else {
        _level = 1;
        tint = [UIColor colorWithRed:1.0 green:0.45 blue:0.20 alpha:1.0];
    }
    [_tint release];
    _tint = [tint retain];
    SenkoBeginSilentLayers();
    for (int i = 0; i < 4; ++i)
        _bars[i].backgroundColor = (i < _level ? _tint
                                   : [_tint colorWithAlphaComponent:0.22f]).CGColor;
    SenkoEndSilentLayers();
}

@end

@implementation ServerCell {
    BOOL _picked;
    BOOL _plateSized;
}

- (id)initWithStyle:(UITableViewCellStyle)style reuseIdentifier:(NSString *)reuseIdentifier {
    if ((self = [super initWithStyle:style reuseIdentifier:reuseIdentifier])) {
        self.backgroundColor = [UIColor clearColor];
        self.selectionStyle = UITableViewCellSelectionStyleNone;
/* opaque labels avoid blend overdraw */
        self.opaque = NO;
        self.contentView.opaque = NO;

        _plate = [[UIView alloc] initWithFrame:CGRectZero];
        _plate.layer.cornerRadius = SenkoThemeCardRadius();
        _plate.layer.masksToBounds = YES; /* clip fill to rounded plate */
        _plate.layer.borderWidth = SenkoThemeIsIos16() ? 0 : 0.5f;
        _plate.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.10].CGColor;
        _plate.layer.shadowOpacity = 0.0f;
        _plate.layer.shadowRadius = 0;
        _plate.layer.shadowPath = nil;
/* cached plates reduce blend work while classic lists scroll */
        _plate.layer.shouldRasterize = YES;
        if (_plate.layer.shouldRasterize)
            _plate.layer.rasterizationScale = [UIScreen mainScreen].scale;
        /* asynchronous layer drawing can publish an old raster after a reused
           cell has already been rebound under a different theme */
        SEL asyncSel = @selector(setDrawsAsynchronously:);
        if ([_plate.layer respondsToSelector:asyncSel])
            ((void (*)(id, SEL, BOOL))objc_msgSend)(_plate.layer, asyncSel, NO);
        _plateGrad = [CAGradientLayer layer];
        _plateGrad.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                              [NSNull null], @"colors",
                              [NSNull null], @"bounds",
                              [NSNull null], @"position", nil];
        [_plate.layer insertSublayer:_plateGrad atIndex:0];
        _plate.layer.cornerRadius = 0.0f;
        _plate.layer.masksToBounds = NO;
        _plate.layer.borderWidth = 0.0f;
        _corners = [[CAShapeLayer alloc] init];
        _plate.layer.mask = _corners;
        _rule = [[CALayer alloc] init];
        _rule.actions = [NSDictionary dictionaryWithObjectsAndKeys:
                         [NSNull null], @"bounds", [NSNull null], @"position",
                         [NSNull null], @"hidden", [NSNull null], @"backgroundColor", nil];
        [_plate.layer addSublayer:_rule];
        _groupFirst = YES;
        _groupLast = YES;
        [self.contentView addSubview:_plate];

        _serverIcon = [[UIImageView alloc] initWithFrame:CGRectZero];
        _serverIcon.contentMode = UIViewContentModeScaleToFill;
        _serverIcon.image = SenkoServerBadgeImage(nil, 32.0f);
        [_plate addSubview:_serverIcon];

        _title = [[UILabel alloc] initWithFrame:CGRectZero];
        _title.backgroundColor = [UIColor clearColor];
        _title.font = SenkoThemeIsIos16()
            ? SenkoFontBody(15, YES)
            : [UIFont boldSystemFontOfSize:14];
        SenkoStyleInkLabel(_title);
        _title.numberOfLines = 1;
        _title.lineBreakMode = NSLineBreakByClipping;
        _title.adjustsFontSizeToFitWidth = YES;
        _title.minimumFontSize = 10.0f;
        [_plate addSubview:_title];

        _detail = [[UILabel alloc] initWithFrame:CGRectZero];
        _detail.backgroundColor = [UIColor clearColor];
        _detail.font = SenkoThemeIsIos16()
            ? SenkoFontBody(12, NO)
            : [UIFont systemFontOfSize:12];
        _detail.lineBreakMode = NSLineBreakByClipping;
        _detail.adjustsFontSizeToFitWidth = YES;
        _detail.minimumFontSize = 8.0f;
        SenkoStyleMutedLabel(_detail);
        [_plate addSubview:_detail];

        _transport = [[UILabel alloc] initWithFrame:CGRectZero];
        _transport.backgroundColor = [UIColor clearColor];
/* the protocol line took over the row the endpoint used to have and reads as
   the row's one line of secondary text now, not a cramped third line, so it
   gets the bump the endpoint's removal left room for */
        _transport.font = SenkoThemeIsIos16()
            ? SenkoFontBody(13, YES)
            : [UIFont boldSystemFontOfSize:13];
        _transport.lineBreakMode = NSLineBreakByClipping;
        _transport.adjustsFontSizeToFitWidth = YES;
        _transport.minimumFontSize = 8.0f;
        SenkoStyleMutedLabel(_transport);
        [_plate addSubview:_transport];

        _unsupported = [[UILabel alloc] initWithFrame:CGRectZero];
        _unsupported.backgroundColor = [UIColor clearColor];
        _unsupported.textColor = [UIColor colorWithRed:1.0 green:0.35 blue:0.28 alpha:1.0];
        _unsupported.font = [UIFont boldSystemFontOfSize:11];
        _unsupported.text = @"Senko does not support this protocol";
/* no emboss; stays readable on both themes */
        _unsupported.shadowColor = nil;
        _unsupported.shadowOffset = CGSizeZero;
        [_plate addSubview:_unsupported];

        _ping = [[UILabel alloc] initWithFrame:CGRectZero];
        _ping.backgroundColor = [UIColor clearColor];
        _ping.textAlignment = NSTextAlignmentRight;
        _ping.font = [UIFont boldSystemFontOfSize:13];
        _ping.lineBreakMode = NSLineBreakByClipping;
        _ping.adjustsFontSizeToFitWidth = YES;
        _ping.minimumFontSize = 9.0f;
        SenkoStyleAccentLabel(_ping);
        [_plate addSubview:_ping];

        _pingActivity = [[UIActivityIndicatorView alloc]
            initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhite];
        _pingActivity.hidesWhenStopped = YES;
        if ([_pingActivity respondsToSelector:@selector(setColor:)])
            _pingActivity.color = kAccentBlue;
        [_plate addSubview:_pingActivity];

        _bars = [[SenkoSignalBars alloc] initWithFrame:CGRectZero];
        _bars.hidden = YES;
        [_plate addSubview:_bars];

        _radio = [[SenkoRadioView alloc] initWithFrame:CGRectZero];
        [_plate addSubview:_radio];

        _pingButton = [[UIButton alloc] initWithFrame:CGRectZero];
        _pingButton.backgroundColor = [UIColor clearColor];
        _pingButton.accessibilityLabel = SenkoLocalizedText(@"Check ping");
        [_plate addSubview:_pingButton];

        _picked = NO;
        _plateSized = NO;
    }
    return self;
}

- (void)dealloc {
    [_plate release];
    [_rule release];
    [_corners release];
    [_title release];
    [_detail release];
    [_transport release];
    [_unsupported release];
    [_ping release];
    [_pingActivity release];
    [_pingButton release];
    [_serverIcon release];
    [_bars release];
    [_radio release];
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect plate = self.contentView.bounds;
    BOOL sizeChanged = !_plateSized || !CGSizeEqualToSize(_plate.bounds.size, plate.size);
    _plate.bounds = CGRectMake(0, 0, plate.size.width, plate.size.height);
    _plate.center = CGPointMake(CGRectGetMidX(plate), CGRectGetMidY(plate));
    SenkoBeginSilentLayers();
    if (sizeChanged) {
        _plateGrad.frame = _plate.bounds;
        _plateSized = YES;
    }
    [self layoutGroupCorners];
    SenkoEndSilentLayers();
    CGFloat iconSize = SenkoThemeIsIos16() ? 34.0f : 32.0f;
    CGFloat iconY = floorf((_plate.bounds.size.height - iconSize) * 0.5f);
    CGRect iconFrame = CGRectMake(12.0f, iconY, iconSize, iconSize);
    if (!CGRectEqualToRect(_serverIcon.frame, iconFrame)) {
        _serverIcon.frame = iconFrame;
        SenkoStyleBadgeShadow(_serverIcon, 8.0f);
    }
    CGFloat plateW = _plate.bounds.size.width;
    CGFloat plateH = _plate.bounds.size.height;
    CGFloat textX = 12.0f + iconSize + 12.0f;
    CGFloat hair = 1.0f / [UIScreen mainScreen].scale;
    SenkoBeginSilentLayers();
    _rule.frame = CGRectMake(textX, _plate.bounds.size.height - hair,
                             _plate.bounds.size.width - textX, hair);
    _rule.hidden = _groupLast;
    SenkoEndSilentLayers();
    /* the right cluster is the reading, its bars and the radio. the title has
       to stop before all three or it overprints them on a 320pt screen */
    CGFloat rowMid = floorf(plateH * 0.5f);
    CGFloat radioSide = 22.0f;
    CGFloat radioX = plateW - 14.0f - radioSide;
    _radio.frame = CGRectMake(radioX, rowMid - radioSide * 0.5f, radioSide, radioSide);
    CGFloat barsW = 16.0f;
    CGFloat barsX = radioX - 12.0f - barsW;
    _bars.frame = CGRectMake(barsX, rowMid - 7.0f, barsW, 14.0f);
    CGFloat pingW = 58.0f;
    CGFloat pingH = 20.0f;
    CGFloat pingX = barsX - 6.0f - pingW;
    _ping.frame = CGRectMake(pingX, rowMid - pingH * 0.5f, pingW, pingH);
    _pingActivity.frame = CGRectMake(barsX - 2.0f, rowMid - 9.0f, 18.0f, 18.0f);
    _pingButton.frame = CGRectMake(pingX - 6.0f, rowMid - 15.0f,
                                   radioX - pingX, 30.0f);
    CGFloat textW = pingX - textX - 8.0f;
    CGFloat detailW = textW;
    if (textW < 42.0f) textW = 42.0f;
    if (detailW < 42.0f) detailW = 42.0f;
    if (plateW >= 520.0f) {
/* a wide ipad row left a third of its width empty under the name, so the
   endpoint and the transport share one line there instead of stacking, and the
   whole block sits centred rather than pinned to the top edge */
        CGFloat extra = _unsupported.hidden ? 0.0f : 13.0f;
        CGFloat blockH = 24.0f + 16.0f + extra;
        CGFloat top = floorf((plateH - blockH) * 0.5f);
        if (top < 2.0f) top = 2.0f;
        _title.frame = CGRectMake(textX, top, textW, 22);
/* _detail and _transport share this one line: a server row leaves _detail
   empty and shows the protocol here, the amneziawg row does the opposite.
   they never both carry text, so the shared rect never has to be split */
        _detail.frame = CGRectMake(textX, top + 24.0f, detailW, 15);
        _transport.frame = CGRectMake(textX, top + 24.0f, detailW, 16);
        _unsupported.frame = CGRectMake(textX, top + 41.0f, detailW, 12);
        return;
    }
    CGFloat block = 22.0f + 17.0f + (_unsupported.hidden ? 0.0f : 13.0f);
    CGFloat top = floorf((plateH - block) * 0.5f);
    if (top < 2.0f) top = 2.0f;
    _title.frame = CGRectMake(textX, top, textW, 22);
    _detail.frame = CGRectMake(textX, top + 22.0f, detailW, 16);
    _transport.frame = CGRectMake(textX, top + 22.0f, detailW, 17);
    _unsupported.frame = CGRectMake(textX, top + 39.0f, detailW, 12);
}

/* one cut of the rounded card per position, rebuilt only when the row size or
   its place in the group changes. the plate is rasterized, so the mask costs
   nothing while the list scrolls */
- (void)layoutGroupCorners {
    CGSize size = _plate.bounds.size;
    if (size.width < 1.0f || size.height < 1.0f) return;
    if (CGSizeEqualToSize(size, _cornersSize) && _corners.path) return;
    _cornersSize = size;
    UIRectCorner round = 0;
    if (_groupFirst) round |= UIRectCornerTopLeft | UIRectCornerTopRight;
    if (_groupLast) round |= UIRectCornerBottomLeft | UIRectCornerBottomRight;
    CGFloat r = SenkoThemeCardRadius();
    if (r < 12.0f) r = 12.0f;
    _corners.frame = _plate.bounds;
    _corners.path = [UIBezierPath bezierPathWithRoundedRect:_plate.bounds
                                          byRoundingCorners:round
                                                cornerRadii:CGSizeMake(r, r)].CGPath;
}

- (void)setGroupFirst:(BOOL)first last:(BOOL)last {
    if (_groupFirst == first && _groupLast == last) return;
    _groupFirst = first;
    _groupLast = last;
    _corners.path = NULL;
    [self setNeedsLayout];
}

/* every row of a group shares one fill, so the group reads as one card and
   selection is left to the radio */
- (UIColor *)plateFill:(BOOL)pressed {
    BOOL light = SenkoThemeIsLight();
    UIColor *fill;
    if (SenkoThemeIsIos26()) {
        fill = [UIColor colorWithWhite:1.0f alpha:light ? 0.34f : 0.12f];
    } else {
        UIColor *card = kCellHi;
        if (SenkoThemeIsMiside() || SenkoThemeIsFrutigeraero() || SenkoThemeIsBoykisser())
            card = [kCellHi colorWithAlphaComponent:0.86f];
        fill = SenkoShadeColor(card, light ? -0.02f : -0.05f);
    }
    return pressed ? SenkoShadeColor(fill, light ? -0.06f : 0.06f) : fill;
}

- (void)applyPicked:(BOOL)picked {
    _picked = picked;
    BOOL light = SenkoThemeIsLight();
    SenkoBeginSilentLayers();
    SenkoRemoveFrost(_plate);
    _plate.layer.borderWidth = 0.0f;
    _plate.layer.shadowOpacity = 0.0f;
    _plate.layer.shadowPath = nil;
    _plate.backgroundColor = [UIColor clearColor];
    UIColor *fill = [self plateFill:NO];
    _plateGrad.colors = [NSArray arrayWithObjects:(id)fill.CGColor, (id)fill.CGColor, nil];
    _rule.backgroundColor = (light ? [UIColor colorWithWhite:0.0f alpha:0.10f]
                                   : [UIColor colorWithWhite:1.0f alpha:0.08f]).CGColor;
    [_radio setChecked:picked];
/* the classic plate has a gradient, three labels and embossed ink. redrawing
   that stack for each scroll tick is slower than keeping one bounded tile on
   armv7. glass stays unrasterized because its translucent material must
   sample the wallpaper behind it */
    _plate.layer.shouldRasterize = !SenkoThemeIsIos26();
    if (_plate.layer.shouldRasterize)
        _plate.layer.rasterizationScale = [UIScreen mainScreen].scale;
    SenkoEndSilentLayers();
}

/* the plate carries the theme's own card colour in every state, so the labels
   always take the theme ink; the on-dark variants belonged to the selected
   plate that no longer turns dark */
- (void)styleLabels {
    SenkoStyleInkLabel(_title);
    SenkoStyleMutedLabel(_detail);
    SenkoStyleMutedLabel(_transport);
}

/* a press darkens the row in place. scaling one row of a grouped card pulled
   it out of line with its neighbours, which is what read as a shaky list */
- (void)setHighlighted:(BOOL)highlighted animated:(BOOL)animated {
    [super setHighlighted:highlighted animated:animated];
    UIColor *fill = [self plateFill:highlighted];
    SenkoBeginSilentLayers();
    _plateGrad.colors = [NSArray arrayWithObjects:(id)fill.CGColor, (id)fill.CGColor, nil];
    SenkoEndSilentLayers();
}

- (void)configureWithServer:(SenkoServer *)server
                      picked:(BOOL)picked
                     pingVal:(NSNumber *)ping
                 displayName:(NSString *)displayName {
    [self applyPicked:picked];
/* restyle each bind; reuse may outlive theme switch */
    [self styleLabels];
    _serverIcon.image = SenkoServerBadgeImage(server->remark,
                                              SenkoThemeIsIos16() ? 34.0f : 32.0f);
    NSString *title = [displayName length] ? displayName
        : ([server->remark length] ? SenkoCellRemark(server->remark)
                                   : ServerEndpointLabel(server));
    _title.text = title;
    _detail.text = nil;
    _transport.text = SenkoServerProtocolLabel(server);
    _unsupported.hidden = server->supported;
    if (!server->supported) {
        _unsupported.textColor = SenkoUnsupportedTint();
        _title.textColor = SenkoUnsupportedTint();
        _transport.textColor = SenkoUnsupportedTint();
    }

    BOOL checking = ping && [ping intValue] == -3;
    _bars.hidden = !ping || checking;
    if (!_bars.hidden) [_bars setLatency:ping];
    if ([_pingActivity respondsToSelector:@selector(setColor:)])
        _pingActivity.color = kAccentBlue;
    SenkoSetRowChecking(_pingActivity, checking);
    if (!ping) {
        _ping.text = picked ? @"   " : @"";
        SenkoStyleAccentLabel(_ping);
    } else if ([ping intValue] >= 0) {
        _ping.text = [NSString stringWithFormat:SenkoLocalizedText(@"%d ms"), [ping intValue]];
        SenkoStyleMutedLabel(_ping);
    } else if ([ping intValue] == -3) {
        _ping.text = @"";
        SenkoStyleAccentLabel(_ping);
    } else {
        _ping.text = SenkoLocalizedText(@"Timeout");
        _ping.textColor = SenkoThemeIsLight()
            ? [UIColor colorWithRed:0.72 green:0.10 blue:0.08 alpha:1.0]
            : [UIColor colorWithRed:1.0 green:0.45 blue:0.36 alpha:1.0];
        _ping.shadowColor = nil;
        _ping.shadowOffset = CGSizeZero;
    }
}

- (void)setPingTarget:(id)target action:(SEL)action serverIndex:(int)serverIndex {
    [_pingButton removeTarget:nil action:NULL forControlEvents:UIControlEventTouchUpInside];
    _pingButton.tag = serverIndex;
    _pingButton.hidden = !target || !action || serverIndex < 0;
    if (!_pingButton.hidden) {
        NSString *shown = [_ping.text stringByTrimmingCharactersInSet:
                           [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        BOOL checking = [_pingActivity isAnimating];
        _pingButton.enabled = !checking;
        [_pingButton setImage:(!checking && ![shown length])
                              ? GaugeIcon(18.0f, kAccentBlue) : nil
                      forState:UIControlStateNormal];
        _pingButton.contentHorizontalAlignment = UIControlContentHorizontalAlignmentRight;
        _pingButton.imageEdgeInsets = UIEdgeInsetsMake(0, 0, 0, 12.0f);
        [_pingButton addTarget:target action:action forControlEvents:UIControlEventTouchUpInside];
    } else {
        _pingButton.enabled = NO;
        [_pingButton setImage:nil forState:UIControlStateNormal];
    }
}

- (void)configureWithTitle:(NSString *)title
                     detail:(NSString *)detail
                     picked:(BOOL)picked
                     status:(NSString *)status {
    [self applyPicked:picked];
    [self styleLabels];
    _serverIcon.image = SenkoServerBadgeImage(nil, SenkoThemeIsIos16() ? 34.0f : 32.0f);
    _title.text = title;
    _detail.text = detail;
    _transport.text = nil;
    _unsupported.hidden = YES;
    _ping.text = status ? status : (picked ? @"   " : @"");
    SenkoSetRowChecking(_pingActivity, NO);
    _bars.hidden = YES;
    [self setPingTarget:nil action:NULL serverIndex:-1];
    SenkoStyleAccentLabel(_ping);
}

- (void)prepareForReuse {
    [super prepareForReuse];
/* keep layers; only clear text on reuse */
    _title.text = nil;
    _detail.text = nil;
    _transport.text = nil;
    _ping.text = nil;
    /* a recycled row must not carry a spinner into another server's result */
    SenkoSetRowChecking(_pingActivity, NO);
    [_pingButton removeTarget:nil action:NULL forControlEvents:UIControlEventTouchUpInside];
    [_pingButton setImage:nil forState:UIControlStateNormal];
    _pingButton.enabled = NO;
    _pingButton.hidden = YES;
    _unsupported.hidden = YES;
    _bars.hidden = YES;
    [_plate.layer removeAllAnimations];
    [self.contentView.layer removeAllAnimations];
    _plate.transform = CGAffineTransformIdentity;
    /* a row can be recycled mid entrance, so its lift has to be cleared or the
       next binding inherits the offset */
    self.contentView.transform = CGAffineTransformIdentity;
    self.contentView.alpha = 1.0f;
}

@end
