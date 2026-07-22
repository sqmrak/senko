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


@implementation SettingsVC {

    UITableView *_tv;
    SenkoControl *_ctl;
    NSString *_daemonState;

}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_ctl release];
    [_daemonState release];
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Settings";
    _ctl = [[SenkoControl alloc] initWithSocketPath:SENKO_SOCK];
    _daemonState = [@"..." copy];

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
    _tv = [[[UITableView alloc] initWithFrame:SenkoViewBounds(self.view)
                                        style:UITableViewStyleGrouped] autorelease];
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.backgroundColor = kBG;
    _tv.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14f]
        : [UIColor colorWithWhite:1 alpha:0.16f];
    if ([_tv respondsToSelector:@selector(setBackgroundView:)])
        _tv.backgroundView = nil;
    [self.view addSubview:_tv];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    SenkoApplyScreenChrome(self.view);
    _tv.backgroundColor = kBG;
    _tv.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14f]
        : [UIColor colorWithWhite:1 alpha:0.16f];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

- (void)viewWillAppear:(BOOL)animated {
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
    return 5; /* hide, themes, logs, update, about */
}

- (NSString *)tableView:(UITableView *)tv titleForHeaderInSection:(NSInteger)s {
    (void)tv;
    if (s == 0) return @"DAEMON";
    if (s == 1) return @"UTILITIES";
    return nil;
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 28.0f;
}

- (NSString *)tableView:(UITableView *)tv titleForFooterInSection:(NSInteger)s {
    (void)tv;
    if (s == 0)
        return @"SOCKS is localhost-only by default. socks_public=1 in config opens it to the LAN.";
    return nil;
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    (void)tv;
    return (s == 0) ? 36.0f : 10.0f;
}

- (void)tableView:(UITableView *)tv willDisplayHeaderView:(UIView *)view
                       forSection:(NSInteger)s {
    (void)tv; (void)s;
    if (![view respondsToSelector:@selector(textLabel)]) return;
    UILabel *label = [(UITableViewHeaderFooterView *)view textLabel];
    label.font = [UIFont boldSystemFontOfSize:13.0f];
    SenkoStyleAccentLabel(label);
}

- (void)tableView:(UITableView *)tv willDisplayFooterView:(UIView *)view
                       forSection:(NSInteger)s {
    (void)tv; (void)s;
    if (![view respondsToSelector:@selector(textLabel)]) return;
    UILabel *label = [(UITableViewHeaderFooterView *)view textLabel];
    label.font = [UIFont systemFontOfSize:11.0f];
    SenkoStyleMutedLabel(label);
}

- (void)tableView:(UITableView *)tv willDisplayCell:(UITableViewCell *)cell
 forRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    UIView *bg = cell.backgroundView;
    if (!bg) {
        bg = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
        cell.backgroundView = bg;
    }
    bg.backgroundColor = kCellHi;
    bg.layer.cornerRadius = 10.0f;
    bg.layer.masksToBounds = YES;
    bg.opaque = !SenkoThemeIsIos26();
    UIView *line = [bg viewWithTag:9122];
    if (!line) {
        line = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
        line.tag = 9122;
        line.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                 UIViewAutoresizingFlexibleTopMargin;
        [bg addSubview:line];
    }
    NSInteger rowCount = [tv numberOfRowsInSection:ip.section];
    line.hidden = (ip.row >= rowCount - 1);
    line.frame = CGRectMake(12.0f, MAX(0.0f, bg.bounds.size.height - 1.0f),
                            MAX(0.0f, bg.bounds.size.width - 24.0f), 1.0f);
    line.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.16f]
        : [UIColor colorWithWhite:1 alpha:0.22f];
    cell.backgroundColor = [UIColor clearColor];
    cell.opaque = !SenkoThemeIsIos26();
    cell.contentView.backgroundColor = [UIColor clearColor];
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"set";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                       reuseIdentifier:cid] autorelease];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.accessoryView = nil;
    cell.detailTextLabel.text = nil;
    SenkoStyleInkLabel(cell.textLabel);
    SenkoStyleAccentLabel(cell.detailTextLabel);
    cell.textLabel.font = [UIFont boldSystemFontOfSize:15];
    cell.detailTextLabel.font = [UIFont systemFontOfSize:14];
    UIView *bg = cell.backgroundView;
    if (!bg) {
        bg = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
        cell.backgroundView = bg;
    }
    bg.backgroundColor = kCellHi;
    bg.layer.cornerRadius = 10.0f;
    bg.layer.masksToBounds = YES;
    bg.opaque = !SenkoThemeIsIos26();
    cell.backgroundColor = [UIColor clearColor];
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];

    if (ip.section == 0) {
        if (ip.row == 0) {
            cell.textLabel.text = @"State";
            cell.detailTextLabel.text = _daemonState;
        } else if (ip.row == 1) {
            cell.textLabel.text = @"Routing";
            cell.detailTextLabel.text = @"full-device";
        } else if (ip.row == 2) {
            cell.textLabel.text = @"Version";
            cell.detailTextLabel.text = SENKO_VERSION;
        } else {
            cell.textLabel.text = @"Edit selected server";
            cell.detailTextLabel.text = @"manual profiles only";
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            cell.selectionStyle = UITableViewCellSelectionStyleBlue;
        }
    } else {
        if (ip.row == 0) {
            cell.textLabel.text = @"Hide server links";
            cell.detailTextLabel.text = nil;
            UISwitch *sw = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            sw.on = [[NSUserDefaults standardUserDefaults] boolForKey:SENKO_HIDE_LINKS_KEY];
            [sw addTarget:self action:@selector(hideLinksChanged:) forControlEvents:UIControlEventValueChanged];
            if ([sw respondsToSelector:@selector(setOnTintColor:)])
                sw.onTintColor = kAccentBlue;
            cell.accessoryView = sw;
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
        } else if (ip.row == 1) {
            cell.textLabel.text = @"Themes";
            cell.detailTextLabel.text = SenkoThemeStatusLine();
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            cell.selectionStyle = UITableViewCellSelectionStyleBlue;
        } else if (ip.row == 2) {
            cell.textLabel.text = @"System Logs";
            cell.detailTextLabel.text = nil;
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            cell.selectionStyle = UITableViewCellSelectionStyleBlue;
        } else if (ip.row == 3) {
            cell.textLabel.text = @"Update Senko";
            cell.detailTextLabel.text = @"install a .deb";
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            cell.selectionStyle = UITableViewCellSelectionStyleBlue;
        } else {
            cell.textLabel.text = @"About";
            cell.detailTextLabel.text = nil;
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            cell.selectionStyle = UITableViewCellSelectionStyleBlue;
        }
    }
    return cell;
}

- (void)hideLinksChanged:(UISwitch *)sw {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:sw.on forKey:SENKO_HIDE_LINKS_KEY];
    [d synchronize];
}

- (void)openUpdateBrowser {
    FileImportVC *files = [[[FileImportVC alloc] initWithPath:nil delegate:self] autorelease];
    files.title = @"Update Senko";
    [self.navigationController pushViewController:files animated:YES];
}

- (void)fileImportVCDidCancel:(FileImportVC *)vc {
    (void)vc;
/* return to settings */
    [self.navigationController popToViewController:self animated:YES];
}

- (void)presentUpdateInstallForPath:(NSString *)path {
    if (![path length]) return;
    NSString *pkg = [[path copy] autorelease];
    UINavigationController *nav = self.navigationController;
    void (^showInstall)(void) = ^{
/* present from settings */
        UIViewController *host = nav ? (UIViewController *)nav : (UIViewController *)self;
        if (host.presentedViewController) {
/* use self when no nav is active */
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
        if (ip.row == 1) {
            ThemesVC *vc = [[[ThemesVC alloc] init] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        } else if (ip.row == 2) {
            LogsVC *vc = [[[LogsVC alloc] init] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        } else if (ip.row == 3) {
            [self openUpdateBrowser];
        } else if (ip.row == 4) {
            AboutVC *vc = [[[AboutVC alloc] init] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        }
    }
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
