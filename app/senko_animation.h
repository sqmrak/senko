#ifndef SENKO_ANIMATION_H
#define SENKO_ANIMATION_H

#import <UIKit/UIKit.h>

#include "senko_motion.h"

/* the movement of the interface, drawn by the app instead of the system. every
   move is a sampled curve of senko_motion, so a spring wobbles the same on ios
   4.3, which has no CASpringAnimation, as on ios 16. with reduced motion, or a
   view that is not on screen, a view only takes its final state */

/* true when the person asked the system for less motion; the entry is looked up
   at run time, since ios 4.3 to 7 have no such setting */
BOOL SenkoReduceMotion(void);

/* a view takes its final transform and alpha at once and plays the way there.
   a running move of the view is retargeted from what is on screen, so a leave
   that begins during an entry never jumps. the completion runs at once when
   nothing is drawn */
void SenkoMotionAnimate(UIView *view, CGAffineTransform transform, CGFloat alpha,
                        senko_motion_curve curve, NSTimeInterval delay,
                        void (^completion)(BOOL finished));
/* the view is already in its final state: this plays the way in from a
   transform and an alpha without touching it, and a later call replaces a run
   that has not ended */
void SenkoMotionEnter(UIView *view, CGAffineTransform from, CGFloat fromAlpha,
                      senko_motion_curve curve, NSTimeInterval delay);

/* the press of a control: it shrinks by depth (0.05 is five percent) while a
   finger is on it and springs back past its size when the finger leaves */
void SenkoMotionAttachBounce(UIControl *control, CGFloat depth);
void SenkoMotionBounce(UIView *view, BOOL pressed, CGFloat depth);
/* a press or a release of SenkoMotionBounce is still running on the view */
BOOL SenkoMotionBouncing(UIView *view);

/* the rows of a list that is on show for the first time rise in one after the
   other, the first ten only. a table that was staggered once is left alone, so a
   reload or a return to the screen does not replay it */
void SenkoMotionStaggerRows(UITableView *table);
/* the same rise, every time it is called: for a list that comes on show again,
   such as the server picker that slides over the home screen */
void SenkoMotionRiseRows(UITableView *table);

/* the picture of a view as it is drawn now, in an image view at the view's
   frame, or nil when the view has no size */
UIImageView *SenkoMotionSnapshot(UIView *view);

/* a screen swap: leaving is the picture of what was on screen, laid over
   entering, which is already in its final state and stays opaque and still.
   the picture slips away against the side the new screen comes from and fades,
   so the new screen is uncovered, never faded in under a half transparent
   picture. direction is 1 when the new screen is to the right of the old one,
   -1 when it is to the left. the picture is taken out when it is done */
void SenkoMotionSwap(UIView *leaving, UIView *entering, int direction);

/* the app's navigation controller: pushes and pops swap the screens on the
   curves above instead of the system slide. an edge swipe back keeps the system
   transition, which has to follow the finger */
@interface SenkoNavigationController : UINavigationController
@end

#endif
