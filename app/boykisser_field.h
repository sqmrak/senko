#ifndef SENKO_BOYKISSER_FIELD_H
#define SENKO_BOYKISSER_FIELD_H

#import <UIKit/UIKit.h>

/* falling sprites: boykisser.png for that theme, any bundle png elsewhere, or snow */
@interface SenkoBoykisserField : UIView
- (id)initWithFrame:(CGRect)frame spriteName:(NSString *)name;
/* the same field with small drifting flakes instead of sprites */
- (id)initSnowWithFrame:(CGRect)frame;
- (BOOL)isSnow;
- (void)start;
- (void)stop;
/* pause off-screen to save cpu */
- (void)setPaused:(BOOL)paused;
- (BOOL)isRunning;
@end

#endif
