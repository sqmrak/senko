#ifndef SENKO_APP_CATALOG_H
#define SENKO_APP_CATALOG_H

#import <UIKit/UIKit.h>

/* one app installed on the device, as an app rule names it */
@interface SenkoInstalledApp : NSObject {
@public
    NSString *bundleID;
    NSString *name;
    NSString *bundlePath;
}
@end

/* the apps on this device, read from their bundles. the ui runs unsandboxed, so
   the bundle folders are readable on every jailbreak, and a private framework
   would not be there on the oldest and newest systems alike */
@interface SenkoAppCatalog : NSObject

/* every launchable app by name, each bundle identifier once. reads many files:
   call it off the main thread */
+ (NSArray *)installedApps;

/* the apps the last scan found, nil before the first. a screen shows these at once
   and scans again in the background */
+ (NSArray *)cachedApps;

/* the app's icon at the list's size, rounded, or a plain letter tile when the
   bundle has no icon file senko can read. main thread; cached */
+ (UIImage *)iconForApp:(SenkoInstalledApp *)app side:(CGFloat)side;

@end

#endif
