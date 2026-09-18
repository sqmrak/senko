#ifndef SENKO_DEV_MENU_VC_H
#define SENKO_DEV_MENU_VC_H

#import "dev_table_vc.h"

/* which backend, firewall and pf variant this device actually ended up on. the
   facts come from the daemon, which is the only side that knows */
@interface DevMenuVC : DevTableVC
@end

/* the report, grouped so the choices come before the live numbers */
@interface DevFactsVC : DevTableVC {
    NSArray  *_groups;
    NSTimer  *_refresh;
    BOOL      _loaded;
}
@end

#endif
