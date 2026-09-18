#ifndef SENKO_RULES_VC_H
#define SENKO_RULES_VC_H

#import <UIKit/UIKit.h>

@class SenkoControl;

/* the routing rule list. the daemon owns the rules, so this screen never keeps
   a copy of its own: every change is sent and the list is read back */
@interface RulesVC : UIViewController <UITableViewDataSource, UITableViewDelegate,
                                       UIActionSheetDelegate, UIAlertViewDelegate> {
    UITableView  *_tv;
    SenkoControl *_ctl;
    NSArray      *_rules;
    NSString     *_pendingAction;
    NSString     *_pendingType;
    BOOL          _loaded;
}

- (id)initWithControl:(SenkoControl *)control;

@end

#endif
