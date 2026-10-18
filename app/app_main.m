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
#import "update_install.h"
#import "meow.h"
#import "app_common.h"
#import "crash_report.h"
#import "emoji_text.h"
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
    NSArray *parts = [[[UIDevice currentDevice] systemVersion]
                      componentsSeparatedByString:@"."];
    NSInteger major = [parts count] ? [[parts objectAtIndex:0] integerValue] : 0;
    return major > 0 && major < 12;
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

NSString *SenkoAboutTLSMode(void) {
    BOOL systemTLS = [[[UIDevice currentDevice] systemVersion] floatValue] >= 12.0f;
    if (systemTLS)
        return SenkoLocalizedText(@"system TLS, the compatibility hook is not injected");
    if (ExternalTlsfixInstalled())
        return SenkoLocalizedText(@"external tlsfix present, senkotlsfix hooks stay off");
    return SenkoLocalizedText(@"senkotlsfix, Safari TLS 1.3 when MobileSubstrate is installed");
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

BOOL SenkoAutoServerEnabled(void) {
    return [[NSUserDefaults standardUserDefaults] boolForKey:SENKO_AUTO_SERVER_KEY];
}

void SenkoSetAutoServerEnabled(BOOL enabled) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:enabled forKey:SENKO_AUTO_SERVER_KEY];
    [d synchronize];
}

NSString *SenkoFormatBytes(unsigned long long value) {
    double amount = (double)value;
    NSArray *units = SenkoLanguageIsRussian()
        ? [NSArray arrayWithObjects:@"Б", @"КБ", @"МБ", @"ГБ", @"ТБ", nil]
        : [NSArray arrayWithObjects:@"B", @"KB", @"MB", @"GB", @"TB", nil];
    NSUInteger unit = 0;
    while (amount >= 1024.0 && unit + 1 < [units count]) {
        amount /= 1024.0;
        unit++;
    }
    NSString *number = [NSString stringWithFormat:(amount >= 10.0 || unit == 0)
        ? @"%.0f" : @"%.1f", amount];
    if (SenkoLanguageIsRussian())
        number = [number stringByReplacingOccurrencesOfString:@"." withString:@","];
    return [NSString stringWithFormat:@"%@ %@", number, [units objectAtIndex:unit]];
}

/* link speeds are quoted in bits, so the tiles read like the carrier's plan.
   the unit follows the amount: a fixed kbit/s rounded a quiet keepalive to 0 */
NSString *SenkoFormatRate(double bytesPerSecond) {
    static NSString *const units[] = { @"bit/s", @"Kbit/s", @"Mbit/s", @"Gbit/s" };
    double amount = bytesPerSecond > 0.0 ? bytesPerSecond * 8.0 : 0.0;
    size_t unit = 0;
    while (amount >= 1000.0 && unit + 1 < sizeof units / sizeof units[0]) {
        amount /= 1000.0;
        unit++;
    }
    NSString *number = [NSString stringWithFormat:(unit == 0 || amount >= 10.0)
        ? @"%.0f" : @"%.1f", amount];
    if (SenkoLanguageIsRussian())
        number = [number stringByReplacingOccurrencesOfString:@"." withString:@","];
    return [NSString stringWithFormat:@"%@ %@", number, SenkoLocalizedText(units[unit])];
}


/* uikit looks the main window up through the delegate on systems that expect a
   scene manifest, so the property is part of the launch contract, not decor */
@interface AppDelegate : UIResponder <UIApplicationDelegate> {
    UIWindow *_window;
}
@property (nonatomic, retain) UIWindow *window;
- (BOOL)handleSenkoURL:(NSURL *)url;
- (void)handleLaunchURL:(NSURL *)url;
- (void)loadLegacyTlsfix;
- (void)senkoLaunchSettled;
- (void)senkoOfferSafeModeReport;
- (void)applyThemeAppearance:(NSNotification *)note;
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
    SenkoCrashStage("palette");
    InitPalette();
    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(applyThemeAppearance:)
        name:SenkoThemeDidChangeNotification object:nil];
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
    [self applyThemeAppearance:nil];
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
    /* ios 5 has no process-wide injection for senko, but loading the shim
       before UIApplicationMain lets its fishhook scan block the launch watchdog */
    [self performSelector:@selector(loadLegacyTlsfix)
               withObject:nil
               afterDelay:0.1];
    NSURL *launchURL = [opts objectForKey:UIApplicationLaunchOptionsURLKey];
    if (launchURL)
        [self performSelector:@selector(handleLaunchURL:)
                   withObject:launchURL
                   afterDelay:0.1];
    return YES;
}

- (void)loadLegacyTlsfix {
    static BOOL attempted;
    if (attempted) return;
    attempted = YES;
    if (!SenkoLegacyTlsfixRequired() || ExternalTlsfixInstalled() ||
        SenkoTlsfixAlreadyLoaded())
        return;

    void *handle = dlopen(SENKO_USR_LIB "/senkotlsfix.dylib",
                          RTLD_NOW | RTLD_GLOBAL);
    if (handle) return;
    const char *error = dlerror();
    NSLog(@"Senko: cannot load senkotlsfix.dylib: %s",
          error ? error : "unknown error");
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

- (void)applyThemeAppearance:(NSNotification *)note {
    (void)note;
    BOOL modern = [UIViewController instancesRespondToSelector:@selector(setNeedsStatusBarAppearanceUpdate)];
    UIStatusBarStyle style = SenkoThemeIsLight() ? UIStatusBarStyleDefault
        : (UIStatusBarStyle)(modern ? 1 : 2);
    [[UIApplication sharedApplication] setStatusBarStyle:style animated:NO];
    if ([_window respondsToSelector:@selector(setOverrideUserInterfaceStyle:)])
        ((void (*)(id, SEL, NSInteger))objc_msgSend)(_window,
            @selector(setOverrideUserInterfaceStyle:), SenkoThemeIsLight() ? 1 : 2);
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_window release];
    [super dealloc];
}
@end

int main(int argc, char **argv) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    SenkoCrashInstall();
    SenkoCrashStage("localization");
    SenkoLocalizationInstall();
    SenkoEmojiTextInstall();
    SenkoCrashStage("sfx hooks");
    SenkoMeowInstallHooks();
    SenkoCrashStage("uikit");
    /* the application subclass sees touches first, which is how senko-material
       starts a ripple at the finger on any control */
    int rc = UIApplicationMain(argc, argv, @"SenkoApplication", @"AppDelegate");
    [pool release];
    return rc;
}
