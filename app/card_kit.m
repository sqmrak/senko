#import "card_kit.h"
#import "home_view.h"
#import "ui_theme.h"

@implementation SenkoCardView

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) [self restyle];
    return self;
}

- (void)restyle {
    SenkoStyleHomePlate(self, SenkoHomePlateRadius(), nil);
}

- (void)layoutSubviews {
    [super layoutSubviews];
    SenkoLayoutHomePlate(self);
}

@end

@implementation SenkoMeterView
@synthesize fraction = _fraction;
@synthesize fill = _fill;

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.backgroundColor = [UIColor clearColor];
        self.userInteractionEnabled = NO;
        _fill = [kAccentBlue retain];
    }
    return self;
}

- (void)dealloc {
    [_fill release];
    [super dealloc];
}

- (void)setFraction:(CGFloat)fraction {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    if (fraction == _fraction) return;
    _fraction = fraction;
    [self setNeedsDisplay];
}

- (void)setFill:(UIColor *)fill {
    if (fill == _fill) return;
    [_fill release];
    _fill = [fill retain];
    [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect {
    (void)rect;
    CGRect b = self.bounds;
    CGFloat r = b.size.height * 0.5f;
    UIColor *track = [kInk colorWithAlphaComponent:SenkoThemeIsLight() ? 0.12f : 0.18f];
    [track setFill];
    [[UIBezierPath bezierPathWithRoundedRect:b cornerRadius:r] fill];
    CGFloat w = floorf(b.size.width * _fraction);
    if (w <= 0.0f) return;
    /* a sliver narrower than the cap would draw as a lopsided blob */
    if (w < b.size.height) w = b.size.height;
    [_fill setFill];
    [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 0, w, b.size.height)
                                cornerRadius:r] fill];
}

@end

UILabel *SenkoCardLabel(UIView *host, UIFont *font, UIColor *color, NSTextAlignment alignment) {
    UILabel *label = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    label.backgroundColor = [UIColor clearColor];
    label.font = font;
    label.textColor = color;
    label.textAlignment = alignment;
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
    label.userInteractionEnabled = NO;
    [host addSubview:label];
    return label;
}

CGFloat SenkoCardColumnWidth(CGFloat available) {
    CGFloat column = available - 32.0f;
    return column > 560.0f ? 560.0f : (column < 120.0f ? 120.0f : column);
}
