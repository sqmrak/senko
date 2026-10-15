#ifndef SENKO_CARD_KIT_H
#define SENKO_CARD_KIT_H

#import <UIKit/UIKit.h>

/* the pieces the statistics, subscription and log screens are built from. they
   draw on the home screen's plate and the theme palette, so a theme restyles
   them without a second set of colours */

/* a rounded plate. it restyles itself when the theme changes and refits its
   relief when the size does */
@interface SenkoCardView : UIView
- (void)restyle;
@end

/* a horizontal meter, fraction 0 to 1. the track is a quiet tint of the ink so
   it reads on every theme, light or dark */
@interface SenkoMeterView : UIView {
    CGFloat  _fraction;
    UIColor *_fill;
}
@property (nonatomic, assign) CGFloat fraction;
@property (nonatomic, retain) UIColor *fill;
@end

/* a transparent label on a card */
UILabel *SenkoCardLabel(UIView *host, UIFont *font, UIColor *color, NSTextAlignment alignment);

/* the width the content column gets: the full screen on a phone, held to a
   readable column on an ipad or a phone turned sideways */
CGFloat SenkoCardColumnWidth(CGFloat available);

#endif
