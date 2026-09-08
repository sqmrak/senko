#ifndef SENKO_EMPTY_STATE_H
#define SENKO_EMPTY_STATE_H

#import <UIKit/UIKit.h>

/* what the list area shows while nothing is configured: what to do next, the
   device id panels bind a subscription to, and the two fastest ways in */
@interface SenkoEmptyStateView : UIView {
@public
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
