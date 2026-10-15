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
#import "update_install.h"
#import "meow.h"
#import "app_common.h"
#import "card_kit.h"
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

/* how a log line reads at a glance: faults in red, retries in amber, the bracket
   tags of the app, the status bar and the amneziawg helper in the accent */
typedef enum { SenkoLogLineNormal = 0, SenkoLogLineFault, SenkoLogLineWarn, SenkoLogLineTag } SenkoLogLineKind;

static BOOL SenkoLogLineHas(NSString *line, NSString *needle) {
    return [line rangeOfString:needle options:NSCaseInsensitiveSearch].location != NSNotFound;
}

static SenkoLogLineKind SenkoClassifyLogLine(NSString *line) {
    static NSString *const faults[] = { @"fail", @"error", @"cannot", @"refused", @"denied",
                                        @"crash", @"fault", @"killed", nil };
    static NSString *const warns[] = { @"warn", @"retry", @"timeout", @"timed out", @"lost",
                                       @"unreachable", nil };
    for (int i = 0; faults[i]; ++i)
        if (SenkoLogLineHas(line, faults[i])) return SenkoLogLineFault;
    for (int i = 0; warns[i]; ++i)
        if (SenkoLogLineHas(line, warns[i])) return SenkoLogLineWarn;
    if ([line hasPrefix:@"["]) return SenkoLogLineTag;
    return SenkoLogLineNormal;
}

/* nsforegroundcolorattributename is ios 6; the symbol is looked up so the same
   binary runs on ios 5 and 4.3, where the log stays one colour */
static NSString *SenkoLogColorKey(void) {
    NSString **key = (NSString **)dlsym(RTLD_DEFAULT, "NSForegroundColorAttributeName");
    return key ? *key : nil;
}

static NSString *SenkoLogFontKey(void) {
    NSString **key = (NSString **)dlsym(RTLD_DEFAULT, "NSFontAttributeName");
    return key ? *key : nil;
}

#define LOGS_SHEET_ACTIONS 1

@implementation LogsVC {

    UITextView *_textView;
    UISegmentedControl *_filter;
    UISearchBar *_search;
    UILabel *_count;
    NSString *_allLogs;
    NSString *_shown;
    SenkoControl *_ctl;

}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"System Logs";
    SenkoApplyScreenChrome(self.view);

    UIBarButtonItem *refresh =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemRefresh
                                                       target:self
                                                       action:@selector(loadLogs)] autorelease];
    UIBarButtonItem *actions =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAction
                                                       target:self
                                                       action:@selector(actionsPressed:)] autorelease];
/* ios 4.3 has one slot on the right: refresh keeps it, copy and share need two */
    if ([self.navigationItem respondsToSelector:@selector(setRightBarButtonItems:)])
        ((void (*)(id, SEL, id))objc_msgSend)(self.navigationItem,
            @selector(setRightBarButtonItems:), [NSArray arrayWithObjects:refresh, actions, nil]);
    else
        self.navigationItem.rightBarButtonItem = refresh;

    _filter = [[UISegmentedControl alloc] initWithItems:
               [NSArray arrayWithObjects:SenkoLocalizedText(@"all"), @"senkod", @"awg",
                                         @"app", nil]];
    _filter.selectedSegmentIndex = 0;
    [_filter addTarget:self action:@selector(filterChanged:)
      forControlEvents:UIControlEventValueChanged];
    SenkoStyleGlassSegmented(_filter);
    [self.view addSubview:_filter];

    _search = [[UISearchBar alloc] initWithFrame:CGRectMake(0, 0, 320.0f, 44.0f)];
    _search.delegate = self;
    _search.placeholder = SenkoLocalizedText(@"Search logs");
    _search.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _search.autocorrectionType = UITextAutocorrectionTypeNo;
    [self.view addSubview:_search];

    _count = [[UILabel alloc] initWithFrame:CGRectZero];
    _count.backgroundColor = [UIColor clearColor];
    _count.font = SenkoFontBody(12.0f, NO);
    _count.textColor = kInkMuted;
    _count.textAlignment = NSTextAlignmentCenter;
    [self.view addSubview:_count];

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
    CGFloat pad = 12.0f;
    CGFloat width = b.size.width - pad * 2.0f;
    CGFloat column = SenkoCardColumnWidth(b.size.width);
    if (column + 32.0f < b.size.width) { pad = floorf((b.size.width - column) * 0.5f); width = column; }
    _filter.frame = CGRectMake(pad, 8.0f, width, 30.0f);
    _search.frame = CGRectMake(pad - 4.0f, 42.0f, width + 8.0f, 44.0f);
    CGFloat footer = 22.0f;
    CGFloat top = 90.0f;
    plate.frame = CGRectMake(pad, top, width, b.size.height - top - footer - 8.0f);
    _count.frame = CGRectMake(pad, CGRectGetMaxY(plate.frame) + 2.0f, width, 18.0f);
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
    [_search release];
    [_count release];
    [_shown release];
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
   be shown without touching the wifi glyph; the app cannot see its log.
   this is an append-only ring now, so the tail can show the sequence that
   led up to a wifi glyph report, not just the last state */
static NSString *SenkoStatusSection(void) {
    NSString *tail = SenkoReadLogTail(
        @"/var/mobile/Library/Preferences/com.senko.status.log", 8000);
    if (![tail length]) return @"";
    NSMutableString *tagged = [NSMutableString string];
    for (NSString *line in [tail componentsSeparatedByString:@"\n"]) {
        if (![line length]) continue;
        [tagged appendFormat:@"[status bar] %@\n", line];
    }
    return tagged;
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
    NSString *crash = [SenkoStatusSection()
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
        [self showLogText:[[SenkoStatusSection()
                            stringByAppendingString:SenkoAppCrashSection()]
                           stringByAppendingString:text ? text : @""]];
    }];
}

/* the lines of the chosen source that hold the search text */
- (NSString *)filteredLogText {
    NSInteger selected = _filter.selectedSegmentIndex;
    NSString *query = [_search.text stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSMutableString *shown = [NSMutableString string];
    for (NSString *line in [_allLogs componentsSeparatedByString:@"\n"]) {
        if (![line length]) continue;
        if (selected != 0) {
            BOOL isApp = [line hasPrefix:@"[app]"];
            BOOL isAWG = !isApp &&
                         ([line rangeOfString:@"amneziawg" options:NSCaseInsensitiveSearch].location != NSNotFound ||
                          [line rangeOfString:@"senkoawgd:" options:NSCaseInsensitiveSearch].location != NSNotFound ||
                          [line hasPrefix:@"[AWG]"] || [line hasPrefix:@"[awg]"]);
            BOOL keep = (selected == 3 && isApp) || (selected == 2 && isAWG) ||
                        (selected == 1 && !isAWG && !isApp);
            if (!keep) continue;
        }
        if ([query length] && !SenkoLogLineHas(line, query)) continue;
        [shown appendFormat:@"%@\n", line];
    }
    return shown;
}

- (UIColor *)colorForKind:(SenkoLogLineKind)kind {
    BOOL light = SenkoThemeIsLight();
    switch (kind) {
        case SenkoLogLineFault:
            return light ? [UIColor colorWithRed:0.76f green:0.17f blue:0.14f alpha:1.0f]
                         : [UIColor colorWithRed:1.00f green:0.45f blue:0.40f alpha:1.0f];
        case SenkoLogLineWarn:
            return light ? [UIColor colorWithRed:0.69f green:0.42f blue:0.02f alpha:1.0f]
                         : [UIColor colorWithRed:1.00f green:0.75f blue:0.30f alpha:1.0f];
        case SenkoLogLineTag:
            return light ? kAccentBlue : SenkoShadeColor(kAccentBlue, 0.25f);
        default:
            return _textView.textColor;
    }
}

/* each line is coloured by what it says. ios 6 and later take attributed text in
   a text view; before that the log is one colour, as it always was */
- (void)showText:(NSString *)text {
    NSString *colorKey = SenkoLogColorKey();
    NSString *fontKey = SenkoLogFontKey();
    if (![_textView respondsToSelector:@selector(setAttributedText:)] || !colorKey || !fontKey ||
        ![text length]) {
        _textView.text = text;
        return;
    }
    NSMutableAttributedString *attributed = [[[NSMutableAttributedString alloc]
                                              initWithString:text] autorelease];
    [attributed addAttribute:fontKey value:_textView.font range:NSMakeRange(0, [text length])];
    NSUInteger at = 0;
    UIColor *base = _textView.textColor;
    while (at < [text length]) {
        NSRange eol = [text rangeOfString:@"\n" options:0
                                    range:NSMakeRange(at, [text length] - at)];
        NSUInteger end = eol.location == NSNotFound ? [text length] : eol.location + 1;
        NSRange range = NSMakeRange(at, end - at);
        SenkoLogLineKind kind = SenkoClassifyLogLine([text substringWithRange:range]);
        UIColor *color = kind == SenkoLogLineNormal ? base : [self colorForKind:kind];
        [attributed addAttribute:colorKey value:color range:range];
        at = end;
    }
    ((void (*)(id, SEL, id))objc_msgSend)(_textView, @selector(setAttributedText:), attributed);
}

- (void)filterChanged:(UISegmentedControl *)sender {
    (void)sender;
    if (![_allLogs length]) {
        [_shown release];
        _shown = nil;
        _textView.text = SenkoLocalizedText(@"No daemon logs available");
        _count.text = @"";
        return;
    }
    NSString *shown = [self filteredLogText];
    NSInteger lines = 0;
    for (NSString *line in [shown componentsSeparatedByString:@"\n"])
        if ([line length]) ++lines;
    if (_filter.selectedSegmentIndex == 3 && ![shown length] && ![_search.text length])
        shown = SenkoLocalizedText(@"No app fault report");
    [_shown release];
    _shown = lines ? [shown copy] : nil;
    [self showText:shown];
    _count.textColor = kInkMuted;
    _count.text = lines ? [NSString stringWithFormat:SenkoLocalizedText(@"Lines: %d"), (int)lines] : @"";
    if ([_textView.text length] > 0) {
        NSRange range = NSMakeRange([_textView.text length] - 1, 1);
        [_textView scrollRangeToVisible:range];
    }
}

- (void)searchBar:(UISearchBar *)bar textDidChange:(NSString *)text {
    (void)bar; (void)text;
    [self filterChanged:_filter];
}

- (void)searchBarSearchButtonClicked:(UISearchBar *)bar {
    [bar resignFirstResponder];
}

- (void)actionsPressed:(id)sender {
    (void)sender;
    if (![_shown length]) return;
    BOOL canShare = NSClassFromString(@"UIActivityViewController") != nil;
    UIActionSheet *sheet = [[UIActionSheet alloc]
             initWithTitle:nil
                  delegate:self
         cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    destructiveButtonTitle:nil
         otherButtonTitles:SenkoLocalizedText(@"Copy"),
                           canShare ? SenkoLocalizedText(@"Share") : nil, nil];
    sheet.tag = LOGS_SHEET_ACTIONS;
    [sheet showInView:self.view];
    [sheet release];
}

- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)index {
    if (sheet.tag != LOGS_SHEET_ACTIONS || index == sheet.cancelButtonIndex || ![_shown length])
        return;
    if (index == 0) {
        [UIPasteboard generalPasteboard].string = _shown;
        return;
    }
    Class activity = NSClassFromString(@"UIActivityViewController");
    if (!activity) return;
    id controller = [[[activity alloc] performSelector:@selector(initWithActivityItems:applicationActivities:)
                                            withObject:[NSArray arrayWithObject:_shown]
                                            withObject:nil] autorelease];
/* an ipad from ios 8 refuses to present the sheet without an anchor */
    id popover = [controller respondsToSelector:@selector(popoverPresentationController)]
        ? [controller performSelector:@selector(popoverPresentationController)] : nil;
    if (popover) {
        [popover setValue:self.view forKey:@"sourceView"];
        [popover setValue:[NSValue valueWithCGRect:CGRectMake(CGRectGetMidX(self.view.bounds),
                                                              self.view.bounds.size.height - 1.0f,
                                                              1.0f, 1.0f)]
                   forKey:@"sourceRect"];
    }
    [self presentViewController:controller animated:YES completion:nil];
}

@end
