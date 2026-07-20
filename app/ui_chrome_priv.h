#ifndef SENKO_UI_CHROME_PRIV_H
#define SENKO_UI_CHROME_PRIV_H

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

/* bar and dome share size/color keys so recolor skips full rebuilds */
extern char SenkoStyleSizeKey;
extern char SenkoStyleTopKey;
extern char SenkoStyleBotKey;

CAGradientLayer *SenkoNamedGradientLayer(CALayer *parent, NSString *name);
BOOL SenkoStyleSizeMatches(UIView *v, CGSize sz);
void SenkoStyleRemember(UIView *v, CGSize sz, UIColor *top, UIColor *bottom);
void SenkoApplyShadowPath(CALayer *layer, CGFloat radius);
UIBezierPath *SenkoHeartPath(CGRect r);
CAShapeLayer *SenkoHeartMaskLayer(CGRect bounds);

#endif
