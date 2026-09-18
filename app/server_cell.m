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

static UIImage *SenkoCellServerIcon(NSString *raw) {
    NSString *flag = SenkoCellFlag(raw);
    NSString *code = SenkoCellFlagCode(flag);
    UIImage *image = [code length]
        ? SenkoCellCachedImage([NSString stringWithFormat:@"flag-%@.png", code])
        : nil;
    return image ? image : SenkoCellCachedImage(@"server-placeholder.png");
}

static BOOL SenkoCellHasFlag(NSString *raw) {
    NSString *code = SenkoCellFlagCode(SenkoCellFlag(raw));
    return [code length] &&
           SenkoCellCachedImage([NSString stringWithFormat:@"flag-%@.png", code]) != nil;
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

static NSString *ServerProtocolLabel(SenkoServer *server) {
    if ([server->proto isEqualToString:@"vless"])
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
    return server->proto ? server->proto : @"unknown";
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
        [self.contentView addSubview:_plate];

        _accent = [[UIView alloc] initWithFrame:CGRectZero];
        _accent.layer.cornerRadius = 3;
        _accent.layer.borderWidth = 1;
        _accent.layer.borderColor = [UIColor colorWithWhite:0 alpha:0.25].CGColor;
        [_plate addSubview:_accent];

        _serverIcon = [[UIImageView alloc] initWithFrame:CGRectZero];
        _serverIcon.backgroundColor = [UIColor colorWithWhite:1.0f alpha:0.94f];
        _serverIcon.contentMode = UIViewContentModeScaleAspectFill;
        _serverIcon.clipsToBounds = YES;
        _serverIcon.layer.masksToBounds = YES;
        _serverIcon.layer.cornerRadius = 8.0f;
        _serverIcon.layer.borderWidth = 0.7f;
        _serverIcon.layer.borderColor = [UIColor colorWithWhite:0 alpha:0.16f].CGColor;
        _serverIcon.image = [UIImage imageNamed:@"server-placeholder.png"];
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

        _chevron = [[UIImageView alloc] initWithFrame:CGRectZero];
        _chevron.contentMode = UIViewContentModeScaleAspectFit;
        _chevron.userInteractionEnabled = NO;
        _chevron.alpha = 0.45f;
        [_plate addSubview:_chevron];

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
    [_accent release];
    [_title release];
    [_detail release];
    [_transport release];
    [_unsupported release];
    [_ping release];
    [_pingActivity release];
    [_pingButton release];
    [_serverIcon release];
    [_chevron release];
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect bounds = self.contentView.bounds;
    CGFloat pad = SENKO_LIST_PLATE_INSET;
    CGFloat vpad = 3.0f;
    CGRect plate = CGRectInset(bounds, pad, vpad);
    BOOL sizeChanged = !_plateSized || !CGSizeEqualToSize(_plate.bounds.size, plate.size);
    /* the press pop and the row reveal both leave a transform on the plate, and
       writing a frame through one of those folds the animation into the layout */
    _plate.bounds = CGRectMake(0, 0, plate.size.width, plate.size.height);
    _plate.center = CGPointMake(CGRectGetMidX(plate), CGRectGetMidY(plate));
    CGFloat cr = SenkoThemeCardRadius();
    SenkoBeginSilentLayers();
    if (sizeChanged) {
        _plateGrad.frame = _plate.bounds;
        _plateGrad.cornerRadius = cr;
        _plate.layer.cornerRadius = cr;
        if (_plate.layer.shadowOpacity > 0.0f)
            _plate.layer.shadowPath = [UIBezierPath
                bezierPathWithRoundedRect:_plate.bounds cornerRadius:cr].CGPath;
        else
            _plate.layer.shadowPath = nil;
        _plateSized = YES;
    }
    SenkoEndSilentLayers();
    _accent.frame = CGRectMake(9, 8, SenkoThemeIsIos16() ? 3 : 4,
                               MAX(8.0f, _plate.bounds.size.height - 16));
    _accent.layer.cornerRadius = SenkoThemeIsIos16() ? 2 : 3;
    CGFloat iconSize = SenkoThemeIsIos16() ? 34.0f : 32.0f;
    CGFloat iconY = floorf((_plate.bounds.size.height - iconSize) * 0.5f);
    _serverIcon.frame = CGRectMake(18.0f, iconY, iconSize, iconSize);
    _serverIcon.layer.cornerRadius = SenkoThemeIsIos16() ? 9.0f : 8.0f;
    _serverIcon.layer.masksToBounds = YES;
    _serverIcon.layer.borderColor = (SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.16f]
        : [UIColor colorWithWhite:1 alpha:0.20f]).CGColor;
    CGFloat plateW = _plate.bounds.size.width;
    CGFloat plateH = _plate.bounds.size.height;
    CGFloat textX = 58.0f;
    /* the right cluster is chevron, then reading, then bars, and the title has
       to stop before all three or it overprints them on a 320pt screen */
    /* reading then chevron, both centred on the row so the pair reads as one
       control */
    CGFloat rowMid = floorf(plateH * 0.5f);
    CGFloat chevronW = 9.0f;
    CGFloat chevronH = 14.0f;
    CGFloat chevronX = plateW - 16.0f - chevronW;
    _chevron.frame = CGRectMake(chevronX, rowMid - chevronH * 0.5f, chevronW, chevronH);
    CGFloat pingW = 62.0f;
    CGFloat pingH = 20.0f;
    CGFloat pingX = chevronX - 8.0f - pingW;
    _ping.frame = CGRectMake(pingX, rowMid - pingH * 0.5f, pingW, pingH);
    /* match the gauge's right-aligned image position. centring this in the
       reading field put the live spinner 18pt left of the resting ping glyph */
    _pingActivity.frame = CGRectMake(chevronX - 30.0f, rowMid - 9.0f,
                                     18.0f, 18.0f);
    _pingButton.frame = CGRectMake(pingX - 6.0f, rowMid - 15.0f,
                                   chevronX - pingX + 6.0f, 30.0f);
    CGFloat textW = pingX - textX - 8.0f;
    CGFloat detailW = chevronX - textX - 10.0f;
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
    _title.frame = CGRectMake(textX, 2, textW, 22);
    _detail.frame = CGRectMake(textX, 24, detailW, 14);
    _transport.frame = CGRectMake(textX, 24, detailW, 16);
    _unsupported.frame = CGRectMake(textX, 42, detailW, 12);
}

- (void)applyPicked:(BOOL)picked {
    _picked = picked;
/* take one theme snapshot while the cell is being restyled */
    BOOL flat = SenkoThemeIsFlat();
    BOOL boy = SenkoThemeIsBoykisser();
    BOOL light = SenkoThemeIsLight();
    BOOL ios26 = SenkoThemeIsIos26();
    BOOL ios16 = SenkoThemeIsIos16() && !ios26;
    BOOL frutiger = SenkoThemeIsFrutigeraero();

/* the plate keeps the theme's own card colour in every state: selection is one
   restrained outline, so nothing here may depend on picked */
    SenkoBeginSilentLayers();
    _plate.layer.shadowOpacity = 0.0f;
    _plate.layer.shadowRadius = 0;
    _plate.layer.shadowPath = nil;
    _plate.layer.masksToBounds = YES;
    _plateGrad.colors = [NSArray arrayWithObjects:
        (id)kCellHi.CGColor, (id)kCellLo.CGColor, nil];

    if (boy) {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor =
            [UIColor colorWithRed:1.0 green:0.55 blue:0.75 alpha:0.35].CGColor;
    } else if (frutiger) {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor =
            [UIColor colorWithRed:0.20 green:0.75 blue:0.95 alpha:0.40].CGColor;
    } else if (flat) {
        _plate.layer.borderWidth = ios16 ? 0.0f : 0.5f;
        _plate.layer.borderColor = light
            ? [UIColor colorWithRed:0.70 green:0.74 blue:0.82 alpha:0.50].CGColor
            : [UIColor colorWithWhite:1 alpha:0.12].CGColor;
    } else {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor = light
            ? [UIColor colorWithWhite:0 alpha:0.10].CGColor
            : [UIColor colorWithWhite:1 alpha:0.08].CGColor;
    }
    SenkoRemoveFrost(_plate);

    if (ios26) {
/* glass is an alpha gradient only; a frost uiview per cell would overdraw the
   wallpaper once per visible row */
        _plateGrad.colors = light
            ? [NSArray arrayWithObjects:
                (id)[UIColor colorWithWhite:1.0 alpha:0.36].CGColor,
                (id)[UIColor colorWithWhite:1.0 alpha:0.14].CGColor, nil]
            : [NSArray arrayWithObjects:
                (id)[UIColor colorWithWhite:1.0 alpha:0.18].CGColor,
                (id)[UIColor colorWithWhite:1.0 alpha:0.06].CGColor, nil];
        _plate.backgroundColor = [UIColor clearColor];
        _plate.opaque = NO;
        _plateGrad.opaque = NO;
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor = light
            ? [UIColor colorWithWhite:1 alpha:0.70].CGColor
            : [UIColor colorWithWhite:1 alpha:0.26].CGColor;
        _plate.layer.masksToBounds = NO;
        _plate.layer.shadowColor = [UIColor colorWithWhite:0 alpha:1].CGColor;
        _plate.layer.shadowOpacity = light ? 0.10f : 0.28f;
        _plate.layer.shadowRadius = 6.0f;
        _plate.layer.shadowOffset = CGSizeMake(0, 3);
        if (_plate.bounds.size.width > 1.0f)
            _plate.layer.shadowPath = [UIBezierPath
                bezierPathWithRoundedRect:_plate.bounds
                             cornerRadius:SenkoThemeCardRadius()].CGPath;
        _plateGrad.masksToBounds = YES;
    } else if (ios16) {
        _plateGrad.colors = light
            ? [NSArray arrayWithObjects:
                (id)[UIColor colorWithWhite:1.0 alpha:0.94].CGColor,
                (id)[UIColor colorWithWhite:1.0 alpha:0.88].CGColor, nil]
            : [NSArray arrayWithObjects:
                (id)[UIColor colorWithWhite:1.0 alpha:0.14].CGColor,
                (id)[UIColor colorWithWhite:1.0 alpha:0.08].CGColor, nil];
        _plate.layer.borderWidth = 0.0f;
        _plate.layer.masksToBounds = NO;
        _plate.layer.shadowColor = [UIColor blackColor].CGColor;
        _plate.layer.shadowOpacity = light ? 0.12f : 0.35f;
        _plate.layer.shadowRadius = 8.0f;
        _plate.layer.shadowOffset = CGSizeMake(0, 3);
        if (_plate.bounds.size.width > 1.0f)
            _plate.layer.shadowPath = [UIBezierPath
                bezierPathWithRoundedRect:_plate.bounds
                             cornerRadius:SenkoThemeCardRadius()].CGPath;
    } else if (flat) {
        _plate.backgroundColor = [UIColor clearColor];
    }

    if (picked) {
/* selection stays quiet: keep the theme card and outline it in the accent */
        _plate.layer.borderWidth = 0.75f;
        _plate.layer.borderColor = [kAccentBlue colorWithAlphaComponent:0.58f].CGColor;
    }
/* the classic plate has a gradient, three labels and embossed ink. redrawing
   that stack for each scroll tick is slower than keeping one bounded tile on
   armv7, just as the ios 16 plate already does. glass stays unrasterized
   because its translucent material must sample the wallpaper behind it. */
    _plate.layer.shouldRasterize = !ios26;
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

- (void)revealAtIndex:(NSUInteger)index {
    SenkoRevealView(_plate, index);
}

- (void)setHighlighted:(BOOL)highlighted animated:(BOOL)animated {
    [super setHighlighted:highlighted animated:animated];
    SenkoPressPop(_plate, highlighted);
}

- (void)configureWithServer:(SenkoServer *)server
                      picked:(BOOL)picked
                     pingVal:(NSNumber *)ping
                 displayName:(NSString *)displayName {
    [self applyPicked:picked];
/* restyle each bind; reuse may outlive theme switch */
    [self styleLabels];
    _accent.backgroundColor = server->supported
        ? kAccentBlue
        : [UIColor colorWithRed:0.92 green:0.16 blue:0.12 alpha:1.0];
    BOOL hasFlag = SenkoCellHasFlag(server->remark);
    _serverIcon.image = SenkoCellServerIcon(server->remark);
    _serverIcon.contentMode = hasFlag
        ? UIViewContentModeScaleAspectFill : UIViewContentModeScaleAspectFit;
    _serverIcon.backgroundColor = hasFlag
        ? [UIColor clearColor] : [UIColor colorWithWhite:1.0f alpha:0.94f];
    NSString *title = [displayName length] ? displayName
        : ([server->remark length] ? SenkoCellRemark(server->remark)
                                   : ServerEndpointLabel(server));
    _title.text = title;
    _detail.text = nil;
    _transport.text = ServerProtocolLabel(server);
    _unsupported.hidden = server->supported;
    if (!server->supported) {
        _unsupported.textColor = SenkoThemeIsLight()
            ? [UIColor colorWithRed:0.72 green:0.10 blue:0.08 alpha:1.0]
            : [UIColor colorWithRed:1.0 green:0.45 blue:0.36 alpha:1.0];
    }

    _chevron.image = SenkoIconChevron(14.0f, kInkMuted);
    _chevron.hidden = NO;
    BOOL checking = ping && [ping intValue] == -3;
    if ([_pingActivity respondsToSelector:@selector(setColor:)])
        _pingActivity.color = kAccentBlue;
    SenkoSetRowChecking(_pingActivity, checking);
    if (!ping) {
        _ping.text = picked ? @"   " : @"";
        SenkoStyleAccentLabel(_ping);
    } else if ([ping intValue] >= 0) {
        _ping.text = [NSString stringWithFormat:@"%d ms", [ping intValue]];
        SenkoStyleAccentLabel(_ping);
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
    _accent.backgroundColor = kAccentBlue;
    _serverIcon.image = [UIImage imageNamed:@"server-placeholder.png"];
    _serverIcon.contentMode = UIViewContentModeScaleAspectFit;
    _serverIcon.backgroundColor = [UIColor colorWithWhite:1.0f alpha:0.94f];
    _title.text = title;
    _detail.text = detail;
    _transport.text = nil;
    _unsupported.hidden = YES;
    _ping.text = status ? status : (picked ? @"   " : @"");
    SenkoSetRowChecking(_pingActivity, NO);
    /* the amneziawg row has no per-server card of its own */
    _chevron.hidden = YES;
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
    _chevron.hidden = NO;
    [_plate.layer removeAllAnimations];
    [self.contentView.layer removeAllAnimations];
    _plate.transform = CGAffineTransformIdentity;
    /* a row can be recycled mid entrance, so its lift has to be cleared or the
       next binding inherits the offset */
    self.contentView.transform = CGAffineTransformIdentity;
    self.contentView.alpha = 1.0f;
    _serverIcon.image = [UIImage imageNamed:@"server-placeholder.png"];
    _serverIcon.contentMode = UIViewContentModeScaleAspectFit;
    _serverIcon.backgroundColor = [UIColor colorWithWhite:1.0f alpha:0.94f];
}

@end
