#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CFNetwork/CFNetwork.h>
#import <ifaddrs.h>
#import <arpa/inet.h>
#import <dlfcn.h>
#import <unistd.h>
#include <math.h>
#include <objc/message.h>
#import "control_client.h"
#import "qr_scan.h"
#import "ui_theme.h"
#import "boykisser_field.h"
#import "bubble_field.h"
#import "themes_vc.h"
#import "server_cell.h"
#import "main_layout.h"
#import "update_install.h"
#import "meow.h"
#import "app_common.h"

@interface UIViewController (SenkoRotation)
@end

@implementation UIViewController (SenkoRotation)
- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)io {
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad)
        return YES;
    return io != UIInterfaceOrientationPortraitUpsideDown;
}

- (BOOL)shouldAutorotate {
    return YES;
}

- (NSUInteger)supportedInterfaceOrientations {
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad)
        return UIInterfaceOrientationMaskAll;
    return UIInterfaceOrientationMaskAllButUpsideDown;
}
@end

static BOOL ExternalTlsfixInstalled(void) {
    return access("/Library/MobileSubstrate/DynamicLibraries/tlsfix.dylib", F_OK) == 0;
}

NSString *SenkoAboutAppReport(void) {
    int tlsfix = ExternalTlsfixInstalled();
    return [NSString stringWithFormat:
            @"Full-device VLESS and AmneziaWG client for jailbroken iOS 5-10.\n"
             "Needs root for full-device routing.\n\n"
             "Protocols\n"
             "- VLESS + TCP (none / TLS / Reality+Vision)\n"
             "- VLESS + WebSocket / XHTTP\n"
             "- AmneziaWG\n"
             "- SOCKS5, HTTP(S) CONNECT\n\n"
             "Paths\n"
             "Control: %@\n"
             "Config: /var/root/Library/Preferences/senko.cfg\n"
             "Logs: /var/log/senkod.log, /var/log/senkoawgd.log\n\n"
             "TLS hook: %@\n\n"
             "Testers: ogeprint, rafal_official, nifty, wolfer, lineysom, lime, fr0n1k, inraxx, qualcomm",
            SENKO_SOCK,
            tlsfix
                ? @"external tlsfix present - senkotlsfix hooks stay off"
                : @"senkotlsfix (Safari TLS1.3 when MobileSubstrate is installed)"];
}

@interface AppDelegate : UIResponder <UIApplicationDelegate> {
    UIWindow *_window;
}
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    _window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    MainVC *vc = [[[MainVC alloc] init] autorelease];
    _window.rootViewController = vc;
    [_window makeKeyAndVisible];
    return YES;
}
- (void)dealloc { [_window release]; [super dealloc]; }
@end

int main(int argc, char **argv) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    if (!ExternalTlsfixInstalled())
        (void)dlopen("/usr/lib/senkotlsfix.dylib", RTLD_NOW | RTLD_GLOBAL);
    InitPalette();
    SenkoMeowInstallHooks();
    int rc = UIApplicationMain(argc, argv, nil, @"AppDelegate");
    [pool release];
    return rc;
}
