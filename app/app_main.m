#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CFNetwork/CFNetwork.h>
#import <ifaddrs.h>
#import <arpa/inet.h>
#import <dlfcn.h>
#import <unistd.h>
#include <fcntl.h>
#include <notify.h>
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
#import "crash_report.h"
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

NSString *SenkoSponsorURL(void) {
    return @"https://2xvpn.shop/dashboard/buy?promo=SENKO";
}

NSString *SenkoAboutAppReport(void) {
    int tlsfix = ExternalTlsfixInstalled();
    BOOL systemTLS = [[[UIDevice currentDevice] systemVersion] floatValue] >= 12.0f;
    if (SenkoLanguageIsRussian()) {
        return [NSString stringWithFormat:
                @"VPN для всего устройства\n"
                 "Приложения и системный трафик идут через выбранный профиль. "
                 "Для маршрутизации нужен root, и джейлбрейк его уже даёт.\n\n"
                 "Транспорты\n"
                 "TCP · TLS · REALITY + Vision\n"
                 "WebSocket · XHTTP · gRPC\n"
                 "AmneziaWG · SOCKS5 · HTTP(S) CONNECT\n\n"
                 "Совместимость\n"
                 "iOS 5-15 · armv7 + arm64\n"
                 "Интерфейс подстраивается под компактные, Plus, X/mini/Max и iPad-экраны.\n\n"
                 "Безопасность и диагностика\n"
                 "Control socket с токеном · защита подписок от SSRF · скрытие секретов "
                 "в логах · настоящая проверка транспорта.\n"
                 "Общий журнал: /var/log/senko-system.log\n\n"
                 "Режим TLS: %@\n\n"
                 "Тестировали: @inraxx, @s3dativee, @rafal_official, @RealPetuh, @QuaIcomm, @belo4kaFLUNI, @Wolfer_QUIC, @fluffynifty, @not_a_modder, @Lineysom, @Lime_iOS6, @fr0n1k, @ogeprint, @CookieValerka, @ra1n_developer",
                systemTLS
                    ? @"системный TLS, compatibility hook не внедряется"
                    : tlsfix
                    ? @"внешний tlsfix найден, хуки senkotlsfix отключены"
                    : @"senkotlsfix для TLS 1.3 в Safari при установленном MobileSubstrate"];
    }
    return [NSString stringWithFormat:
            @"Full-device VPN\n"
             "Apps and system traffic use the selected profile. Routing needs root, "
             "and a jailbreak already provides it.\n\n"
             "Transports\n"
             "TCP · TLS · REALITY + Vision\n"
             "WebSocket · XHTTP · gRPC\n"
             "AmneziaWG · SOCKS5 · HTTP(S) CONNECT\n\n"
             "Compatibility\n"
             "iOS 5-15 · armv7 + arm64\n"
             "Adaptive layouts for compact, Plus, X/mini/Max and iPad displays.\n\n"
             "Security and diagnostics\n"
             "Token-authenticated control socket · subscription SSRF protection · "
             "secret redaction · real transport checks.\n"
             "Combined log: /var/log/senko-system.log\n\n"
             "TLS mode: %@\n\n"
             "Testers: @inraxx, @s3dativee, @rafal_official, @RealPetuh, @QuaIcomm, @belo4kaFLUNI, @Wolfer_QUIC, @fluffynifty, @not_a_modder, @Lineysom, @Lime_iOS6, @fr0n1k, @ogeprint, @CookieValerka, @ra1n_developer",
            systemTLS
                ? @"system TLS, the compatibility hook is not injected"
                : tlsfix
                ? @"external tlsfix present, senkotlsfix hooks stay off"
                : @"senkotlsfix, Safari TLS 1.3 when MobileSubstrate is installed"];
}

/* the daemon writes the id it sends as x-hwid to a file the ui can read too.
   reading it costs nothing and cannot time out, unlike the control round trip
   that used to be the only source: a daemon busy under a live tunnel misses
   the reply window, and the screen was then stuck reading "not available yet"
   for the rest of the session */
NSString *SenkoSharedDeviceHWID(void) {
    NSString *shared = [NSString stringWithContentsOfFile:@SENKO_HWID_PATH
                                                 encoding:NSUTF8StringEncoding
                                                    error:NULL];
    shared = [shared stringByTrimmingCharactersInSet:
              [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([shared length] >= 10 && [shared length] <= 64) return shared;
    return nil;
}

BOOL SenkoVPNBadgeEnabled(void) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    if (![d objectForKey:SENKO_VPN_BADGE_KEY]) return YES;
    return [d boolForKey:SENKO_VPN_BADGE_KEY];
}

void SenkoVPNBadgeSetEnabled(BOOL enabled) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:enabled forKey:SENKO_VPN_BADGE_KEY];
    [d synchronize];
    if (enabled) {
        unlink(SENKO_VPN_BADGE_OFF_PATH);
    } else {
        int fd = open(SENKO_VPN_BADGE_OFF_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) close(fd);
    }
    /* the same darwin name senkod posts, so springboard repaints at once */
    (void)notify_post("com.senko.vpnicon.changed");
}

/* uikit looks the main window up through the delegate on systems that expect a
   scene manifest, so the property is part of the launch contract, not decor */
@interface AppDelegate : UIResponder <UIApplicationDelegate> {
    UIWindow *_window;
}
@property (nonatomic, retain) UIWindow *window;
- (void)senkoLaunchSettled;
- (void)senkoOfferSafeModeReport;
@end

@implementation AppDelegate
@synthesize window = _window;
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    (void)app; (void)opts;
    SenkoCrashStage("window");
    UIWindow *w = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    SenkoCrashStage("main controller");
    MainVC *vc = [[[MainVC alloc] init] autorelease];
    w.rootViewController = vc;
    self.window = w;
    [w release];
    /* the launch is not over when this method returns: the first frame is
       rendered by the commit that follows it, and that render is where a
       core image or core ui fault lands. marking the launch good here would
       call every one of those crashes a clean start */
    SenkoCrashStage("first frame");
    [CATransaction begin];
    [CATransaction setCompletionBlock:^{
        SenkoCrashLaunchComplete();
        [self senkoOfferSafeModeReport];
    }];
    [_window makeKeyAndVisible];
    [CATransaction commit];
    /* a screen that never finishes its first commit would stay in safe mode
       forever, so a plain run loop backstop releases it either way */
    [self performSelector:@selector(senkoLaunchSettled)
               withObject:nil
               afterDelay:6.0];
    return YES;
}

- (void)senkoLaunchSettled {
    SenkoCrashLaunchComplete();
    [self senkoOfferSafeModeReport];
}

/* safe mode is worth nothing if the person holding the phone cannot tell it
   happened, so the run that recovers says so and offers the report */
- (void)senkoOfferSafeModeReport {
    static BOOL shown = NO;
    if (shown || !SenkoCrashSafeMode()) return;
    shown = YES;
    NSString *body = [NSString stringWithFormat:
                      SenkoLocalizedText(@"Senko failed to start %d times and "
                                          "is running with the stock theme. The "
                                          "report is in Logs."),
                      SenkoCrashFailedLaunches()];
    UIAlertView *a = [[[UIAlertView alloc]
                       initWithTitle:SenkoLocalizedText(@"Safe mode")
                             message:body
                            delegate:nil
                   cancelButtonTitle:SenkoLocalizedText(@"OK")
                   otherButtonTitles:nil] autorelease];
    [a show];
}

- (void)dealloc { [_window release]; [super dealloc]; }
@end

int main(int argc, char **argv) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    SenkoCrashInstall();
    SenkoCrashStage("tlsfix dlopen");
    if (!ExternalTlsfixInstalled())
        (void)dlopen(SENKO_USR_LIB "/senkotlsfix.dylib", RTLD_NOW | RTLD_GLOBAL);
    SenkoCrashStage("localization");
    SenkoLocalizationInstall();
    SenkoCrashStage("palette");
    InitPalette();
    SenkoCrashStage("sfx hooks");
    SenkoMeowInstallHooks();
    SenkoCrashStage("uikit");
    int rc = UIApplicationMain(argc, argv, nil, @"AppDelegate");
    [pool release];
    return rc;
}
