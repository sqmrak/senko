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
#import "home_layout.h"
#import "update_install.h"
#import "meow.h"
#import "app_common.h"
#include "../common/senko_paths.h"

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
    return access(SENKO_SUBSTRATE_DIR "/tlsfix.dylib", F_OK) == 0;
}

NSString *SenkoAboutAppReport(void) {
    int tlsfix = ExternalTlsfixInstalled();
    BOOL systemTLS = [[[UIDevice currentDevice] systemVersion] floatValue] >= 12.0f;
    if (SenkoLanguageIsRussian()) {
        return [NSString stringWithFormat:
                @"vpn для всего устройства\n"
                 "приложения и системный трафик идут через выбранный профиль. для маршрутизации нужен root.\n\n"
                 "транспорты\n"
                 "tcp · tls · reality + vision\n"
                 "websocket · xhttp · grpc\n"
                 "amneziawg · socks5 · http(s) connect\n\n"
                 "совместимость\n"
                 "ios 5-15 · armv7 + arm64\n"
                 "интерфейс подстраивается под компактные, plus, x/mini/max и ipad-экраны.\n\n"
                 "безопасность и диагностика\n"
                 "control socket с токеном · защита подписок от ssrf · скрытие секретов в логах · настоящая проверка транспорта.\n"
                 "общий журнал: /var/log/senko-system.log\n\n"
                 "tls-режим: %@\n\n"
                 "тестировали: @inraxx, @s3dativee, @rafal_official, @RealPetuh, @QuaIcomm, @belo4kaFLUNI, @Wolfer_QUIC, @fluffynifty, @not_a_modder, @Lineysom, @Lime_iOS6, @fr0n1k, @ogeprint, @CookieValerka",
                systemTLS
                    ? @"системный tls (compatibility hook не внедряется)"
                    : tlsfix
                    ? @"внешний tlsfix найден, хуки senkotlsfix отключены"
                    : @"senkotlsfix для tls1.3 safari при установленном mobilesubstrate"];
    }
    return [NSString stringWithFormat:
            @"full-device vpn\n"
             "apps and system traffic use the selected profile. root is required for routing.\n\n"
             "transports\n"
             "tcp · tls · reality + vision\n"
             "websocket · xhttp · grpc\n"
             "amneziawg · socks5 · http(s) connect\n\n"
             "compatibility\n"
             "ios 5-15 · armv7 + arm64\n"
             "adaptive layouts for compact, plus, x/mini/max and ipad displays.\n\n"
             "security and diagnostics\n"
             "token-authenticated control socket · subscription ssrf protection · secret redaction · real transport checks.\n"
             "combined log: /var/log/senko-system.log\n\n"
             "tls mode: %@\n\n"
             "testers: @inraxx, @s3dativee, @rafal_official, @RealPetuh, @QuaIcomm, @belo4kaFLUNI, @Wolfer_QUIC, @fluffynifty, @not_a_modder, @Lineysom, @Lime_iOS6, @fr0n1k, @ogeprint, @CookieValerka",
            systemTLS
                ? @"system tls (the compatibility hook is not injected)"
                : tlsfix
                ? @"external tlsfix present - senkotlsfix hooks stay off"
                : @"senkotlsfix (safari tls1.3 when mobilesubstrate is installed)"];
}

/* uikit looks the main window up through the delegate on systems that expect a
   scene manifest, so the property is part of the launch contract, not decor */
@interface AppDelegate : UIResponder <UIApplicationDelegate> {
    UIWindow *_window;
}
@property (nonatomic, retain) UIWindow *window;
@end

@implementation AppDelegate
@synthesize window = _window;
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    (void)app; (void)opts;
    UIWindow *w = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    MainVC *vc = [[[MainVC alloc] init] autorelease];
    w.rootViewController = vc;
    self.window = w;
    [w release];
    [_window makeKeyAndVisible];
    return YES;
}
- (void)dealloc { [_window release]; [super dealloc]; }
@end

int main(int argc, char **argv) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    if (!ExternalTlsfixInstalled())
        (void)dlopen(SENKO_USR_LIB "/senkotlsfix.dylib", RTLD_NOW | RTLD_GLOBAL);
    SenkoLocalizationInstall();
    InitPalette();
    SenkoMeowInstallHooks();
    int rc = UIApplicationMain(argc, argv, nil, @"AppDelegate");
    [pool release];
    return rc;
}
