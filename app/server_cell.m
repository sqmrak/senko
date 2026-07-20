#import "server_cell.h"
#import "ui_theme.h"

#import <QuartzCore/QuartzCore.h>

static NSString *ServerProtocolLabel(SenkoServer *server) {
    if ([server->proto isEqualToString:@"vless"])
        return [NSString stringWithFormat:@"%@/%@/%@",
                server->proto ? server->proto : @"vless",
                server->net ? server->net : @"tcp",
                server->security ? server->security : @"none"];
    return server->proto ? server->proto : @"unknown";
}

static NSString *ServerEndpointLabel(SenkoServer *server, BOOL hideLinks) {
    if (hideLinks) return @"hidden :3";
    return [NSString stringWithFormat:@"%@:%d",
            server->host ? server->host : @"", server->port];
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
/* ios26: rasterize after style; alpha gradient bakes once for scroll */
        _plate.layer.shouldRasterize = YES;
        _plate.layer.rasterizationScale = [UIScreen mainScreen].scale;
        _plate.layer.drawsAsynchronously = YES;
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

        _title = [[UILabel alloc] initWithFrame:CGRectZero];
        _title.backgroundColor = [UIColor clearColor];
        _title.font = SenkoThemeIsIos16()
            ? SenkoFontBody(16, YES)
            : [UIFont boldSystemFontOfSize:16];
        SenkoStyleInkLabel(_title);
        [_plate addSubview:_title];

        _detail = [[UILabel alloc] initWithFrame:CGRectZero];
        _detail.backgroundColor = [UIColor clearColor];
        _detail.font = SenkoThemeIsIos16()
            ? SenkoFontBody(12, NO)
            : [UIFont systemFontOfSize:12];
        SenkoStyleMutedLabel(_detail);
        [_plate addSubview:_detail];

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
        SenkoStyleAccentLabel(_ping);
        [_plate addSubview:_ping];

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
    [_unsupported release];
    [_ping release];
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect bounds = self.contentView.bounds;
    CGFloat pad = SenkoThemeIsIos16() ? 12.0f : 8.0f;
    CGFloat vpad = SenkoThemeIsIos16() ? 4.0f : 6.0f;
    CGRect plate = CGRectInset(bounds, pad, vpad);
    BOOL sizeChanged = !_plateSized || !CGRectEqualToRect(_plate.frame, plate);
    _plate.frame = plate;
    CGFloat cr = SenkoThemeCardRadius();
    SenkoBeginSilentLayers();
    if (sizeChanged) {
        _plateGrad.frame = _plate.bounds;
        _plateGrad.cornerRadius = cr;
        _plate.layer.cornerRadius = cr;
        _plate.layer.shadowPath = nil;
        _plateSized = YES;
    }
    SenkoEndSilentLayers();
    _accent.frame = CGRectMake(10, 10, SenkoThemeIsIos16() ? 4 : 6, _plate.bounds.size.height - 20);
    _accent.layer.cornerRadius = SenkoThemeIsIos16() ? 2 : 3;
    _title.frame = CGRectMake(24, 8, _plate.bounds.size.width - 118, 23);
    _detail.frame = CGRectMake(24, 38, _plate.bounds.size.width - 118, 20);
    _unsupported.frame = CGRectMake(24, 57, _plate.bounds.size.width - 118, 18);
    _ping.frame = CGRectMake(_plate.bounds.size.width - 86, 28, 72, 24);
}

- (void)applyPicked:(BOOL)picked {
    _picked = picked;
    UIColor *top;
    UIColor *bottom;
/* take one theme snapshot while the cell is being restyled */
    BOOL flat = SenkoThemeIsFlat();
    BOOL boy = SenkoThemeIsBoykisser();
    BOOL light = SenkoThemeIsLight();
    BOOL ios26 = SenkoThemeIsIos26();
    BOOL ios16 = SenkoThemeIsIos16() && !ios26;
    BOOL miside = SenkoThemeIsMiside();
    BOOL frutiger = SenkoThemeIsFrutigeraero();
    if (picked && miside) {
        top = [UIColor colorWithRed:0.28 green:0.12 blue:0.32 alpha:1.0];
        bottom = [UIColor colorWithRed:0.16 green:0.06 blue:0.20 alpha:1.0];
    } else if (picked && boy) {
        top = [UIColor colorWithRed:1.00 green:0.78 blue:0.88 alpha:1.0];
        bottom = [UIColor colorWithRed:1.00 green:0.62 blue:0.80 alpha:1.0];
    } else if (picked && frutiger) {
        top = [UIColor colorWithRed:0.72 green:0.92 blue:1.00 alpha:1.0];
        bottom = [UIColor colorWithRed:0.42 green:0.78 blue:0.96 alpha:1.0];
    } else if (picked && ios26 && light) {
        top = [UIColor colorWithWhite:1.0 alpha:0.78];
        bottom = [UIColor colorWithWhite:1.0 alpha:0.52];
    } else if (picked && ios26) {
        top = [UIColor colorWithWhite:1.0 alpha:0.22];
        bottom = [UIColor colorWithWhite:1.0 alpha:0.10];
    } else if (picked && ios16 && light) {
        top = [UIColor colorWithRed:0.88 green:0.93 blue:1.00 alpha:1.0];
        bottom = [UIColor colorWithRed:0.80 green:0.88 blue:1.00 alpha:1.0];
    } else if (picked && ios16) {
        top = [UIColor colorWithRed:0.10 green:0.18 blue:0.32 alpha:1.0];
        bottom = [UIColor colorWithRed:0.06 green:0.12 blue:0.24 alpha:1.0];
    } else if (picked && flat && light) {
        top = [UIColor colorWithRed:0.90 green:0.94 blue:1.00 alpha:1.0];
        bottom = [UIColor colorWithRed:0.82 green:0.90 blue:1.00 alpha:1.0];
    } else if (picked && flat) {
        top = [UIColor colorWithRed:0.10 green:0.22 blue:0.40 alpha:1.0];
        bottom = [UIColor colorWithRed:0.06 green:0.14 blue:0.28 alpha:1.0];
    } else if (picked && light) {
/* cream select on white paper */
        top = [UIColor colorWithRed:1.000 green:0.920 blue:0.780 alpha:1.0];
        bottom = [UIColor colorWithRed:1.000 green:0.780 blue:0.480 alpha:1.0];
    } else if (picked) {
        top = [UIColor colorWithRed:0.55 green:0.32 blue:0.10 alpha:1.0];
        bottom = [UIColor colorWithRed:0.28 green:0.14 blue:0.04 alpha:1.0];
    } else {
        top = kCellHi;
        bottom = kCellLo;
    }
    SenkoBeginSilentLayers();
    _plate.layer.shadowOpacity = 0.0f;
    _plate.layer.shadowRadius = 0;
    _plate.layer.shadowPath = nil;
    _plate.layer.masksToBounds = YES;
    _plateGrad.colors = [NSArray arrayWithObjects:(id)top.CGColor, (id)bottom.CGColor, nil];
    if (picked && (flat || boy || frutiger)) {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor = kAccentBlue.CGColor;
    } else if (picked && light) {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor =
            [UIColor colorWithRed:0.95 green:0.55 blue:0.18 alpha:0.70].CGColor;
        _plate.layer.masksToBounds = YES;
        _plate.layer.shadowOpacity = 0.0f;
    } else if (picked) {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor =
            [UIColor colorWithRed:1.0 green:0.55 blue:0.12 alpha:0.65].CGColor;
        _plate.layer.masksToBounds = NO;
        _plate.layer.shadowOpacity = 0.22f;
        _plate.layer.shadowRadius = 1.5f;
        _plate.layer.shadowOffset = CGSizeMake(0, 1);
        _plate.layer.shadowColor = [UIColor blackColor].CGColor;
    } else if (boy) {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor =
            [UIColor colorWithRed:1.0 green:0.55 blue:0.75 alpha:0.35].CGColor;
        SenkoRemoveFrost(_plate);
    } else if (frutiger) {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor =
            [UIColor colorWithRed:0.20 green:0.75 blue:0.95 alpha:0.40].CGColor;
        SenkoRemoveFrost(_plate);
    } else if (flat) {
        _plate.layer.borderWidth = ios16 ? 0 : 0.5f;
        _plate.layer.borderColor = light
            ? [UIColor colorWithRed:0.70 green:0.74 blue:0.82 alpha:0.50].CGColor
            : [UIColor colorWithWhite:1 alpha:0.12].CGColor;
        _plate.backgroundColor = [UIColor clearColor];
    } else if (light) {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor = [UIColor colorWithWhite:0 alpha:0.10].CGColor;
        SenkoRemoveFrost(_plate);
    } else {
        _plate.layer.borderWidth = 0.5f;
        _plate.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.08].CGColor;
        SenkoRemoveFrost(_plate);
    }
    if (flat) {
        SenkoRemoveFrost(_plate);
        if (ios26) {
/* glass = alpha gradient only; no frost uiview per cell (n times overdraw) */
            if (picked && light) {
                _plateGrad.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1.0 alpha:0.48].CGColor,
                    (id)[UIColor colorWithWhite:1.0 alpha:0.22].CGColor, nil];
            } else if (picked) {
                _plateGrad.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1.0 alpha:0.22].CGColor,
                    (id)[UIColor colorWithWhite:1.0 alpha:0.08].CGColor, nil];
            } else if (light) {
                _plateGrad.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1.0 alpha:0.36].CGColor,
                    (id)[UIColor colorWithWhite:1.0 alpha:0.14].CGColor, nil];
            } else {
                _plateGrad.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1.0 alpha:0.18].CGColor,
                    (id)[UIColor colorWithWhite:1.0 alpha:0.06].CGColor, nil];
            }
            _plate.backgroundColor = [UIColor clearColor];
            _plate.opaque = NO;
            _plateGrad.opaque = NO;
            _plate.layer.borderWidth = 0.5f;
            _plate.layer.borderColor = light
                ? [UIColor colorWithWhite:1 alpha:0.70].CGColor
                : [UIColor colorWithWhite:1 alpha:0.26].CGColor;
/* soft contact shadow; tight radius is cheaper on armv7 */
            _plate.layer.masksToBounds = NO;
            _plate.layer.shadowColor = [UIColor colorWithWhite:0 alpha:1].CGColor;
            _plate.layer.shadowOpacity = light ? 0.10f : 0.28f;
            _plate.layer.shadowRadius = 6.0f;
            _plate.layer.shadowOffset = CGSizeMake(0, 3);
            if (_plate.bounds.size.width > 1.0f) {
                UIBezierPath *sp = [UIBezierPath bezierPathWithRoundedRect:_plate.bounds
                                                              cornerRadius:SenkoThemeCardRadius()];
                _plate.layer.shadowPath = sp.CGPath;
            }
            _plateGrad.masksToBounds = YES;
/* no frost install on list cells */
        } else if (ios16) {
            if (picked && light) {
                _plateGrad.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithRed:0.86 green:0.92 blue:1.00 alpha:0.96].CGColor,
                    (id)[UIColor colorWithRed:0.78 green:0.88 blue:1.00 alpha:0.94].CGColor, nil];
            } else if (picked) {
                _plateGrad.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithRed:0.16 green:0.24 blue:0.42 alpha:0.95].CGColor,
                    (id)[UIColor colorWithRed:0.10 green:0.14 blue:0.28 alpha:0.95].CGColor, nil];
            } else if (light) {
                _plateGrad.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1.0 alpha:0.94].CGColor,
                    (id)[UIColor colorWithWhite:1.0 alpha:0.88].CGColor, nil];
            } else {
                _plateGrad.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1.0 alpha:0.14].CGColor,
                    (id)[UIColor colorWithWhite:1.0 alpha:0.08].CGColor, nil];
            }
            _plate.layer.borderWidth = 0;
            _plate.layer.borderColor = [UIColor clearColor].CGColor;
            _plate.layer.masksToBounds = NO;
            _plate.layer.shadowColor = [UIColor blackColor].CGColor;
            _plate.layer.shadowOpacity = light ? 0.12f : 0.35f;
            _plate.layer.shadowRadius = 8.0f;
            _plate.layer.shadowOffset = CGSizeMake(0, 3);
/* shadowpath only when bounds known; skip zero rects */
            if (_plate.bounds.size.width > 1.0f) {
                UIBezierPath *sp = [UIBezierPath bezierPathWithRoundedRect:_plate.bounds
                                                              cornerRadius:SenkoThemeCardRadius()];
                _plate.layer.shadowPath = sp.CGPath;
            }
            _plateGrad.masksToBounds = YES;
        } else if (picked && light) {
            _plateGrad.colors = [NSArray arrayWithObjects:
                (id)[UIColor colorWithRed:0.86 green:0.91 blue:1.00 alpha:1.0].CGColor,
                (id)[UIColor colorWithRed:0.78 green:0.86 blue:0.98 alpha:1.0].CGColor, nil];
        } else if (picked) {
            _plateGrad.colors = [NSArray arrayWithObjects:
                (id)[UIColor colorWithRed:0.12 green:0.22 blue:0.40 alpha:1.0].CGColor,
                (id)[UIColor colorWithRed:0.08 green:0.14 blue:0.28 alpha:1.0].CGColor, nil];
        } else if (light) {
            _plateGrad.colors = [NSArray arrayWithObjects:
                (id)[UIColor colorWithRed:0.97 green:0.97 blue:0.98 alpha:1.0].CGColor,
                (id)[UIColor colorWithRed:0.93 green:0.94 blue:0.96 alpha:1.0].CGColor, nil];
        } else {
            _plateGrad.colors = [NSArray arrayWithObjects:
                (id)[UIColor colorWithRed:0.16 green:0.17 blue:0.20 alpha:1.0].CGColor,
                (id)[UIColor colorWithRed:0.12 green:0.13 blue:0.16 alpha:1.0].CGColor, nil];
        }
    } else {
        SenkoRemoveFrost(_plate);
    }
    if (picked) {
        UIColor *edge = (flat || frutiger)
            ? kAccentBlue
            : (boy
               ? [UIColor colorWithRed:1.0 green:0.30 blue:0.64 alpha:1.0]
               : [UIColor colorWithRed:1.0 green:0.55 blue:0.12 alpha:1.0]);
        _plate.layer.borderWidth = 1.0f;
        _plate.layer.borderColor = edge.CGColor;
    }
/* always rasterize after style; alpha glass has no live blur so bake is safe */
    _plate.layer.shouldRasterize = YES;
    _plate.layer.rasterizationScale = [UIScreen mainScreen].scale;
    SenkoEndSilentLayers();
}

- (void)styleLabelsForPicked:(BOOL)picked {
/* cream text only on dark select plates */
    BOOL darkPlate = picked && !SenkoThemeIsFlat() && !SenkoThemeIsBoykisser()
                     && !SenkoThemeIsLight();
    if (darkPlate) {
        SenkoStyleInkOnDark(_title);
        SenkoStyleMutedOnDark(_detail);
    } else {
        SenkoStyleInkLabel(_title);
        SenkoStyleMutedLabel(_detail);
    }
}

- (void)configureWithServer:(SenkoServer *)server
                      picked:(BOOL)picked
                   hideLinks:(BOOL)hideLinks
                     pingVal:(NSNumber *)ping {
    [self applyPicked:picked];
/* restyle each bind; reuse may outlive theme switch */
    [self styleLabelsForPicked:picked];
    _accent.backgroundColor = server->supported
        ? kAccentBlue
        : [UIColor colorWithRed:0.92 green:0.16 blue:0.12 alpha:1.0];
    _title.text = [server->remark length]
        ? server->remark : ServerEndpointLabel(server, hideLinks);
    _detail.text = [NSString stringWithFormat:@"%@      %@",
                    ServerProtocolLabel(server), ServerEndpointLabel(server, hideLinks)];
    _unsupported.hidden = server->supported;
    if (!server->supported) {
        _unsupported.textColor = SenkoThemeIsLight()
            ? [UIColor colorWithRed:0.72 green:0.10 blue:0.08 alpha:1.0]
            : [UIColor colorWithRed:1.0 green:0.45 blue:0.36 alpha:1.0];
    }

    BOOL darkPlate = picked && !SenkoThemeIsFlat() && !SenkoThemeIsBoykisser()
                     && !SenkoThemeIsLight();
    if (!ping) {
        _ping.text = picked ? @"   " : @"";
        if (darkPlate)
            SenkoStyleAccentOnDark(_ping);
        else SenkoStyleAccentLabel(_ping);
    } else if ([ping intValue] >= 0) {
        _ping.text = [NSString stringWithFormat:@"%d ms", [ping intValue]];
        if (darkPlate)
            SenkoStyleAccentOnDark(_ping);
        else SenkoStyleAccentLabel(_ping);
    } else {
        _ping.text = @"timeout";
        _ping.textColor = SenkoThemeIsLight()
            ? [UIColor colorWithRed:0.72 green:0.10 blue:0.08 alpha:1.0]
            : [UIColor colorWithRed:1.0 green:0.45 blue:0.36 alpha:1.0];
        _ping.shadowColor = nil;
        _ping.shadowOffset = CGSizeZero;
    }
}

- (void)configureWithTitle:(NSString *)title
                     detail:(NSString *)detail
                     picked:(BOOL)picked
                     status:(NSString *)status {
    [self applyPicked:picked];
    [self styleLabelsForPicked:picked];
    _accent.backgroundColor = kAccentBlue;
    _title.text = title;
    _detail.text = detail;
    _unsupported.hidden = YES;
    _ping.text = status ? status : (picked ? @"   " : @"");
    if (picked && !SenkoThemeIsFlat() && !SenkoThemeIsBoykisser() && !SenkoThemeIsLight())
        SenkoStyleAccentOnDark(_ping);
    else SenkoStyleAccentLabel(_ping);
}

- (void)prepareForReuse {
    [super prepareForReuse];
/* keep layers; only clear text on reuse */
    _title.text = nil;
    _detail.text = nil;
    _ping.text = nil;
}

@end
