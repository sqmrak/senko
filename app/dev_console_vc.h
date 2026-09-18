#ifndef SENKO_DEV_CONSOLE_VC_H
#define SENKO_DEV_CONSOLE_VC_H

#import <UIKit/UIKit.h>

@class SenkoControl;

/* the control socket without ssh: type a verb, read what the daemon answered.
   senkoctl already speaks this protocol from a terminal, and needing a second
   machine to send one line is the reason this screen exists */
@interface DevConsoleVC : UIViewController <UITextFieldDelegate> {
    UITextView   *_out;
    UITextField  *_input;
    SenkoControl *_ctl;
    NSMutableArray *_history;
    NSInteger     _historyAt;
    BOOL          _busy;
}

- (id)initWithControl:(SenkoControl *)control;

@end

#endif
