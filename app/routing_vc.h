#ifndef SENKO_ROUTING_VC_H
#define SENKO_ROUTING_VC_H

#import <UIKit/UIKit.h>

@class SenkoControl;
@class SenkoGeoState;

/* where traffic goes: the routing profile a subscription delivered, the geo data
   it needs, the apps with their icons, and the domain and address rules. it is
   the one entry in settings; the daemon owns everything it shows, so the screen
   reads it back whenever it appears */
@interface RoutingVC : UIViewController <UITableViewDataSource, UITableViewDelegate> {
    UITableView  *_tv;
    SenkoControl *_ctl;
    NSArray      *_profiles;    /* SenkoRoutingProfile, one per subscription that sent one */
    int           _active;      /* the subscription whose profile is on, -1 for none */
    BOOL          _profilesLoaded;
    SenkoGeoState *_geo;
    NSArray      *_rules;       /* every user rule */
    NSTimer      *_geoTimer;    /* polls the download while one runs */
    NSString     *_notice;      /* the daemon's last answer to a switch, for the footer */
}

- (id)initWithControl:(SenkoControl *)control;

@end

#endif
