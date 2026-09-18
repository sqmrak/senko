#ifndef SENKO_DEV_TOOLS_VC_H
#define SENKO_DEV_TOOLS_VC_H

#import "dev_table_vc.h"

/* switches a normal install must not reach: each one either widens what senko
   listens on or stops a fallback from being tried */
@interface DevTogglesVC : DevTableVC <UIActionSheetDelegate> {
    NSMutableDictionary *_settings;
}
@end

@interface DevRescueVC : DevTableVC <UIActionSheetDelegate> {
    NSString *_hwid;
}
@end

#endif
