#ifndef SENKO_EMPTY_STATE_H
#define SENKO_EMPTY_STATE_H

#import <UIKit/UIKit.h>

/* what the list area shows while nothing is configured: what to do next, the
   device id panels bind a subscription to, and the two fastest ways in */
@interface SenkoEmptyStateView : UIView {
@public
/* the text sits on its own translucent plate: over a patterned wallpaper a bare
   caption is unreadable, and on a 3.5 inch screen the block does not always
   fit, so everything above the button row scrolls */
    UIScrollView *scroll;
    UIView   *textPlate;
    UILabel  *headline;
    UILabel  *body;
    UIView   *hwidPlate;
    UILabel  *hwidCaption;
    UILabel  *hwidValue;
    UIButton *hwidTap;
    UIButton *pasteButton;
    UIButton *scanButton;
}

- (void)applyTheme;
/* nil while the daemon has not answered yet */
- (void)setHWID:(NSString *)hwid;
- (void)flashCopiedNotice;

@end

#endif
