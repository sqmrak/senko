#ifndef SENKO_MATERIAL_H
#define SENKO_MATERIAL_H

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

/* the motion and depth of senko-material. nothing here runs unless that theme
   is active, so the other themes pay nothing for it */

/* material's easing curves: standard for things that stay on screen,
   deceleration for things that arrive, acceleration for things that leave */
CAMediaTimingFunction *SenkoMaterialStandardCurve(void);
CAMediaTimingFunction *SenkoMaterialDecelerateCurve(void);
CAMediaTimingFunction *SenkoMaterialAccelerateCurve(void);

/* the shadow a surface raised by dp casts. only the shadow parameters are set:
   the caller owns the layer's shadowPath, which it knows the shape of */
void SenkoMaterialElevate(CALayer *layer, CGFloat dp);
/* the same, animated, for a surface that rises under a finger */
void SenkoMaterialSetElevation(CALayer *layer, CGFloat dp, BOOL animated);
/* a rounded rectangle shadow path for a plain view's layer */
void SenkoMaterialShadowPath(CALayer *layer, CGFloat radius);

/* material's indeterminate circular progress: one arc that grows to about
   257 degrees, is caught up by its tail, and turns the whole time. arc is a
   shape layer whose path is a full circle starting at the top; the animation
   repeats seamlessly every 5.4 s and runs on the render server */
void SenkoMaterialSpinnerStart(CAShapeLayer *arc);
void SenkoMaterialSpinnerStop(CAShapeLayer *arc);

/* a ring that grows from a surface and fades, for a state that just arrived */
void SenkoMaterialPulse(CALayer *host, CGPoint center, CGFloat radius, UIColor *color);

/* a view can shape and colour the ripple it receives. all optional */
@protocol SenkoRippleHost <NSObject>
@optional
/* where the ripple is clipped, in the view's own coordinates */
- (CGRect)senkoRippleBounds;
- (CGFloat)senkoRippleRadius;
- (UIColor *)senkoRippleColor;
@end

/* the application class: it sees every touch before the views do, which is
   the one place a ripple can start at the finger without each screen wiring
   its own controls. passed to UIApplicationMain by name */
@interface SenkoApplication : UIApplication
@end

#endif
