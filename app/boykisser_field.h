#ifndef SENKO_BOYKISSER_FIELD_H
#define SENKO_BOYKISSER_FIELD_H

#import <UIKit/UIKit.h>

/* flake overlay only for boykisser */
@interface SenkoBoykisserField : UIView
- (void)start;
- (void)stop;
/* pause off-screen to save cpu */
- (void)setPaused:(BOOL)paused;
- (BOOL)isRunning;
@end

/* cached sprite; avoid redecode per flake */
UIImage *SenkoBoykisserSprite(CGFloat size);

#endif
