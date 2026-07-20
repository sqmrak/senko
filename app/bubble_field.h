#ifndef SENKO_BUBBLE_FIELD_H
#define SENKO_BUBBLE_FIELD_H

#import <UIKit/UIKit.h>

/* floating gloss bubbles for frutigeraero */
@interface SenkoBubbleField : UIView
- (void)start;
- (void)stop;
- (void)setPaused:(BOOL)paused;
- (BOOL)isRunning;
@end

#endif
