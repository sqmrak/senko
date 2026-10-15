#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include <objc/message.h>
#include <string.h>
#import "control_client.h"
#import "ui_theme.h"
#import "themes_vc.h"
#import "routing_vc.h"
#import "dev_menu_vc.h"
#import "update_install.h"
#import "app_common.h"
#import "crash_report.h"
#import "text_prompt.h"

/* a backup is the config file itself, so there is no magic to check for and no
   format version to compare: the file is ours when one of its first lines is a
   keyword the daemon writes. files from older builds open with a version line
   the parser has always ignored */
static BOOL SenkoLooksLikeBackup(NSData *data) {
    static const char *const keys[] = { "SET ", "SRV ", "SUB ", "SEL ", "ORDER" };
    const char *p = (const char *)[data bytes];
    NSUInteger len = [data length];
    if (len > 4096) len = 4096;
    NSUInteger start = 0;
    for (NSUInteger i = 0; i <= len; ++i) {
        if (i != len && p[i] != '\n') continue;
        NSUInteger n = i - start;
        for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
            size_t kl = strlen(keys[k]);
            if (n >= kl && memcmp(p + start, keys[k], kl) == 0) return YES;
        }
        start = i + 1;
    }
    return NO;
}

static const CGFloat kSenkoSettingsRowHeight = 54.0f;

static void SenkoSettingsStyleSwitch(UISwitch *sw) {
    if (!sw) return;
    if ([sw respondsToSelector:@selector(setOnTintColor:)])
        sw.onTintColor = kAccentBlue;
    if ([sw respondsToSelector:@selector(setTintColor:)])
        ((void (*)(id, SEL, id))objc_msgSend)(
            sw, @selector(setTintColor:), [kInkMuted colorWithAlphaComponent:0.65f]);
    if ([sw respondsToSelector:@selector(setThumbTintColor:)])
        ((void (*)(id, SEL, id))objc_msgSend)(
            sw, @selector(setThumbTintColor:), [UIColor whiteColor]);
}

typedef enum {
    SenkoRowAutoConnect = 0,
    SenkoRowQuickConnect,
    SenkoRowReconnect,
    SenkoRowFailover,
    SenkoRowAttempts,
    SenkoRowSubRefresh,
    SenkoRowTheme,
    SenkoRowLanguage,
    SenkoRowStats,
    SenkoRowLogs,
    SenkoRowBackup,
    SenkoRowUpdate,
    SenkoRowDNS,
    SenkoRowDNSPort,
    SenkoRowSocksPort,
    SenkoRowRules,
    SenkoRowAbout,
    SenkoRowDeveloper
} SenkoSettingsRow;

static const SenkoSettingsRow kSenkoMainRows[] = {
    SenkoRowAutoConnect, SenkoRowQuickConnect, SenkoRowReconnect,
    SenkoRowFailover, SenkoRowAttempts, SenkoRowSubRefresh
};
static const SenkoSettingsRow kSenkoAppRows[] = {
    SenkoRowTheme, SenkoRowLanguage, SenkoRowStats, SenkoRowLogs,
    SenkoRowBackup, SenkoRowUpdate
};
static const SenkoSettingsRow kSenkoMoreRows[] = {
    SenkoRowDNS, SenkoRowDNSPort, SenkoRowSocksPort, SenkoRowRules, SenkoRowAbout
};
static const SenkoSettingsRow kSenkoDevRows[] = { SenkoRowDeveloper };

typedef struct {
    const char *title;
    const char *glyph;
} SenkoSettingsRowInfo;

static const SenkoSettingsRowInfo kSenkoRowInfo[] = {
    [SenkoRowAutoConnect]  = { "Connect at startup",      "glyph-rocket.png" },
    [SenkoRowQuickConnect] = { "Quick connect",           "glyph-bolt.png" },
    [SenkoRowReconnect]    = { "Reconnect automatically", "glyph-reconnect.png" },
    [SenkoRowFailover]     = { "Try another server",      "glyph-shuffle.png" },
    [SenkoRowAttempts]     = { "Reconnect attempts",      "glyph-repeat.png" },
    [SenkoRowSubRefresh]   = { "Update subscriptions",    "glyph-cloud.png" },
    [SenkoRowTheme]        = { "Theme",                   "glyph-palette.png" },
    [SenkoRowLanguage]     = { "Language",                "glyph-globe.png" },
    [SenkoRowStats]        = { "Statistics",              "glyph-chart.png" },
    [SenkoRowLogs]         = { "System Logs",             "glyph-logs.png" },
    [SenkoRowBackup]       = { "Backup",                  "glyph-backup.png" },
    [SenkoRowUpdate]       = { "Update Senko",            "glyph-download.png" },
    [SenkoRowDNS]          = { "DNS",                     "glyph-shield.png" },
    [SenkoRowDNSPort]      = { "Local DNS port",          "glyph-hash.png" },
    [SenkoRowSocksPort]    = { "SOCKS port",              "glyph-port.png" },
    [SenkoRowRules]        = { "Routing",                 "glyph-split.png" },
    [SenkoRowAbout]        = { "About",                   "glyph-info.png" },
    [SenkoRowDeveloper]    = { "Developer settings",      "glyph-code.png" }
};

static NSString *SenkoAttemptLimitName(NSString *attempts) {
    int n = [attempts intValue];
    if (n <= 0) return SenkoLocalizedText(@"Until it works");
    return [NSString stringWithFormat:SenkoLocalizedText(@"%d attempts"), n];
}

static NSString *SenkoRefreshIntervalName(NSString *hours) {
    int h = [hours intValue];
    if (h <= 0) return SenkoLocalizedText(@"Off");
    return [NSString stringWithFormat:SenkoLocalizedText(@"Every %@"), SenkoHoursText(h)];
}

/* the three text settings share one editor, keyed by the row that opened it */
static NSString *SenkoTextSettingKey(SenkoSettingsRow row) {
    if (row == SenkoRowDNS) return @"dns_upstream";
    if (row == SenkoRowDNSPort) return @"dns_local_port";
    return @"socks_port";
}

@implementation SettingsVC {

    UITableView *_tv;
    SenkoControl *_ctl;
    BOOL _backupImportMode;
    NSString *_pendingBackupPath;
/* the daemon owns the automation settings, so the screen mirrors what it
   answered and never keeps a second copy that could disagree with it */
    NSMutableDictionary *_settings;
    NSString *_editingKey;
/* ios narrows grouped-style cells to a centered column on ipad, by an amount
   this app does not control and that has changed across releases. reading it
   off an actual displayed cell keeps section headers lined up with the row
   plates instead of guessing a margin */
    CGFloat _groupedInsetX;

}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    [_ctl release];
    [_pendingBackupPath release];
    [_settings release];
    [_editingKey release];
    [super dealloc];
}

- (void)styleTable {
    SenkoClearTableBackground(_tv);
    /* the cell background draws a two-tone groove. leaving UIKit's separator
       enabled puts a third line over it on iOS 6 */
    _tv.separatorStyle = UITableViewCellSeparatorStyleNone;
    if ([_tv respondsToSelector:@selector(setBackgroundView:)])
        _tv.backgroundView = nil;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"Settings");
    if (!_ctl) _ctl = [[SenkoControl alloc] initWithSocketPath:SENKO_SOCK];

    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setAutomaticallyAdjustsScrollViewInsets:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setAutomaticallyAdjustsScrollViewInsets:), NO);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);

    self.navigationItem.leftBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                       target:self
                                                       action:@selector(donePressed)] autorelease];

    SenkoApplyScreenChrome(self.view);
    _tv = [[UITableView alloc] initWithFrame:SenkoViewBounds(self.view)
                                       style:UITableViewStyleGrouped];
    SenkoScrollViewUseManualInsets(_tv);
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.alwaysBounceVertical = YES;
    _tv.rowHeight = kSenkoSettingsRowHeight;
    _tv.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                           UIViewAutoresizingFlexibleHeight;
    [self styleTable];
    [self.view addSubview:_tv];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(languageDidChange:)
                                                 name:SenkoLanguageDidChangeNotification
                                               object:nil];
}

/* ios 5 releases the view of an offscreen controller, so the retained table
   must go with it instead of pointing into a freed hierarchy */
- (void)viewDidUnload {
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:SenkoThemeDidChangeNotification
                                                  object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:SenkoLanguageDidChangeNotification
                                                  object:nil];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    _tv = nil;
    [super viewDidUnload];
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    SenkoApplyScreenChrome(self.view);
    [self styleTable];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

- (void)languageDidChange:(NSNotification *)n {
    (void)n;
    self.title = SenkoLocalizedText(@"Settings");
    [_tv reloadData];
}

/* the developer section is unlocked from About, so the section count can change
   while this screen is off. uikit throws on the first partial update whose
   section count disagrees with the one the table was loaded with, so the whole
   table has to be reloaded before any row is touched */
- (BOOL)reloadWhenSectionCountChanged {
    if ([_tv numberOfSections] == [self numberOfSectionsInTableView:_tv])
        return NO;
    [_tv reloadData];
    return YES;
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("settings");
    [super viewWillAppear:animated];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    SenkoApplyScreenChrome(self.view);
    [self layoutSettings];
    [self reloadWhenSectionCountChanged];
    [_tv reloadData];
    [self reloadDaemonSettings];
}

- (void)layoutSettings {
    CGRect b = SenkoViewBounds(self.view);
    UIView *bg = [self.view viewWithTag:9111];
    if (bg) bg.frame = b;
    _tv.frame = CGRectMake(0, 0, b.size.width, b.size.height);
    /* grouped tables on ios 13 can lose their last rows when the controller
       owns the inset and the navigation controller also adjusts it. keeping a
       small explicit gap makes the first section readable and leaves the
       developer row reachable at the bottom on every supported runtime */
    UIEdgeInsets safe = SenkoSafeAreaInsets(self.view);
    _tv.contentInset = UIEdgeInsetsMake(8.0f, 0.0f, safe.bottom + 16.0f, 0.0f);
    _tv.scrollIndicatorInsets = _tv.contentInset;
    SenkoClearTableBackground(_tv);
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutSettings];
}

- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)io
                                         duration:(NSTimeInterval)dur {
    (void)io; (void)dur;
    [self layoutSettings];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io {
    (void)io;
    [self layoutSettings];
}

- (void)donePressed {
    SenkoDismissModal(self, nil);
}

/* the developer section is the last one, so unlocking it never renumbers a row
   the user was about to tap */
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return SenkoDevMenuEnabled() ? 4 : 3;
}

- (const SenkoSettingsRow *)rowsInSection:(NSInteger)s count:(NSInteger *)count {
    const SenkoSettingsRow *rows = kSenkoDevRows;
    NSInteger n = 1;
    if (s == 0) { rows = kSenkoMainRows; n = sizeof kSenkoMainRows / sizeof kSenkoMainRows[0]; }
    else if (s == 1) { rows = kSenkoAppRows; n = sizeof kSenkoAppRows / sizeof kSenkoAppRows[0]; }
    else if (s == 2) { rows = kSenkoMoreRows; n = sizeof kSenkoMoreRows / sizeof kSenkoMoreRows[0]; }
    if (count) *count = n;
    return rows;
}

- (SenkoSettingsRow)rowAtIndexPath:(NSIndexPath *)ip {
    NSInteger n = 0;
    const SenkoSettingsRow *rows = [self rowsInSection:ip.section count:&n];
    if (ip.row < 0 || ip.row >= n) return SenkoRowAbout;
    return rows[ip.row];
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    NSInteger n = 0;
    [self rowsInSection:s count:&n];
    return n;
}

- (NSString *)headerTextForSection:(NSInteger)s {
    if (s == 0) return SenkoLocalizedText(@"Main");
    if (s == 1) return SenkoLocalizedText(@"App");
    if (s == 2) return SenkoLocalizedText(@"Advanced");
    return SenkoLocalizedText(@"Developer settings");
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 38.0f;
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return kSenkoSettingsRowHeight;
}

/* the daemon rows go inert when it does not answer, and the screen says why
   once instead of every row explaining itself */
- (NSString *)footerTextForSection:(NSInteger)s {
    if (s == 0 && !_settings)
        return SenkoLocalizedText(@"The daemon is not answering, so these cannot be read or changed.");
    return nil;
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    NSString *text = [self footerTextForSection:s];
    if (![text length]) return 10.0f;
    CGFloat width = tv.bounds.size.width - 40.0f;
    if (width < 120.0f) width = 120.0f;
    CGSize size = SenkoTextSize(text, [UIFont systemFontOfSize:12.0f], width);
    return MAX(36.0f, size.height + 16.0f);
}

/* a plain label in an owned view is the only way these keep the theme ink:
   from ios 14 UITableViewHeaderFooterView re-applies its own content
   configuration after willDisplayHeaderView:, which put the section titles
   back to the system colour (black on the dark palettes) */
- (UIView *)sectionTextViewWithText:(NSString *)text
                               font:(UIFont *)font
                             height:(CGFloat)height
                              width:(CGFloat)width {
    if (![text length]) return nil;
    UIView *wrap = [[[UIView alloc] initWithFrame:
                     CGRectMake(0, 0, width, height)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    wrap.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    CGFloat inset = 16.0f + _groupedInsetX;
    UILabel *label = [[[UILabel alloc] initWithFrame:
                       CGRectMake(inset, 4.0f, width - inset * 2.0f, height - 6.0f)] autorelease];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                             UIViewAutoresizingFlexibleHeight;
    label.backgroundColor = [UIColor clearColor];
    label.font = font;
    label.numberOfLines = 0;
    label.lineBreakMode = NSLineBreakByWordWrapping;
    label.text = text;
    label.textAlignment = NSTextAlignmentLeft;
    SenkoStyleMutedLabel(label);
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
    [wrap addSubview:label];
    return wrap;
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    UIView *header = [self sectionTextViewWithText:[self headerTextForSection:s]
                                              font:SenkoFontBody(15.0f, YES)
                                            height:38.0f
                                             width:tv.bounds.size.width];
    for (UIView *child in header.subviews)
        if ([child isKindOfClass:[UILabel class]])
            ((UILabel *)child).textAlignment = NSTextAlignmentCenter;
/* the same five taps that opened the section close it, so a tester who turned
   it on by accident is not stuck with it */
    if (header && s == 3) {
        header.userInteractionEnabled = YES;
        [header addGestureRecognizer:
            [[[UITapGestureRecognizer alloc] initWithTarget:self
                                                     action:@selector(devHeaderTapped)] autorelease]];
    }
    return header;
}

- (void)devHeaderTapped {
    if (SenkoDevMenuTapsLeft() > 0) return;
    SenkoSetDevMenuEnabled(NO);
    [_tv reloadData];
}

- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)s {
    UIView *footer = [self sectionTextViewWithText:[self footerTextForSection:s]
                                              font:[UIFont systemFontOfSize:12.0f]
                                            height:[self tableView:tv heightForFooterInSection:s]
                                             width:tv.bounds.size.width];
    return footer ? footer : [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
}

- (void)tableView:(UITableView *)tv willDisplayCell:(UITableViewCell *)cell
 forRowAtIndexPath:(NSIndexPath *)ip {
    SenkoStyleGroupCell(cell, ip, [self tableView:tv numberOfRowsInSection:ip.section]);
/* uikit has already narrowed and centered cell.frame by the time this runs,
   so this is the real margin rather than a guess at one */
    CGFloat insetX = cell.frame.origin.x;
    if (insetX >= 0.0f && insetX < tv.bounds.size.width * 0.5f &&
        fabsf((float)(insetX - _groupedInsetX)) > 0.5f) {
        _groupedInsetX = insetX;
/* section 0's header is built before any row exists to measure against, so
   the first display leaves it at the wrong offset; once a row answers, redraw
   the headers that were built too early instead of leaving them stuck there */
        dispatch_async(dispatch_get_main_queue(), ^{
            [tv reloadData];
        });
    }
}

- (UISwitch *)switchOn:(BOOL)on enabled:(BOOL)enabled action:(SEL)action {
    UISwitch *toggle = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
    toggle.on = on;
    toggle.enabled = enabled;
    SenkoSettingsStyleSwitch(toggle);
    [toggle addTarget:self action:action forControlEvents:UIControlEventValueChanged];
    return toggle;
}

/* the value a daemon row shows, or the word for a daemon that did not answer */
- (NSString *)daemonValue:(NSString *)text {
    return _settings ? text : SenkoLocalizedText(@"unreachable");
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"set";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                       reuseIdentifier:cid] autorelease];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.clipsToBounds = YES;
    cell.selectedBackgroundView = nil;
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.accessoryView = nil;
    cell.detailTextLabel.text = nil;
    cell.textLabel.textColor = kInk;
    cell.textLabel.shadowColor = nil;
    cell.textLabel.shadowOffset = CGSizeZero;
    cell.detailTextLabel.textColor = kInkMuted;
    cell.detailTextLabel.shadowColor = nil;
    cell.detailTextLabel.shadowOffset = CGSizeZero;
    cell.textLabel.font = SenkoFontBody(16.0f, NO);
    cell.detailTextLabel.font = SenkoFontBody(15.0f, NO);
    cell.textLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    cell.textLabel.adjustsFontSizeToFitWidth = YES;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    cell.textLabel.minimumFontSize = 12.0f;
#pragma clang diagnostic pop
    SenkoStyleGroupCell(cell, ip, [self tableView:tv numberOfRowsInSection:ip.section]);
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];

    SenkoSettingsRow row = [self rowAtIndexPath:ip];
    SenkoSettingsRowInfo info = kSenkoRowInfo[row];
    cell.textLabel.text = SenkoLocalizedText([NSString stringWithUTF8String:info.title]);
    cell.imageView.image = TintedIconNamed([NSString stringWithUTF8String:info.glyph],
                                           22.0f, kInk);
    BOOL daemon = _settings != nil;

    switch (row) {
        case SenkoRowAutoConnect:
            cell.accessoryView = [self switchOn:[self daemonFlag:@"auto_connect"]
                                        enabled:daemon action:@selector(autoConnectChanged:)];
            return cell;
        case SenkoRowQuickConnect:
            cell.accessoryView = [self switchOn:SenkoAutoServerEnabled()
                                        enabled:YES action:@selector(quickConnectChanged:)];
            return cell;
        case SenkoRowReconnect:
            cell.accessoryView = [self switchOn:[self daemonFlag:@"auto_reconnect"]
                                        enabled:daemon action:@selector(autoReconnectChanged:)];
            return cell;
        case SenkoRowFailover:
            cell.accessoryView = [self switchOn:[self daemonFlag:@"failover"]
                                        enabled:daemon action:@selector(failoverChanged:)];
            return cell;
        case SenkoRowAttempts:
            cell.detailTextLabel.text = [self daemonValue:
                SenkoAttemptLimitName([_settings objectForKey:@"reconnect_max_attempts"])];
            break;
        case SenkoRowSubRefresh:
            cell.detailTextLabel.text = [self daemonValue:
                SenkoRefreshIntervalName([_settings objectForKey:@"sub_refresh_hours"])];
            break;
        case SenkoRowTheme:
            cell.detailTextLabel.text = SenkoThemeDisplayName(SenkoThemeCurrentId());
            break;
        case SenkoRowLanguage:
            cell.detailTextLabel.text = SenkoLanguageName();
            break;
        case SenkoRowDNS:
        case SenkoRowDNSPort:
        case SenkoRowSocksPort:
            cell.detailTextLabel.text = [self daemonValue:
                [_settings objectForKey:SenkoTextSettingKey(row)]];
            break;
        default:
            break;
    }
    BOOL inert = !daemon && (row == SenkoRowAttempts || row == SenkoRowSubRefresh ||
                             row == SenkoRowDNS || row == SenkoRowDNSPort ||
                             row == SenkoRowSocksPort);
    if (!inert) {
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        SenkoStyleSelectableCell(cell);
    }
    return cell;
}

- (void)reloadDaemonSettings {
    [_ctl daemonSettings:^(NSDictionary *values) {
        [_settings release];
        _settings = values ? [values mutableCopy] : nil;
        [_tv reloadData];
    }];
}

- (BOOL)daemonFlag:(NSString *)key {
    return [[_settings objectForKey:key] isEqualToString:@"1"];
}

/* a switch that the daemon refused must not keep showing the new position, and
   the daemon is the only place that knows what it took */
- (void)applySetting:(NSString *)key value:(NSString *)value {
    if (!_settings) return;
    [_ctl setSetting:key value:value reply:^(NSString *reply) {
        if ([reply hasPrefix:@"OK "])
            [_settings setObject:value forKey:key];
        else
            [self showBackupMessage:reply ? SenkoHumanReadableError(reply)
                                          : SenkoLocalizedText(@"Daemon is unreachable")];
        [_tv reloadData];
    }];
}

/* reloading on the reply replaced the switch mid-slide, so it jumped late.
   keep it and slide back only if the daemon refused */
- (void)applySwitch:(UISwitch *)sw key:(NSString *)key {
    NSString *value = sw.on ? @"1" : @"0";
    if (!_settings) {
        [sw setOn:!sw.on animated:YES];
        return;
    }
    [_ctl setSetting:key value:value reply:^(NSString *reply) {
        if ([reply hasPrefix:@"OK "]) {
            [_settings setObject:value forKey:key];
            return;
        }
        [sw setOn:[self daemonFlag:key] animated:YES];
        [self showBackupMessage:reply ? SenkoHumanReadableError(reply)
                                      : SenkoLocalizedText(@"Daemon is unreachable")];
    }];
}

- (void)autoConnectChanged:(UISwitch *)sw {
    [self applySwitch:sw key:@"auto_connect"];
}

/* quick connect is the app's own choice of what the button dials, so it lives
   in the app defaults the server list reads, not in the daemon */
- (void)quickConnectChanged:(UISwitch *)sw {
    SenkoSetAutoServerEnabled(sw.on);
}

- (void)autoReconnectChanged:(UISwitch *)sw {
    [self applySwitch:sw key:@"auto_reconnect"];
}

- (void)failoverChanged:(UISwitch *)sw {
    [self applySwitch:sw key:@"failover"];
}

- (void)showAttemptMenu {
    UIAlertView *pick = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Reconnect attempts")
              message:SenkoAttemptLimitName([_settings objectForKey:@"reconnect_max_attempts"])
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:SenkoLocalizedText(@"Until it works"), @"3", @"5", @"10", nil] autorelease];
    pick.tag = 4205;
    [pick show];
}

/* a plain text alert is the only input control that behaves the same from ios 5
   to ios 15, and the daemon validates what comes out of it. ios 4.3 has no text
   alert and gets a small screen */
- (void)showEditorForRow:(SenkoSettingsRow)row {
    NSString *key = SenkoTextSettingKey(row);
    NSString *title =
        SenkoLocalizedText([NSString stringWithUTF8String:kSenkoRowInfo[row].title]);
    NSString *message = [key isEqualToString:@"dns_upstream"]
        ? SenkoLocalizedText(@"An IPv4 address, for example 1.1.1.1")
        : SenkoLocalizedText(@"A port number between 1 and 65535");
    [_editingKey release];
    _editingKey = [key copy];
    if (![UIAlertView instancesRespondToSelector:@selector(setAlertViewStyle:)]) {
        [SenkoTextPromptVC presentFrom:self
                                 title:title
                               message:message
                                  text:[_settings objectForKey:key]
                              keyboard:UIKeyboardTypeNumbersAndPunctuation
                               confirm:SenkoLocalizedText(@"Save")
                               handler:^(NSString *text) { [self applyEditedText:text]; }];
        return;
    }
    UIAlertView *editor = [[[UIAlertView alloc]
        initWithTitle:title
              message:message
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:SenkoLocalizedText(@"Save"), nil] autorelease];
    editor.tag = 4206;
    editor.alertViewStyle = UIAlertViewStylePlainTextInput;
    UITextField *field = [editor textFieldAtIndex:0];
    field.autocapitalizationType = UITextAutocapitalizationTypeNone;
    field.autocorrectionType = UITextAutocorrectionTypeNo;
    field.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
    field.text = [_settings objectForKey:key];
    [editor show];
}

- (void)applyEditedText:(NSString *)text {
    if (![_editingKey length]) return;
    NSString *value = [text stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([value length]) [self applySetting:_editingKey value:value];
}

/* the alert picker is the one control that exists unchanged from ios 5 to 15 */
- (void)showRefreshMenu {
    UIAlertView *pick = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Update subscriptions")
              message:SenkoRefreshIntervalName([_settings objectForKey:@"sub_refresh_hours"])
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:SenkoLocalizedText(@"Off"), SenkoHoursText(6), SenkoHoursText(12),
                      SenkoHoursText(24), nil] autorelease];
    pick.tag = 4204;
    [pick show];
}

- (void)showBackupMenu {
    UIAlertView *pick = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Backup")
              message:nil
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:SenkoLocalizedText(@"Export backup"),
                      SenkoLocalizedText(@"Restore backup"), nil] autorelease];
    pick.tag = 4207;
    [pick show];
}

- (void)showLanguageMenu {
    UIAlertView *language = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Language")
              message:SenkoLanguageName()
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:@"English", @"Русский", @"中文", nil] autorelease];
    language.tag = 4202;
    [language show];
}

- (void)exportBackup {
    [_ctl exportBackup:^(NSString *reply) {
        NSString *msg = [reply hasPrefix:@"OK "] ?
            SenkoLocalizedText(@"Saved to Documents/senko-backup.senko") : reply;
        [self showBackupMessage:msg ?: SenkoLocalizedText(@"Backup export failed")];
    }];
}

- (void)openUpdateBrowser {
    _backupImportMode = NO;
    FileImportVC *files = [[[FileImportVC alloc] initWithPath:nil delegate:self] autorelease];
    files.title = @"Update Senko";
    [self.navigationController pushViewController:files animated:YES];
}

- (void)openBackupBrowser {
    _backupImportMode = YES;
    FileImportVC *files = [[[FileImportVC alloc] initWithPath:nil delegate:self] autorelease];
    files.title = SenkoLocalizedText(@"Restore configuration");
    [self.navigationController pushViewController:files animated:YES];
}

- (void)showBackupMessage:(NSString *)message {
    UIAlertView *av = [[[UIAlertView alloc] initWithTitle:SenkoLocalizedText(@"Configuration backup")
                                                   message:SenkoHumanReadableError(message)
                                                  delegate:nil
                                         cancelButtonTitle:@"OK"
                                         otherButtonTitles:nil] autorelease];
    [av show];
}

- (void)fileImportVCDidCancel:(FileImportVC *)vc {
    (void)vc;
    [self.navigationController popToViewController:self animated:YES];
}

- (void)presentUpdateInstallForPath:(NSString *)path {
    if (![path length]) return;
    NSString *pkg = [[path copy] autorelease];
    UINavigationController *nav = self.navigationController;
    void (^showInstall)(void) = ^{
        UIViewController *host = nav ? (UIViewController *)nav : (UIViewController *)self;
        if (SenkoPresentedController(host)) {
            host = self;
        }
        UpdateInstallVC *uvc = [[[UpdateInstallVC alloc] initWithControl:_ctl
                                                             packagePath:pkg] autorelease];
        uvc.modalTransitionStyle = UIModalTransitionStyleCoverVertical;
        uvc.modalPresentationStyle = UIModalPresentationFullScreen;
        SenkoPresentModal(host, uvc);
    };
    if (nav && nav.topViewController != self) {
/* wait for the pop animation */
        [CATransaction begin];
        [CATransaction setCompletionBlock:showInstall];
        [nav popToViewController:self animated:YES];
        [CATransaction commit];
    } else {
        showInstall();
    }
}

- (void)fileImportVC:(FileImportVC *)vc didPickPath:(NSString *)path {
    (void)vc;
    if (_backupImportMode) {
        NSData *data = [NSData dataWithContentsOfFile:path];
        if (![data length] || [data length] > 1024 * 1024 ||
            !SenkoLooksLikeBackup(data)) {
            [self showBackupMessage:SenkoLocalizedText(@"Not a senko backup")];
            return;
        }
        [_pendingBackupPath release];
        _pendingBackupPath = [path copy];
        UIAlertView *confirm = [[[UIAlertView alloc]
            initWithTitle:SenkoLocalizedText(@"Replace configuration?")
                  message:SenkoLocalizedText(@"The imported backup will replace all current servers and subscriptions.")
                 delegate:self cancelButtonTitle:SenkoLocalizedText(@"Cancel")
        otherButtonTitles:SenkoLocalizedText(@"Replace"), nil] autorelease];
        confirm.tag = 4201;
        [confirm show];
        return;
    }
    if ([[path pathExtension] caseInsensitiveCompare:@"deb"] != NSOrderedSame) {
        UIAlertView *av = [[[UIAlertView alloc] initWithTitle:@"Update Senko"
                                                       message:@"choose a .deb package"
                                                      delegate:nil
                                             cancelButtonTitle:@"OK"
                                             otherButtonTitles:nil] autorelease];
        [av show];
        return;
    }
    [self presentUpdateInstallForPath:path];
}

- (void)alertView:(UIAlertView *)alert clickedButtonAtIndex:(NSInteger)buttonIndex {
    if (alert.tag == 4202) {
        if (buttonIndex == 1) SenkoSetLanguage(SenkoLanguageEnglish);
        else if (buttonIndex == 2) SenkoSetLanguage(SenkoLanguageRussian);
        else if (buttonIndex == 3) SenkoSetLanguage(SenkoLanguageChinese);
        return;
    }
    if (alert.tag == 4207) {
        if (buttonIndex == alert.cancelButtonIndex) return;
        if (buttonIndex == alert.firstOtherButtonIndex) [self exportBackup];
        else [self openBackupBrowser];
        return;
    }
    if (alert.tag == 4205) {
        if (buttonIndex == alert.cancelButtonIndex) return;
        static const char *const limits[] = { "0", "3", "5", "10" };
        NSInteger pick = buttonIndex - 1;
        if (pick < 0 || pick >= (NSInteger)(sizeof limits / sizeof limits[0])) return;
        [self applySetting:@"reconnect_max_attempts"
                     value:[NSString stringWithUTF8String:limits[pick]]];
        return;
    }
    if (alert.tag == 4206) {
        if (buttonIndex == alert.cancelButtonIndex) return;
        [self applyEditedText:[[alert textFieldAtIndex:0] text]];
        return;
    }
    if (alert.tag == 4204) {
        if (buttonIndex == alert.cancelButtonIndex) return;
        static const char *const hours[] = { "0", "6", "12", "24" };
        NSInteger pick = buttonIndex - 1;
        if (pick < 0 || pick >= (NSInteger)(sizeof hours / sizeof hours[0])) return;
        [self applySetting:@"sub_refresh_hours"
                     value:[NSString stringWithUTF8String:hours[pick]]];
        return;
    }
    if (alert.tag != 4201 || buttonIndex == alert.cancelButtonIndex) return;
    NSData *data = [NSData dataWithContentsOfFile:_pendingBackupPath];
    NSString *dir = @"/var/mobile/Library/Preferences/Senko";
    [[NSFileManager defaultManager] createDirectoryAtPath:dir
                              withIntermediateDirectories:YES attributes:nil error:NULL];
    NSString *stage = [dir stringByAppendingPathComponent:@"import.senko"];
    if (![data writeToFile:stage options:NSDataWritingAtomic error:NULL]) {
        [self showBackupMessage:SenkoLocalizedText(@"Could not stage backup")];
        return;
    }
    [_ctl restoreBackup:^(NSString *reply) {
        if ([reply hasPrefix:@"OK "])
            [self showBackupMessage:SenkoLocalizedText(@"Configuration restored")];
        else
            [self showBackupMessage:reply ?: SenkoLocalizedText(@"Backup restore failed")];
    }];
}

- (void)pushController:(UIViewController *)vc {
    [self.navigationController pushViewController:vc animated:YES];
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    switch ([self rowAtIndexPath:ip]) {
        case SenkoRowAttempts:
            if (_settings) [self showAttemptMenu];
            break;
        case SenkoRowSubRefresh:
            if (_settings) [self showRefreshMenu];
            break;
        case SenkoRowDNS:
        case SenkoRowDNSPort:
        case SenkoRowSocksPort:
            if (_settings) [self showEditorForRow:[self rowAtIndexPath:ip]];
            break;
        case SenkoRowTheme:
            [self pushController:[[[ThemesVC alloc] init] autorelease]];
            break;
        case SenkoRowLanguage:
            [self showLanguageMenu];
            break;
        case SenkoRowStats:
            [self pushController:[[[StatsVC alloc] init] autorelease]];
            break;
        case SenkoRowLogs:
            [self pushController:[[[LogsVC alloc] init] autorelease]];
            break;
        case SenkoRowBackup:
            [self showBackupMenu];
            break;
        case SenkoRowUpdate:
            [self openUpdateBrowser];
            break;
        case SenkoRowRules:
            [self pushController:[[[RoutingVC alloc] initWithControl:_ctl] autorelease]];
            break;
        case SenkoRowAbout:
            [self pushController:[[[AboutVC alloc] init] autorelease]];
            break;
        case SenkoRowDeveloper:
            [self pushController:[[[DevMenuVC alloc] initWithControl:_ctl] autorelease]];
            break;
        default:
            break;
    }
}

- (void)editServerVC:(EditServerVC *)vc saveLink:(NSString *)link index:(int)idx {
    [_ctl replaceServerIndex:idx link:link reply:^(NSString *reply) {
        if (!reply || [reply hasPrefix:@"ERR"]) {
            UIAlertView *alert = [[[UIAlertView alloc] initWithTitle:@"Could not update profile"
                                                               message:reply ?: @"daemon offline"
                                                              delegate:nil
                                                     cancelButtonTitle:@"OK"
                                                     otherButtonTitles:nil] autorelease];
            [alert show];
            return;
        }
        SenkoDismissModal(vc, nil);
    }];
}

@end
