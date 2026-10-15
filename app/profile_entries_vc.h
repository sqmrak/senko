#ifndef SENKO_PROFILE_ENTRIES_VC_H
#define SENKO_PROFILE_ENTRIES_VC_H

#import <UIKit/UIKit.h>

@class SenkoControl;

/* what a subscription's routing profile says, entry by entry, as the panel wrote
   it. entries senko cannot match are marked, so a profile that does less than its
   author meant is visible instead of quietly partial */
@interface ProfileEntriesVC : UIViewController <UITableViewDataSource, UITableViewDelegate> {
    UITableView  *_tv;
    SenkoControl *_ctl;
    int           _sub;
    NSString     *_profileName;
    NSArray      *_entries;
    int           _total;
    BOOL          _loaded;
}

- (id)initWithControl:(SenkoControl *)control subscription:(int)sub name:(NSString *)name;

@end

#endif
