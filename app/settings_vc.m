#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CFNetwork/CFNetwork.h>
#import <ifaddrs.h>
#import <arpa/inet.h>
#import <dlfcn.h>
#import <unistd.h>
#include <math.h>
#include <objc/message.h>
#include <string.h>
#import "control_client.h"
#import "qr_scan.h"
#import "ui_theme.h"
#import "boykisser_field.h"
#import "bubble_field.h"
#import "themes_vc.h"
#import "rules_vc.h"
#import "dev_menu_vc.h"
#import "server_cell.h"
#import "home_layout.h"
#import "update_install.h"
#import "meow.h"
#import "app_common.h"
#import "crash_report.h"

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

static void SenkoSettingsStyleTable(UITableView *tv) {
    if (!tv) return;
    tv.backgroundColor = kBG;
    /* the cell background draws a two-tone groove. leaving UIKit's separator
       enabled puts a third line over it on iOS 6 */
    tv.separatorStyle = UITableViewCellSeparatorStyleNone;
    if ([tv respondsToSelector:@selector(setBackgroundView:)])
        tv.backgroundView = nil;
}

static const CGFloat kSenkoSettingsRowHeight = 68.0f;

static void SenkoSettingsStyleSwitch(UISwitch *sw) {
    if (!sw) return;
    if ([sw respondsToSelector:@selector(setOnTintColor:)])
        sw.onTintColor = kAccentBlue;
    if ([sw respondsToSelector:@selector(setTintColor:)])
        ((void (*)(id, SEL, id))objc_msgSend)(
            sw, @selector(setTintColor:), [kInkMuted colorWithAlphaComponent:0.35f]);
}

static void SenkoSettingsApplyCellBackground(UITableView *tv,
                                              UITableViewCell *cell,
                                              NSIndexPath *ip,
                                              NSInteger rowsInSection) {
    (void)tv;
    SenkoStyleGroupCell(cell, ip, rowsInSection);
}


static NSString *SenkoSortModeName(void) {
    switch ((SenkoServerSort)[[NSUserDefaults standardUserDefaults]
                              integerForKey:SENKO_SERVER_SORT_KEY]) {
        case SenkoSortName: return SenkoLocalizedText(@"By name");
        case SenkoSortPing: return SenkoLocalizedText(@"By latency");
        case SenkoSortManual:
        default: return SenkoLocalizedText(@"Stored order");
    }
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
    SenkoSettingsStyleTable(_tv);
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
    SenkoSettingsStyleTable(_tv);
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

- (void)languageDidChange:(NSNotification *)n {
    (void)n;
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
    _tv.backgroundColor = kBG;
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
    [self dismissViewControllerAnimated:YES completion:nil];
}

/* the developer section is the last one, so unlocking it never renumbers a row
   the user was about to tap */
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return SenkoDevMenuEnabled() ? 4 : 3;
}

/* stock/non-jailbreak builds have no senkod, so "System Logs" (senkod + awg
   combined) has nothing behind it to read. the row is skipped on the app
   side instead of pushing a screen that opens onto an unreachable daemon */
static NSInteger SenkoAppSettingsRow(NSInteger displayRow) {
#if SENKO_STOCK_NATIVE
    return displayRow >= 2 ? displayRow + 1 : displayRow;
#else
    return displayRow;
#endif
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    if (s == 0) return 5;
    if (s == 1) return 4;
    if (s == 2) {
#if SENKO_STOCK_NATIVE
        return 8;
#else
        return 9;
#endif
    }
    return 1;
}

- (NSString *)headerTextForSection:(NSInteger)s {
    if (s == 0) return SenkoLocalizedText(@"AUTOMATION");
    if (s == 1) return SenkoLocalizedText(@"ROUTING");
    if (s == 2) return SenkoLocalizedText(@"APP");
    if (s == 3) return SenkoLocalizedText(@"DEVELOPER");
    return nil;
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 34.0f;
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return kSenkoSettingsRowHeight;
}

- (NSString *)footerTextForSection:(NSInteger)s {
    if (s == 0 || s == 1) {
        if (!_settings)
            return SenkoLocalizedText(@"The daemon is not answering, so these cannot be read or changed.");
        if (s == 0)
            return SenkoLocalizedText(@"The daemon runs these on its own, with the app closed.");
        return SenkoLocalizedText(@"DNS settings apply the next time the tunnel comes up. The SOCKS port applies when the daemon restarts.");
    }
    if (s == 3)
        return SenkoLocalizedText(@"Five taps on this heading hide the section again.");
    return nil;
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    NSString *text = [self footerTextForSection:s];
    CGFloat width = tv.bounds.size.width - 40.0f;
/* a section with nothing to explain keeps only the gap before the next one */
    if (!text.length) return 18.0f;
    if (width < 120.0f) width = 120.0f;
    CGSize size = SenkoTextSize(text, [UIFont systemFontOfSize:12.0f], width);
    return MAX(50.0f, size.height + 24.0f);
}

/* a plain label in an owned view is the only way these keep the theme ink:
   from ios 14 UITableViewHeaderFooterView re-applies its own content
   configuration after willDisplayHeaderView:, which put the section titles
   back to the system colour (black on the dark palettes) */
- (UIView *)sectionTextViewWithText:(NSString *)text
                               font:(UIFont *)font
                             height:(CGFloat)height
                              width:(CGFloat)width
                           centered:(BOOL)centered {
    if (![text length]) return nil;
    UIView *wrap = [[[UIView alloc] initWithFrame:
                     CGRectMake(0, 0, width, height)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    wrap.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    CGFloat inset = 20.0f + _groupedInsetX;
    UILabel *label = [[[UILabel alloc] initWithFrame:
                       CGRectMake(inset, 4.0f, width - inset * 2.0f, height - 8.0f)] autorelease];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                             UIViewAutoresizingFlexibleHeight;
    label.backgroundColor = [UIColor clearColor];
    label.font = font;
    label.numberOfLines = 0;
    label.lineBreakMode = NSLineBreakByWordWrapping;
    label.text = text;
    if (centered) {
        label.textAlignment = NSTextAlignmentCenter;
        SenkoStyleAccentLabel(label);
    } else {
        label.textAlignment = NSTextAlignmentLeft;
        SenkoStyleMutedLabel(label);
        label.alpha = 0.72f;
    }
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
    [wrap addSubview:label];
    return wrap;
}

/* headers name a group, while footers are body copy and follow the row inset */
- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    UIView *header = [self sectionTextViewWithText:[self headerTextForSection:s]
                                              font:SenkoFontBody(12.0f, YES)
                                            height:34.0f
                                             width:tv.bounds.size.width
                                          centered:NO];
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
    return [self sectionTextViewWithText:[self footerTextForSection:s]
                                    font:[UIFont systemFontOfSize:12.0f]
                                  height:[self tableView:tv heightForFooterInSection:s]
                                   width:tv.bounds.size.width
                                centered:NO];
}

- (void)tableView:(UITableView *)tv willDisplayCell:(UITableViewCell *)cell
 forRowAtIndexPath:(NSIndexPath *)ip {
    SenkoSettingsApplyCellBackground(tv, cell, ip,
                                     [self tableView:tv numberOfRowsInSection:ip.section]);
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

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"set";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
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
    cell.textLabel.font = SenkoFontBody(16.0f, YES);
    cell.detailTextLabel.font = SenkoFontBody(13.0f, NO);
    cell.textLabel.lineBreakMode = NSLineBreakByClipping;
    cell.textLabel.adjustsFontSizeToFitWidth = YES;
    cell.textLabel.minimumFontSize = 12.0f;
    cell.detailTextLabel.numberOfLines = 2;
    cell.detailTextLabel.lineBreakMode = NSLineBreakByWordWrapping;
    SenkoSettingsApplyCellBackground(tv, cell, ip,
                                     [self tableView:tv numberOfRowsInSection:ip.section]);
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];

    if (ip.section == 0) {
/* every row here reflects a daemon setting, so a daemon that did not answer
   leaves them visible but inert rather than showing a state nothing holds */
        UISwitch *toggle = nil;
        if (ip.row == 0) {
            cell.textLabel.text = SenkoLocalizedText(@"Connect at startup");
            cell.detailTextLabel.text = SenkoLocalizedText(@"Dial the selected server after a reboot");
            toggle = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            toggle.on = [self daemonFlag:@"auto_connect"];
            [toggle addTarget:self action:@selector(autoConnectChanged:)
             forControlEvents:UIControlEventValueChanged];
        } else if (ip.row == 1) {
            cell.textLabel.text = SenkoLocalizedText(@"Reconnect automatically");
            cell.detailTextLabel.text = SenkoLocalizedText(@"After a drop or a change of network");
            toggle = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            toggle.on = [self daemonFlag:@"auto_reconnect"];
            [toggle addTarget:self action:@selector(autoReconnectChanged:)
             forControlEvents:UIControlEventValueChanged];
        } else if (ip.row == 2) {
            cell.textLabel.text = SenkoLocalizedText(@"Try another server");
            cell.detailTextLabel.text = SenkoLocalizedText(@"Only inside the same section, fastest first");
            toggle = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            toggle.on = [self daemonFlag:@"failover"];
            [toggle addTarget:self action:@selector(failoverChanged:)
             forControlEvents:UIControlEventValueChanged];
        } else if (ip.row == 3) {
            cell.textLabel.text = SenkoLocalizedText(@"Update subscriptions");
            cell.detailTextLabel.text = _settings
                ? SenkoRefreshIntervalName([_settings objectForKey:@"sub_refresh_hours"])
                : SenkoLocalizedText(@"unreachable");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            if (_settings) SenkoStyleSelectableCell(cell);
        } else {
            cell.textLabel.text = SenkoLocalizedText(@"Reconnect attempts");
            cell.detailTextLabel.text = _settings
                ? SenkoAttemptLimitName([_settings objectForKey:@"reconnect_max_attempts"])
                : SenkoLocalizedText(@"unreachable");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            if (_settings) SenkoStyleSelectableCell(cell);
        }
        if (toggle) {
            SenkoSettingsStyleSwitch(toggle);
            toggle.enabled = _settings != nil;
            cell.accessoryView = toggle;
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
        }
    } else if (ip.section == 1) {
        if (ip.row == 0) {
            cell.textLabel.text = SenkoLocalizedText(@"Routing rules");
            cell.detailTextLabel.text = SenkoLocalizedText(@"Send a domain or a subnet direct, or block it");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else {
            cell.textLabel.text = SenkoSettingRowTitle(ip.row);
            cell.detailTextLabel.text = _settings
                ? [_settings objectForKey:SenkoSettingRowKey(ip.row)]
                : SenkoLocalizedText(@"unreachable");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            if (_settings) SenkoStyleSelectableCell(cell);
        }
    } else if (ip.section == 3) {
        cell.textLabel.text = SenkoLocalizedText(@"Developer");
        cell.detailTextLabel.text = SenkoLocalizedText(@"What was chosen, checks, overrides and rescue");
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        SenkoStyleSelectableCell(cell);
    } else {
        NSInteger row = SenkoAppSettingsRow(ip.row);
        if (row == 0) {
            cell.textLabel.text = SenkoLocalizedText(@"Sort servers");
            cell.detailTextLabel.text = SenkoSortModeName();
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (row == 1) {
            cell.textLabel.text = SenkoLocalizedText(@"Themes");
            cell.detailTextLabel.text = SenkoThemeStatusLine();
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (row == 2) {
            cell.textLabel.text = SenkoLocalizedText(@"System Logs");
            cell.detailTextLabel.text = SenkoLocalizedText(@"senkod + awg combined");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (row == 3) {
            cell.textLabel.text = SenkoLocalizedText(@"Export backup");
            cell.detailTextLabel.text = SenkoLocalizedText(@"Save a config file to Documents");
            SenkoStyleSelectableCell(cell);
        } else if (row == 4) {
            cell.textLabel.text = SenkoLocalizedText(@"Restore backup");
            cell.detailTextLabel.text = SenkoLocalizedText(@"Validate, then replace configuration");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (row == 5) {
            cell.textLabel.text = SenkoLocalizedText(@"Update Senko");
            cell.detailTextLabel.text = SenkoLocalizedText(@"Choose a Senko .deb package");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (row == 6) {
            cell.textLabel.text = SenkoLocalizedText(@"Language");
            cell.detailTextLabel.text = SenkoLanguageName();
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (row == 7) {
            cell.textLabel.text = SenkoLocalizedText(@"Classic home screen");
            cell.detailTextLabel.text = SenkoLocalizedText(@"The dome button instead of the status card");
            UISwitch *classic = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            classic.on = SenkoClassicHomeEnabled();
            SenkoSettingsStyleSwitch(classic);
            [classic addTarget:self action:@selector(classicHomeChanged:)
              forControlEvents:UIControlEventValueChanged];
            cell.accessoryView = classic;
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
        } else {
            cell.textLabel.text = SenkoLocalizedText(@"About");
            cell.detailTextLabel.text = nil;
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        }
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

- (void)autoConnectChanged:(UISwitch *)sw {
    [self applySetting:@"auto_connect" value:sw.on ? @"1" : @"0"];
}

- (void)autoReconnectChanged:(UISwitch *)sw {
    [self applySetting:@"auto_reconnect" value:sw.on ? @"1" : @"0"];
}

- (void)failoverChanged:(UISwitch *)sw {
    [self applySetting:@"failover" value:sw.on ? @"1" : @"0"];
}

/* the three text settings share one editor, so the row index is the only thing
   that has to stay in step with the table */
static NSString *SenkoSettingRowKey(NSInteger row) {
    if (row == 1) return @"dns_upstream";
    if (row == 2) return @"dns_local_port";
    return @"socks_port";
}

static NSString *SenkoSettingRowTitle(NSInteger row) {
    if (row == 1) return SenkoLocalizedText(@"Upstream DNS");
    if (row == 2) return SenkoLocalizedText(@"Local DNS port");
    return SenkoLocalizedText(@"SOCKS port");
}

static NSString *SenkoAttemptLimitName(NSString *attempts) {
    int n = [attempts intValue];
    if (n <= 0) return SenkoLocalizedText(@"Until it works");
    return [NSString stringWithFormat:SenkoLocalizedText(@"%d attempts"), n];
}

static NSString *SenkoRefreshIntervalName(NSString *hours) {
    int h = [hours intValue];
    if (h <= 0) return SenkoLocalizedText(@"Off");
    return [NSString stringWithFormat:SenkoLocalizedText(@"Every %d h"), h];
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
   to ios 15, and the daemon validates what comes out of it */
- (void)showEditorForRow:(NSInteger)row {
    NSString *key = SenkoSettingRowKey(row);
    UIAlertView *editor = [[[UIAlertView alloc]
        initWithTitle:SenkoSettingRowTitle(row)
              message:[key isEqualToString:@"dns_upstream"]
                          ? SenkoLocalizedText(@"An IPv4 address, for example 1.1.1.1")
                          : SenkoLocalizedText(@"A port number between 1 and 65535")
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:SenkoLocalizedText(@"Save"), nil] autorelease];
    editor.tag = 4206;
    if ([editor respondsToSelector:@selector(setAlertViewStyle:)]) {
        editor.alertViewStyle = UIAlertViewStylePlainTextInput;
        UITextField *field = [editor textFieldAtIndex:0];
        field.autocapitalizationType = UITextAutocapitalizationTypeNone;
        field.autocorrectionType = UITextAutocorrectionTypeNo;
        field.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
        field.text = [_settings objectForKey:key];
    }
    [_editingKey release];
    _editingKey = [key copy];
    [editor show];
}

/* the alert picker is the one control that exists unchanged from ios 5 to 15 */
- (void)showRefreshMenu {
    UIAlertView *pick = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Update subscriptions")
              message:SenkoRefreshIntervalName([_settings objectForKey:@"sub_refresh_hours"])
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:SenkoLocalizedText(@"Off"), @"6 h", @"12 h", @"24 h", nil] autorelease];
    pick.tag = 4204;
    [pick show];
}

/* the home screen rebuilds itself from the preference, so the switch only has
   to record it and tell the screen underneath to lay out again */
- (void)classicHomeChanged:(UISwitch *)sw {
    SenkoSetClassicHomeEnabled(sw.on);
    [[NSNotificationCenter defaultCenter] postNotificationName:SenkoThemeDidChangeNotification
                                                        object:nil];
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
        if (host.presentedViewController) {
            host = self;
        }
        UpdateInstallVC *uvc = [[[UpdateInstallVC alloc] initWithControl:_ctl
                                                             packagePath:pkg] autorelease];
        uvc.modalTransitionStyle = UIModalTransitionStyleCoverVertical;
        uvc.modalPresentationStyle = UIModalPresentationFullScreen;
        [host presentViewController:uvc animated:YES completion:nil];
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
        if (buttonIndex == alert.cancelButtonIndex || ![_editingKey length]) return;
        NSString *value = [[[alert textFieldAtIndex:0] text]
            stringByTrimmingCharactersInSet:
                [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([value length]) [self applySetting:_editingKey value:value];
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
    if (alert.tag == 4203) {
        if (buttonIndex == alert.cancelButtonIndex) return;
        NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
        [d setInteger:(NSInteger)(buttonIndex - 1) forKey:SENKO_SERVER_SORT_KEY];
        [d synchronize];
        [_tv reloadData];
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

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    if (ip.section == 0) {
        if (!_settings) return;
        if (ip.row == 3) [self showRefreshMenu];
        else if (ip.row == 4) [self showAttemptMenu];
        return;
    }
    if (ip.section == 1) {
        if (ip.row == 0) {
            RulesVC *vc = [[[RulesVC alloc] initWithControl:_ctl] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        } else if (_settings) {
            [self showEditorForRow:ip.row];
        }
        return;
    }
    if (ip.section == 3) {
        DevMenuVC *vc = [[[DevMenuVC alloc] initWithControl:_ctl] autorelease];
        [self.navigationController pushViewController:vc animated:YES];
        return;
    }
    if (ip.section == 2) {
        NSInteger row = SenkoAppSettingsRow(ip.row);
        if (row == 0) {
            [self showSortMenu];
        } else if (row == 1) {
            ThemesVC *vc = [[[ThemesVC alloc] init] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        } else if (row == 2) {
            LogsVC *vc = [[[LogsVC alloc] init] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        } else if (row == 3) {
            [_ctl exportBackup:^(NSString *reply) {
                NSString *msg = [reply hasPrefix:@"OK "] ?
                    SenkoLocalizedText(@"Saved to Documents/senko-backup.senko") : reply;
                [self showBackupMessage:msg ?: SenkoLocalizedText(@"Backup export failed")];
            }];
        } else if (row == 4) {
            [self openBackupBrowser];
        } else if (row == 5) {
            [self openUpdateBrowser];
        } else if (row == 6) {
            UIAlertView *language = [[[UIAlertView alloc]
                initWithTitle:SenkoLocalizedText(@"Language")
                      message:SenkoLanguageName()
                     delegate:self
            cancelButtonTitle:SenkoLocalizedText(@"Cancel")
            otherButtonTitles:@"English", @"Русский", @"中文", nil] autorelease];
            language.tag = 4202;
            [language show];
        } else if (row == 8) {
            AboutVC *vc = [[[AboutVC alloc] init] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        }
    }
}

/* the alert picker is the one control that exists unchanged from ios 5 to 15 */
- (void)showSortMenu {
    UIAlertView *sort = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Sort servers")
              message:SenkoSortModeName()
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:SenkoLocalizedText(@"Stored order"),
                      SenkoLocalizedText(@"By name"),
                      SenkoLocalizedText(@"By latency"), nil] autorelease];
    sort.tag = 4203;
    [sort show];
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
        [vc dismissViewControllerAnimated:YES completion:nil];
    }];
}

@end
