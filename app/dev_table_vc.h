#ifndef SENKO_DEV_TABLE_VC_H
#define SENKO_DEV_TABLE_VC_H

#import <UIKit/UIKit.h>

@class SenkoControl;

/* every developer screen is a grouped table in the current theme with the same
   nav chrome, so the setup lives here once instead of in each of them */
@interface DevTableVC : UIViewController <UITableViewDataSource,
                                          UITableViewDelegate,
                                          UIAlertViewDelegate> {
@protected
    UITableView  *_tv;
    SenkoControl *_ctl;
}

- (id)initWithControl:(SenkoControl *)control;

/* the screen name the crash reporter records, and the nav title */
- (NSString *)devTitle;
- (const char *)devScreenName;

/* a themed subtitle cell with the rounded group background already applied */
- (UITableViewCell *)devCellForTable:(UITableView *)tv
                           indexPath:(NSIndexPath *)ip
                                rows:(NSInteger)rows;

/* the one-line explanation under a group, or nil for a bare gap */
- (NSString *)devFooterForSection:(NSInteger)section;
- (NSString *)devHeaderForSection:(NSInteger)section;

/* an alert with one dismiss button, used for every result this screen reports */
- (void)devSay:(NSString *)title message:(NSString *)message;

/* a subclass that shows its own alerts hands the ones it does not own back */
- (void)alertView:(UIAlertView *)alert clickedButtonAtIndex:(NSInteger)index;

/* a destructive action always asks first. tag is handed back in the delegate */
- (void)devConfirm:(NSString *)title message:(NSString *)message
            button:(NSString *)button tag:(NSInteger)tag;

/* the confirmation the user accepted; the default does nothing */
- (void)devConfirmed:(NSInteger)tag;

@end

/* a read only page of text with a copy button: firewall rulesets, crash
   reports and the diagnostics bundle are all too long for an alert */
@interface DevTextVC : UIViewController {
    UITextView *_text;
    NSString   *_body;
/* UIViewController already declares _title on this sdk, so the page keeps its
   own name under a different one */
    NSString   *_pageTitle;
}

- (id)initWithTitle:(NSString *)title body:(NSString *)body;

@end

#endif
