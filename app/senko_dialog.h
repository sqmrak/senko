#ifndef SENKO_DIALOG_H
#define SENKO_DIALOG_H

#import <UIKit/UIKit.h>

/* the app's own popup: a plate in the colours of the current theme with a
   sticker leaning over its top edge. it replaces UIAlertView, UIAlertController
   and UIActionSheet, which draw differently on every ios from 4.3 to 16, ignore
   the theme and cannot carry a sticker. it lives in a window of its own, so it
   covers modal screens and keeps working while the host rotates */

/* which sticker leans over the card */
typedef enum {
    SenkoDialogMoodAuto = -1,
    SenkoDialogMoodHappy = 0,  /* a note that needs nothing from the user */
    SenkoDialogMoodAsk = 1,    /* a question or a choice */
    SenkoDialogMoodInput = 2,  /* a field to fill in */
    SenkoDialogMoodError = 3   /* something failed */
} SenkoDialogMood;

typedef enum {
    SenkoDialogButtonNormal = 0,
    SenkoDialogButtonCancel,
    SenkoDialogButtonDestructive
} SenkoDialogButtonStyle;

@interface SenkoDialog : NSObject

@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *message;
@property (nonatomic, assign) SenkoDialogMood mood;
/* a tap outside the card runs the cancel button, or only closes the card when
   there is none. yes by default only for a dialog that has a cancel button */
@property (nonatomic, assign) BOOL dismissOnBackdrop;
/* the field added by addTextField, nil before that */
@property (nonatomic, readonly) UITextField *textField;

+ (SenkoDialog *)dialogWithTitle:(NSString *)title message:(NSString *)message;

/* every button closes the card first and then runs its handler. a cancel button
   sits on the left, the last normal button is the filled one */
- (void)addButtonWithTitle:(NSString *)title
                     style:(SenkoDialogButtonStyle)style
                   handler:(void (^)(SenkoDialog *dialog))handler;
/* one row of a list of choices, the way an action sheet shows them */
- (void)addItemWithTitle:(NSString *)title
                 handler:(void (^)(SenkoDialog *dialog))handler;
- (UITextField *)addTextFieldWithText:(NSString *)text
                          placeholder:(NSString *)placeholder
                             keyboard:(UIKeyboardType)keyboard;
/* a view the screen builds, laid out at the card's inner width and its own
   height */
- (void)setContentView:(UIView *)view;

- (void)show;
/* closes the card without running any handler */
- (void)dismiss;

@end

/* the questions the screens ask. every string is localized here, so a screen
   passes the english text */
void SenkoDialogAlert(NSString *title, NSString *message, SenkoDialogMood mood);
void SenkoDialogConfirm(NSString *title, NSString *message, NSString *action,
                        BOOL destructive, void (^confirm)(void));
/* a list of choices and a cancel button. the block gets the index of the label */
void SenkoDialogMenu(NSString *title, NSString *message, NSArray *labels,
                     void (^pick)(NSInteger index));
/* one line of text. the block runs only for the confirm button and gets the
   text as typed */
void SenkoDialogPrompt(NSString *title, NSString *message, NSString *text,
                       UIKeyboardType keyboard, NSString *action,
                       void (^done)(NSString *text));

#endif
