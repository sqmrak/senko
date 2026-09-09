#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CFNetwork/CFNetwork.h>
#import <ifaddrs.h>
#import <arpa/inet.h>
#import <dlfcn.h>
#import <unistd.h>
#include <stdio.h>
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

static NSString *SenkoReadLogTail(NSString *path, long maxBytes) {
    FILE *file = fopen([path fileSystemRepresentation], "rb");
    if (!file) return nil;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return nil; }
    long size = ftell(file);
    if (size < 0) { fclose(file); return nil; }
    long start = size > maxBytes ? size - maxBytes : 0;
    if (fseek(file, start, SEEK_SET) != 0) { fclose(file); return nil; }
    size_t length = (size_t)(size - start);
    NSMutableData *data = [NSMutableData dataWithLength:length];
    size_t got = length ? fread([data mutableBytes], 1, length, file) : 0;
    fclose(file);
    [data setLength:got];
    NSString *text = [[[NSString alloc] initWithData:data
                                            encoding:NSUTF8StringEncoding] autorelease];
    if (!text) text = [[[NSString alloc] initWithData:data
                                             encoding:NSISOLatin1StringEncoding] autorelease];
    if (start > 0 && [text length]) {
        NSRange newline = [text rangeOfString:@"\n"];
        if (newline.location != NSNotFound)
            text = [text substringFromIndex:newline.location + 1];
    }
    return text;
}

static NSString *SenkoTagLegacyLog(NSString *text, NSString *source) {
    if (![text length]) return @"";
    NSMutableString *tagged = [NSMutableString string];
    for (NSString *line in [text componentsSeparatedByString:@"\n"]) {
        if (![line length]) continue;
        [tagged appendFormat:@"[%@] %@\n", source, line];
    }
    return tagged;
}

@implementation LogsVC {

    UITextView *_textView;
    UISegmentedControl *_filter;
    NSString *_allLogs;
    SenkoControl *_ctl;

}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"System Logs";
    SenkoApplyScreenChrome(self.view);

    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemRefresh
                                                       target:self
                                                       action:@selector(loadLogs)] autorelease];

    _filter = [[UISegmentedControl alloc] initWithItems:
               [NSArray arrayWithObjects:SenkoLocalizedText(@"all"), @"senkod", @"awg",
                                         @"app", nil]];
    _filter.selectedSegmentIndex = 0;
    [_filter addTarget:self action:@selector(filterChanged:)
      forControlEvents:UIControlEventValueChanged];
    SenkoStyleGlassSegmented(_filter);
    [self.view addSubview:_filter];

    UIView *plate = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    plate.tag = 7701;
    plate.layer.cornerRadius = SenkoThemeCardRadius();
    plate.layer.borderWidth = 0;
    plate.layer.borderColor = [UIColor clearColor].CGColor;
    plate.opaque = NO;
    SenkoStyleTerminalPlate(plate);
    [self.view addSubview:plate];

    _textView = [[UITextView alloc] initWithFrame:CGRectZero];
    _textView.editable = NO;
    _textView.font = [UIFont fontWithName:@"Courier" size:11.0f];
    SenkoStyleTerminalText(_textView);
    [plate addSubview:_textView];

    [self layoutLogs];
    [self loadLogs];
}

- (void)layoutLogs {
    CGRect b = SenkoViewBounds(self.view);
    if (b.size.width < 2.0f || b.size.height < 2.0f) return;
    UIView *bg = [self.view viewWithTag:9111];
    if (bg) bg.frame = b;
    UIView *plate = [self.view viewWithTag:7701];
    if (!plate) return;
    CGFloat pad = 8.0f;
    _filter.frame = CGRectMake(pad, pad, b.size.width - pad * 2.0f, 30.0f);
    plate.frame = CGRectMake(pad, 46.0f, b.size.width - pad * 2.0f,
                             b.size.height - 46.0f - pad);
    _textView.frame = CGRectInset(plate.bounds, 6, 6);
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    SenkoCrashScreen("system logs");
    SenkoApplyScreenChrome(self.view);
    SenkoStyleGlassSegmented(_filter);
    [self layoutLogs];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutLogs];
}

- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)io
                                         duration:(NSTimeInterval)dur {
    (void)io; (void)dur;
    [self layoutLogs];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io {
    (void)io;
    [self layoutLogs];
}

- (void)dealloc {
    [_textView release];
    [_filter release];
    [_allLogs release];
    [_ctl release];
    [super dealloc];
}

- (void)showLogText:(NSString *)content {
    [_allLogs release];
    NSString *safe = SenkoRedactSecrets(content ? content : @"");
    safe = [safe stringByReplacingOccurrencesOfString:@"[AWG]" withString:@"[awg]"];
    _allLogs = [safe copy];
    [self filterChanged:_filter];
}

/* the daemon log cannot hold an app crash: the app is dead before it could
   send one, so the report the crash handler left on disk is folded in here */
static NSString *SenkoAppCrashSection(void) {
    NSString *report = SenkoCrashLastReport();
    BOOL safe = SenkoCrashSafeMode();
    if (![report length] && !safe) return @"";
/* the version the report names is the build that crashed, which is not always
   the build reading it: an update installed over a crash leaves the old report
   in place until this one proves it can start */
    BOOL foreign = [report length] &&
                   [report rangeOfString:SENKO_VERSION].location == NSNotFound;
    NSMutableString *tagged = [NSMutableString stringWithString:
        foreign ? @"[app] --- an earlier build crashed here; this one has not ---\n"
                : @"[app] --- previous launch failed ---\n"];
    [tagged appendFormat:@"[app] running: senko %@\n", SENKO_VERSION];
    if (safe)
        [tagged appendFormat:@"[app] safe mode active: %d launches in a row "
                              "never reached the first frame\n",
                             SenkoCrashFailedLaunches()];
    for (NSString *line in [report componentsSeparatedByString:@"\n"]) {
        if (![line length]) continue;
        [tagged appendFormat:@"[app] %@\n", line];
    }
    [tagged appendString:@"[app] --- end of report ---\n"];
    return tagged;
}

/* springboard writes which status bar it found and whether the vpn badge could
   be shown without touching the wifi glyph; the app cannot see its log */
static NSString *SenkoVPNIconSection(void) {
    NSString *line = [NSString stringWithContentsOfFile:
                      @"/var/mobile/Library/Preferences/com.senko.vpnicon.status"
                                               encoding:NSUTF8StringEncoding
                                                  error:NULL];
    line = [line stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![line length]) return @"";
    return [NSString stringWithFormat:@"[app] status bar: %@\n", line];
}

- (void)loadLogs {
    NSString *content = SenkoReadLogTail(@SENKO_SYSTEM_LOG, 60000);
    if (![content length]) {
        NSString *core = SenkoReadLogTail(@"/var/log/senkod.log", 30000);
        NSString *awg = SenkoReadLogTail(@"/var/log/senkoawgd.log", 30000);
        content = [NSString stringWithFormat:@"%@%@",
                   SenkoTagLegacyLog(core, @"senkod"),
                   SenkoTagLegacyLog(awg, @"awg")];
    }
    NSString *crash = [SenkoVPNIconSection()
                       stringByAppendingString:SenkoAppCrashSection()];
    if ([content length] || [crash length]) {
        [self showLogText:[crash stringByAppendingString:content ? content : @""]];
        return;
    }
/* the app runs as mobile and on some jailbreaks cannot open /var/log at all,
   so the daemon that owns the file reads it over the control socket */
    _textView.text = SenkoLocalizedText(@"Loading logs...");
    if (!_ctl) _ctl = [[SenkoControl alloc] initWithSocketPath:SENKO_SOCK];
    [_ctl daemonLogTail:^(NSString *text) {
        [self showLogText:[[SenkoVPNIconSection()
                            stringByAppendingString:SenkoAppCrashSection()]
                           stringByAppendingString:text ? text : @""]];
    }];
}

- (void)filterChanged:(UISegmentedControl *)sender {
    if (![_allLogs length]) {
        _textView.text = SenkoLocalizedText(@"No daemon logs available");
        return;
    }
    NSInteger selected = sender.selectedSegmentIndex;
    if (selected == 0) {
        _textView.text = _allLogs;
    } else {
        NSMutableString *shown = [NSMutableString string];
        for (NSString *line in [_allLogs componentsSeparatedByString:@"\n"]) {
            BOOL isApp = [line hasPrefix:@"[app]"];
            BOOL isAWG = !isApp &&
                         ([line rangeOfString:@"senkoawgd:" options:NSCaseInsensitiveSearch].location != NSNotFound ||
                          [line hasPrefix:@"[AWG]"] || [line hasPrefix:@"[awg]"]);
            if ((selected == 3 && isApp) || (selected == 2 && isAWG) ||
                (selected == 1 && !isAWG && !isApp))
                [shown appendFormat:@"%@\n", line];
        }
        if (selected == 3 && ![shown length])
            [shown appendString:SenkoLocalizedText(@"No app fault report")];
        _textView.text = shown;
    }
    if ([_textView.text length] > 0) {
        NSRange range = NSMakeRange([_textView.text length] - 1, 1);
        [_textView scrollRangeToVisible:range];
    }
}

@end
