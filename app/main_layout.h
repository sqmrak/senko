#ifndef SENKO_MAIN_LAYOUT_H
#define SENKO_MAIN_LAYOUT_H

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

/* the width the classic table settles on, so any chrome drawn above it can
   line up with the same left and right edges */
CGFloat SenkoMainTableWidth(CGFloat width, CGFloat height);

/* pass connect for size fix on rotate */
void SenkoLayoutMainContent(UIView *root,
                            CAGradientLayer *background,
                            UITableView *table,
                            UIButton *pingAll,
                            UILabel *status,
                            UIButton *connect,
                            UIColor *domeTop,
                            UIColor *domeBot,
                            CGFloat headerProgress);

#endif
