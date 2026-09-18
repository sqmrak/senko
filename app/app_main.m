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

static BOOL SenkoTlsfixAlreadyLoaded(void) {
    void *handle = dlopen(SENKO_USR_LIB "/senkotlsfix.dylib",
                          RTLD_LAZY | RTLD_NOLOAD);
    if (!handle) return NO;
    dlclose(handle);
    return YES;
}

static BOOL SenkoLegacyTlsfixRequired(void) {
#if SENKO_STOCK_NATIVE
    return NO;
#else
    NSArray *parts = [[[UIDevice currentDevice] systemVersion]
                      componentsSeparatedByString:@"."];
    NSInteger major = [parts count] ? [[parts objectAtIndex:0] integerValue] : 0;
    return major > 0 && major < 12;
#endif
}

NSString *SenkoSponsorURL(void) {
    return @"https://2xvpn.shop/dashboard/buy?promo=SENKO";
}

/* five taps is the established gesture for a hidden screen, and nothing else in
   this app reacts to a repeated tap, so it cannot shadow a real control */
#define SENKO_DEV_MENU_TAPS 5

BOOL SenkoDevMenuEnabled(void) {
    return [[NSUserDefaults standardUserDefaults] boolForKey:SENKO_DEV_MENU_KEY];
}

void SenkoSetDevMenuEnabled(BOOL enabled) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:enabled forKey:SENKO_DEV_MENU_KEY];
    [d synchronize];
/* hiding the section has to take its overlay with it, or the frame counter
   outlives the only screen that can turn it off */
    if (!enabled) SenkoFPSOverlaySetEnabled(NO);
}

static int gDevMenuTaps;
static NSTimeInterval gDevMenuLastTap;

int SenkoDevMenuTapsLeft(void) {
    NSTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
/* a tap two seconds after the previous one is a new attempt, not the sixth tap
   of an old one */
    if (now - gDevMenuLastTap > 2.0) gDevMenuTaps = 0;
    gDevMenuLastTap = now;
    if (++gDevMenuTaps >= SENKO_DEV_MENU_TAPS) {
        gDevMenuTaps = 0;
        return 0;
    }
    return SENKO_DEV_MENU_TAPS - gDevMenuTaps;
}

void SenkoDevMenuResetTaps(void) {
    gDevMenuTaps = 0;
    gDevMenuLastTap = 0;
}

NSString *SenkoMaskSecret(NSString *value) {
    NSString *raw = [value stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![raw length]) return @"";
/* below nine characters the two ends would be the whole value, so there is
   nothing left to mask and the value is replaced instead */
    if ([raw length] < 9) return @"<hidden>";
    return [NSString stringWithFormat:@"%@...%@",
            [raw substringToIndex:4],
            [raw substringFromIndex:[raw length] - 4]];
}

/* one window above the app, so a push or a rotation cannot take it away and
   nothing it measures is moved by its presence */
@interface SenkoFPSOverlay : NSObject {
    UIWindow        *_window;
    UILabel         *_label;
    CADisplayLink   *_link;
    NSTimeInterval   _windowStart;
    NSUInteger       _frames;
}
+ (SenkoFPSOverlay *)shared;
- (void)setEnabled:(BOOL)enabled;
@end

@implementation SenkoFPSOverlay

+ (SenkoFPSOverlay *)shared {
    static SenkoFPSOverlay *shared;
    if (!shared) shared = [[SenkoFPSOverlay alloc] init];
    return shared;
}

- (void)tick:(CADisplayLink *)link {
    NSTimeInterval now = link.timestamp;
    if (_windowStart == 0) {
        _windowStart = now;
        _frames = 0;
        return;
    }
    ++_frames;
    NSTimeInterval span = now - _windowStart;
    if (span < 0.5) return;
    _label.text = [NSString stringWithFormat:@" %.0f fps ", (double)_frames / span];
    _windowStart = now;
    _frames = 0;
}

- (void)setEnabled:(BOOL)enabled {
    if (!enabled) {
        [_link invalidate];
        [_link release];
        _link = nil;
        _window.hidden = YES;
        [_label release];
        _label = nil;
        [_window release];
        _window = nil;
        return;
    }
    if (_window) return;
/* CADisplayLink is the only clock that counts the frames uikit actually drew,
   and it exists from ios 3.1 */
    CGRect bounds = [[UIScreen mainScreen] bounds];
    _window = [[UIWindow alloc] initWithFrame:
               CGRectMake(bounds.size.width - 76.0f, 2.0f, 74.0f, 18.0f)];
    _window.backgroundColor = [UIColor clearColor];
    _window.userInteractionEnabled = NO;
    _window.windowLevel = UIWindowLevelStatusBar + 1.0f;
    _label = [[UILabel alloc] initWithFrame:_window.bounds];
    _label.backgroundColor = [UIColor colorWithWhite:0 alpha:0.55f];
    _label.textColor = [UIColor colorWithRed:0.4f green:1.0f blue:0.5f alpha:1.0f];
    _label.font = [UIFont boldSystemFontOfSize:11.0f];
    _label.textAlignment = NSTextAlignmentRight;
    _label.text = @" -- fps ";
    [_window addSubview:_label];
    _window.hidden = NO;
    _windowStart = 0;
    _frames = 0;
    _link = [[CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)] retain];
    [_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

@end

BOOL SenkoFPSOverlayEnabled(void) {
    return [[NSUserDefaults standardUserDefaults] boolForKey:SENKO_FPS_OVERLAY_KEY];
}

void SenkoFPSOverlaySetEnabled(BOOL enabled) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:enabled forKey:SENKO_FPS_OVERLAY_KEY];
    [d synchronize];
    [[SenkoFPSOverlay shared] setEnabled:enabled];
}

NSString *SenkoAboutAppReport(void) {
    int tlsfix = ExternalTlsfixInstalled();
    BOOL systemTLS = [[[UIDevice currentDevice] systemVersion] floatValue] >= 12.0f;
    if (SenkoLanguageIsChinese()) {
        return [NSString stringWithFormat:
                @"全设备 VPN\n"
                 "应用和系统流量使用选中的配置。路由需要 root，越狱已经提供了权限。\n\n"
                 "传输协议\n"
                 "TCP · TLS · REALITY + Vision\n"
                 "WebSocket · XHTTP · gRPC\n"
                 "AmneziaWG · SOCKS5 · HTTP(S) CONNECT\n\n"
                 "兼容性\n"
                 "iOS 5-16 · armv7 + arm64 + arm64e\n\n"
                 "安全与诊断\n"
                 "带令牌认证的控制 socket · 防止订阅 SSRF · 日志隐藏密钥 · 真实传输检查。\n"
                 "合并日志：/var/log/senko-system.log\n\n"
                 "TLS 模式：%@\n\n"
                 "测试者：@inraxx, @s3dativee, @rafal_official, @RealPetuh, @QuaIcomm, @belo4kaFLUNI, @Wolfer_QUIC, @fluffynifty, @not_a_modder, @Lineysom, @Lime_iOS6, @fr0n1k, @ogeprint, @CookieValerka, @ra1n_developer",
                systemTLS
                    ? @"系统 TLS，不注入兼容性 hook"
                    : tlsfix
                    ? @"检测到外部 tlsfix，保持 senkotlsfix hook 关闭"
                    : @"senkotlsfix，用于 MobileSubstrate 下 Safari TLS 1.3"];
    }
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
                 "iOS 5-16 · armv7 + arm64 + arm64e\n\n"
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
             "iOS 5-16 · armv7 + arm64 + arm64e\n\n"
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

BOOL SenkoClassicHomeEnabled(void) {
    return [[NSUserDefaults standardUserDefaults] boolForKey:SENKO_CLASSIC_HOME_KEY];
}

void SenkoSetClassicHomeEnabled(BOOL enabled) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:enabled forKey:SENKO_CLASSIC_HOME_KEY];
    [d synchronize];
}


/* uikit looks the main window up through the delegate on systems that expect a
   scene manifest, so the property is part of the launch contract, not decor */
@interface AppDelegate : UIResponder <UIApplicationDelegate> {
    UIWindow *_window;
}
@property (nonatomic, retain) UIWindow *window;
- (BOOL)handleSenkoURL:(NSURL *)url;
- (void)handleLaunchURL:(NSURL *)url;
- (void)senkoLaunchSettled;
- (void)senkoOfferSafeModeReport;
@end

@implementation AppDelegate
@synthesize window = _window;
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    (void)app; (void)opts;
    /* new installs sort checked profiles by latency, while an explicit stored
       order stays available to people who chose it before this default */
    [[NSUserDefaults standardUserDefaults] registerDefaults:
     [NSDictionary dictionaryWithObject:[NSNumber numberWithInteger:SenkoSortPing]
                                 forKey:SENKO_SERVER_SORT_KEY]];
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
/* the overlay is a device setting, so it comes back on the next launch. it is
   put up after the main window so it stays above it */
    if (SenkoDevMenuEnabled() && SenkoFPSOverlayEnabled())
        SenkoFPSOverlaySetEnabled(YES);
    /* a screen that never finishes its first commit would stay in safe mode
       forever, so a plain run loop backstop releases it either way */
    [self performSelector:@selector(senkoLaunchSettled)
               withObject:nil
               afterDelay:6.0];
    NSURL *launchURL = [opts objectForKey:UIApplicationLaunchOptionsURLKey];
    if (launchURL)
        [self performSelector:@selector(handleLaunchURL:)
                   withObject:launchURL
                   afterDelay:0.1];
    return YES;
}

- (BOOL)handleSenkoURL:(NSURL *)url {
    if (![[[url scheme] lowercaseString] isEqualToString:@"senko"])
        return NO;
    NSString *action = [[[url host] length] ? [url host] : [url path]
                         lowercaseString];
    if (![action isEqualToString:@"toggle"])
        return NO;
    [[NSNotificationCenter defaultCenter]
        postNotificationName:SENKO_WIDGET_TOGGLE_NOTIFICATION object:nil];
    return YES;
}

- (void)handleLaunchURL:(NSURL *)url {
    [self handleSenkoURL:url];
}

- (BOOL)application:(UIApplication *)application
            openURL:(NSURL *)url
  sourceApplication:(NSString *)sourceApplication
         annotation:(id)annotation {
    (void)application;
    (void)sourceApplication;
    (void)annotation;
    return [self handleSenkoURL:url];
}

- (BOOL)application:(UIApplication *)application handleOpenURL:(NSURL *)url {
    (void)application;
    return [self handleSenkoURL:url];
}

- (BOOL)application:(UIApplication *)application
            openURL:(NSURL *)url
            options:(NSDictionary *)options {
    (void)application;
    (void)options;
    return [self handleSenkoURL:url];
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
    if (SenkoLegacyTlsfixRequired() && !ExternalTlsfixInstalled() &&
        !SenkoTlsfixAlreadyLoaded())
        (void)dlopen(SENKO_USR_LIB "/senkotlsfix.dylib", RTLD_NOW | RTLD_GLOBAL);
    SenkoCrashStage("tlsfix ready");
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
