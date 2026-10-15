#import "routing_ui.h"

#import "app_common.h"
#import "ui_theme.h"

CGFloat SenkoCaptionHeight(NSString *text, UIFont *font, UITableView *tv) {
    if (![text length]) return 0.0f;
    CGFloat width = tv.bounds.size.width - 40.0f;
    if (width < 120.0f) width = 120.0f;
    CGSize size = SenkoTextSize(text, font, width);
    return MAX(34.0f, size.height + 22.0f);
}

UIView *SenkoCaptionView(NSString *text, UIFont *font, UITableView *tv) {
    CGFloat height = SenkoCaptionHeight(text, font, tv);
    UIView *wrap = [[[UIView alloc] initWithFrame:
                     CGRectMake(0, 0, tv.bounds.size.width, height)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    wrap.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    if (!height) return wrap;
    UILabel *label = [[[UILabel alloc] initWithFrame:
                       CGRectMake(20.0f, 4.0f, tv.bounds.size.width - 40.0f,
                                  height - 8.0f)] autorelease];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                             UIViewAutoresizingFlexibleHeight;
    label.backgroundColor = [UIColor clearColor];
    label.font = font;
    label.numberOfLines = 0;
    label.lineBreakMode = NSLineBreakByWordWrapping;
    label.text = text;
    SenkoStyleMutedLabel(label);
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
    [wrap addSubview:label];
    return wrap;
}

UIColor *SenkoActionColor(NSString *action) {
    if ([action isEqualToString:@"block"])
        return [UIColor colorWithRed:0.82f green:0.24f blue:0.22f alpha:1.0f];
    if ([action isEqualToString:@"direct"]) return kAccentBlue;
    return kConnOn;
}

NSString *SenkoActionTitle(NSString *action) {
    if ([action isEqualToString:@"direct"]) return SenkoLocalizedText(@"Direct");
    if ([action isEqualToString:@"block"]) return SenkoLocalizedText(@"Block");
    return SenkoLocalizedText(@"Through the tunnel");
}
