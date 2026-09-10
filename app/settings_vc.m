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
    tv.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14f]
        : [UIColor colorWithWhite:1 alpha:0.16f];
    if ([tv respondsToSelector:@selector(setSeparatorInset:)]) {
        void (*setSeparatorInset)(id, SEL, UIEdgeInsets) =
            (void (*)(id, SEL, UIEdgeInsets))objc_msgSend;
        setSeparatorInset(tv, @selector(setSeparatorInset:),
                          UIEdgeInsetsMake(0, 16.0f, 0, 0));
    }
    if ([tv respondsToSelector:@selector(setBackgroundView:)])
        tv.backgroundView = nil;
}

@interface SenkoSettingsCellBackground : UIView {
    UIRectCorner _roundedCorners;
}
@property(nonatomic, assign) UIRectCorner roundedCorners;
@end

@implementation SenkoSettingsCellBackground

- (UIRectCorner)roundedCorners {
    return _roundedCorners;
}

- (void)setRoundedCorners:(UIRectCorner)corners {
    if (_roundedCorners == corners) return;
    _roundedCorners = corners;
    [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    if (!_roundedCorners) {
        self.layer.mask = nil;
        return;
    }
    CAShapeLayer *mask = [CAShapeLayer layer];
    mask.frame = self.bounds;
    CGFloat radius = SenkoThemeCardRadius();
    mask.path = [UIBezierPath bezierPathWithRoundedRect:self.bounds
                                       byRoundingCorners:_roundedCorners
                                             cornerRadii:CGSizeMake(radius, radius)].CGPath;
    self.layer.mask = mask;
}

@end

/* the row count comes from the data source, not from the table: asking the
   table for it while it is building a cell re-enters a table that has not
   finished loading */
static void SenkoSettingsApplyCellBackground(UITableView *tv,
                                              UITableViewCell *cell,
                                              NSIndexPath *ip,
                                              NSInteger rowsInSection) {
    SenkoSettingsCellBackground *bg = nil;
    if ([cell.backgroundView isKindOfClass:[SenkoSettingsCellBackground class]]) {
        bg = (SenkoSettingsCellBackground *)cell.backgroundView;
    } else {
        bg = [[[SenkoSettingsCellBackground alloc] initWithFrame:CGRectZero] autorelease];
        cell.backgroundView = bg;
    }
    (void)tv;
    UIRectCorner corners = 0;
    if (ip.row == 0)
        corners |= UIRectCornerTopLeft | UIRectCornerTopRight;
    if (ip.row == rowsInSection - 1)
        corners |= UIRectCornerBottomLeft | UIRectCornerBottomRight;
    bg.roundedCorners = corners;
    bg.backgroundColor = kCellHi;
    bg.opaque = !SenkoThemeIsIos26();
    cell.backgroundColor = [UIColor clearColor];
    cell.opaque = !SenkoThemeIsIos26();
    cell.contentView.backgroundColor = [UIColor clearColor];
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
    NSString *_daemonState;
    BOOL _backupImportMode;
    NSString *_pendingBackupPath;

}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    [_ctl release];
    [_daemonState release];
    [_pendingBackupPath release];
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Settings";
    _ctl = [[SenkoControl alloc] initWithSocketPath:SENKO_SOCK];
    _daemonState = [SenkoLocalizedText(@"checking") copy];

    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setAutomaticallyAdjustsScrollViewInsets:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setAutomaticallyAdjustsScrollViewInsets:), YES);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);

    self.navigationItem.leftBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                       target:self
                                                       action:@selector(donePressed)] autorelease];

    SenkoApplyScreenChrome(self.view);
    _tv = [[UITableView alloc] initWithFrame:SenkoViewBounds(self.view)
                                       style:UITableViewStyleGrouped];
    _tv.dataSource = self;
    _tv.delegate = self;
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

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("settings");
    [super viewWillAppear:animated];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    SenkoApplyScreenChrome(self.view);
    [self layoutSettings];
    [_ctl statusState:^(NSString *state) {
        [_daemonState release];
        _daemonState = [(state ? state : @"unreachable") copy];
        NSIndexPath *path = [NSIndexPath indexPathForRow:0 inSection:0];
        if ([_tv numberOfRowsInSection:0] > 0)
            [_tv reloadRowsAtIndexPaths:[NSArray arrayWithObject:path]
                       withRowAnimation:UITableViewRowAnimationNone];
    }];
}

- (void)layoutSettings {
    CGRect b = SenkoViewBounds(self.view);
    UIView *bg = [self.view viewWithTag:9111];
    if (bg) bg.frame = b;
    _tv.frame = CGRectMake(0, 0, b.size.width, b.size.height);
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

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv { return 2; }

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    if (s == 0) return 4;
    return 10;
}

- (NSString *)headerTextForSection:(NSInteger)s {
    if (s == 0) return SenkoLocalizedText(@"CONNECTION");
    if (s == 1) return SenkoLocalizedText(@"APP");
    return nil;
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 28.0f;
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return 60.0f;
}

- (NSString *)footerTextForSection:(NSInteger)s {
    if (s == 0)
        return SenkoLocalizedText(@"Routing is not a switch. Senko always carries every app and every system connection, because senkod runs as root on the jailbreak and rewrites the system routes itself, so there is nothing to configure outside this app. The local proxy is reachable only from this device.");
    return SenkoLocalizedText(@"Hide links only changes what is shown on screen. Backups keep the complete configuration.");
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    NSString *text = [self footerTextForSection:s];
    CGFloat width = tv.bounds.size.width - 40.0f;
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
    UILabel *label = [[[UILabel alloc] initWithFrame:
                       CGRectMake(20.0f, 4.0f, width - 40.0f, height - 8.0f)] autorelease];
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
        SenkoStyleMutedLabel(label);
    }
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
    [wrap addSubview:label];
    return wrap;
}

/* group titles sit centred in the accent, the way aniliberty sets its own out;
   the footers stay left because they are sentences, not labels */
- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    return [self sectionTextViewWithText:[self headerTextForSection:s]
                                    font:[UIFont boldSystemFontOfSize:12.0f]
                                  height:28.0f
                                   width:tv.bounds.size.width
                                centered:YES];
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
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"set";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                       reuseIdentifier:cid] autorelease];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
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
    cell.textLabel.font = [UIFont systemFontOfSize:16.0f];
    cell.detailTextLabel.font = [UIFont systemFontOfSize:13.0f];
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
        if (ip.row == 0) {
            cell.textLabel.text = @"State";
            cell.detailTextLabel.text = _daemonState;
        } else if (ip.row == 1) {
            cell.textLabel.text = @"Routing";
            cell.detailTextLabel.text =
                SenkoLocalizedText(@"Whole device, set up by the root daemon");
        } else if (ip.row == 2) {
            cell.textLabel.text = @"Version";
            cell.detailTextLabel.text = SENKO_VERSION;
        } else {
            cell.textLabel.text = @"Edit selected server";
            cell.detailTextLabel.text = SenkoLocalizedText(@"Manually added profiles only");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        }
    } else {
        if (ip.row == 0) {
            cell.textLabel.text = @"Hide links";
            cell.detailTextLabel.text = nil;
            UISwitch *sw = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            sw.on = [[NSUserDefaults standardUserDefaults] boolForKey:SENKO_HIDE_LINKS_KEY];
            [sw addTarget:self action:@selector(hideLinksChanged:) forControlEvents:UIControlEventValueChanged];
            cell.accessoryView = sw;
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
        } else if (ip.row == 1) {
            cell.textLabel.text = SenkoLocalizedText(@"VPN badge in status bar");
            cell.detailTextLabel.text = SenkoLocalizedText(@"Turn off if the wifi glyph disappears");
            UISwitch *badge = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            badge.on = SenkoVPNBadgeEnabled();
            [badge addTarget:self action:@selector(vpnBadgeChanged:)
            forControlEvents:UIControlEventValueChanged];
            cell.accessoryView = badge;
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
        } else if (ip.row == 2) {
            cell.textLabel.text = @"Sort servers";
            cell.detailTextLabel.text = SenkoSortModeName();
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (ip.row == 3) {
            cell.textLabel.text = @"Themes";
            cell.detailTextLabel.text = SenkoThemeStatusLine();
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (ip.row == 4) {
            cell.textLabel.text = @"System Logs";
            cell.detailTextLabel.text = SenkoLocalizedText(@"senkod + awg combined");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (ip.row == 5) {
            cell.textLabel.text = SenkoLocalizedText(@"Export backup");
            cell.detailTextLabel.text = SenkoLocalizedText(@"Save a config file to Documents");
            SenkoStyleSelectableCell(cell);
        } else if (ip.row == 6) {
            cell.textLabel.text = SenkoLocalizedText(@"Restore backup");
            cell.detailTextLabel.text = SenkoLocalizedText(@"Validate, then replace configuration");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (ip.row == 7) {
            cell.textLabel.text = @"Update Senko";
            cell.detailTextLabel.text = SenkoLocalizedText(@"Choose a Senko .deb package");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (ip.row == 8) {
            cell.textLabel.text = SenkoLocalizedText(@"Language");
            cell.detailTextLabel.text = SenkoLanguageName();
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else {
            cell.textLabel.text = SenkoLocalizedText(@"About");
            cell.detailTextLabel.text = nil;
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        }
    }
    return cell;
}

/* springboard cannot read the app defaults domain, so the switch is mirrored as
   a marker file next to the state file the tweak already watches */
- (void)vpnBadgeChanged:(UISwitch *)sw {
    SenkoVPNBadgeSetEnabled(sw.on);
}

- (void)hideLinksChanged:(UISwitch *)sw {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:sw.on forKey:SENKO_HIDE_LINKS_KEY];
    [d synchronize];
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
        if (buttonIndex == 1) SenkoSetLanguage(NO);
        else if (buttonIndex == 2) SenkoSetLanguage(YES);
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
    if (ip.section == 0 && ip.row == 3) {
        if ([_daemonState isEqualToString:@"connected"] || [_daemonState isEqualToString:@"connecting"]) {
            UIAlertView *av = [[[UIAlertView alloc] initWithTitle:@"Disconnect first"
                                                           message:@"A live profile cannot be edited"
                                                          delegate:nil
                                                 cancelButtonTitle:@"OK"
                                                 otherButtonTitles:nil] autorelease];
            [av show];
            return;
        }
        [_ctl listCatalog:^(NSArray *servers, NSArray *subs, NSArray *order) {
            (void)subs;
            (void)order;
            SenkoServer *selected = nil;
            for (SenkoServer *server in servers) {
                if (server->selected) { selected = server; break; }
            }
            if (!selected) return;
            if (selected->group >= 0) {
                UIAlertView *av = [[[UIAlertView alloc] initWithTitle:@"Subscription profile"
                                                               message:@"Refresh the subscription to change it"
                                                              delegate:nil
                                                     cancelButtonTitle:@"OK"
                                                     otherButtonTitles:nil] autorelease];
                [av show];
                return;
            }
            [_ctl serverLinkIndex:selected->index reply:^(NSString *link) {
                if (![link length]) return;
                EditServerVC *editor = [[[EditServerVC alloc] initWithLink:link
                                                                       index:selected->index
                                                                    delegate:self] autorelease];
                UINavigationController *nav = [[[UINavigationController alloc]
                                                initWithRootViewController:editor] autorelease];
                StyleNavBarClassic(nav);
                nav.modalPresentationStyle = UIModalPresentationFullScreen;
                [self presentViewController:nav animated:YES completion:nil];
            }];
        }];
        return;
    }
    if (ip.section == 1) {
        if (ip.row == 2) {
            [self showSortMenu];
        } else if (ip.row == 3) {
            ThemesVC *vc = [[[ThemesVC alloc] init] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        } else if (ip.row == 4) {
            LogsVC *vc = [[[LogsVC alloc] init] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        } else if (ip.row == 5) {
            [_ctl exportBackup:^(NSString *reply) {
                NSString *msg = [reply hasPrefix:@"OK "] ?
                    SenkoLocalizedText(@"Saved to Documents/senko-backup.senko") : reply;
                [self showBackupMessage:msg ?: SenkoLocalizedText(@"Backup export failed")];
            }];
        } else if (ip.row == 6) {
            [self openBackupBrowser];
        } else if (ip.row == 7) {
            [self openUpdateBrowser];
        } else if (ip.row == 8) {
            UIAlertView *language = [[[UIAlertView alloc]
                initWithTitle:SenkoLocalizedText(@"Language")
                      message:SenkoLanguageName()
                     delegate:self
            cancelButtonTitle:SenkoLocalizedText(@"Cancel")
            otherButtonTitles:@"English", @"Русский", nil] autorelease];
            language.tag = 4202;
            [language show];
        } else if (ip.row == 9) {
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
    [_ctl deleteServerIndex:idx reply:^(NSString *reply) {
        if (!reply || [reply hasPrefix:@"ERR"]) return;
        [_ctl addServerLink:link reply:^(NSString *added) {
            if (added && [added hasPrefix:@"OK"])
                [vc dismissViewControllerAnimated:YES completion:nil];
        }];
    }];
}

@end
