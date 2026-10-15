#ifndef SENKO_APP_ROUTING_VC_H
#define SENKO_APP_ROUTING_VC_H

#import <UIKit/UIKit.h>

@class SenkoControl;
@class SenkoInstalledApp;

/* the apps installed on the device, each with its icon and the way its traffic
   goes. an app rule is an ordinary rule of the daemon with the bundle identifier
   as its value, so this screen keeps no list of its own */
@interface AppRoutingVC : UIViewController <UITableViewDataSource, UITableViewDelegate,
                                            UISearchBarDelegate, UIActionSheetDelegate> {
    UITableView       *_tv;
    UISearchBar       *_search;
    SenkoControl      *_ctl;
    NSArray           *_apps;      /* every installed app, by name */
    NSArray           *_ruled;     /* the apps that have a rule, by name */
    NSArray           *_plain;     /* the rest, or the matches of a search */
    NSDictionary      *_rules;     /* bundle identifier to its SenkoRule */
    NSString          *_query;
    SenkoInstalledApp *_picked;
    BOOL               _appsLoaded;
    BOOL               _rulesLoaded;
}

- (id)initWithControl:(SenkoControl *)control;

@end

#endif
