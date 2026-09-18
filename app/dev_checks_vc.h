#ifndef SENKO_DEV_CHECKS_VC_H
#define SENKO_DEV_CHECKS_VC_H

#import "dev_table_vc.h"

/* the four check modes exist already; the normal ui runs one of them behind a
   single button. here the mode is chosen by hand and the stage the path stops
   at is what the screen is for */
@interface DevChecksVC : DevTableVC <UIActionSheetDelegate> {
    NSArray  *_servers;
    NSArray  *_stages;
    NSString *_mode;
    NSString *_result;
    int       _serverIndex;
    BOOL      _running;
}
@end

#endif
