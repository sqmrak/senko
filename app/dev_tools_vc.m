#import "dev_tools_vc.h"

#import "control_client.h"
#import "ui_theme.h"
#import "app_common.h"
#import "crash_report.h"
#include "../common/senko_paths.h"
#import <objc/message.h>

#define DEV_SHEET_BACKEND     4501
#define DEV_SHEET_PF_MODE     4502
#define DEV_SHEET_BLOCK       4503
#define DEV_CONFIRM_FLUSH_DNS    4511
#define DEV_CONFIRM_FLUSH_BYPASS 4512
#define DEV_CONFIRM_FLUSH_RULES  4513
#define DEV_CONFIRM_FLUSH_CONFIG 4514
#define DEV_CONFIRM_HWID_RESET   4515
#define DEV_CONFIRM_SAFE_MODE    4516
#define DEV_CONFIRM_TEST_EMPTY   4517
#define DEV_SHEET_FIXTURES       4504

/* the daemon's own vocabulary. the ui never invents a value for these, because
   a word the settings parser does not know is refused with no way to tell why */
static NSString *const kBackendValues[] = { @"auto", @"go", @"c", @"app_proxy" };
#define DEV_BACKEND_COUNT 4

static NSString *const kBlockValues[] = { @"zero", @"nxdomain", @"refused" };
#define DEV_BLOCK_COUNT 3

/* the eight pf syntax variants in the order routing.h declares them, so the
   index shown here is the index the daemon accepts */
static NSString *const kPFModeNames[] = {
    @"route-to lo0", @"route-to lo0, no gateway", @"divert-to",
    @"divert-to, legacy placement", @"rdr-to with state flags",
    @"rdr-to with keep state", @"legacy rdr", @"compat rdr"
};
#define DEV_PF_MODE_COUNT 8

static NSString *BackendTitle(NSString *value) {
    if ([value isEqualToString:@"go"]) return SenkoLocalizedText(@"Go core");
    if ([value isEqualToString:@"c"]) return SenkoLocalizedText(@"C core");
    if ([value isEqualToString:@"app_proxy"]) return SenkoLocalizedText(@"Connect hook only");
    return SenkoLocalizedText(@"Auto");
}

static NSString *BlockTitle(NSString *value) {
    if ([value isEqualToString:@"nxdomain"]) return @"NXDOMAIN";
    if ([value isEqualToString:@"refused"]) return @"REFUSED";
    return SenkoLocalizedText(@"Zero address");
}

static NSString *PFModeTitle(NSString *value) {
    int index = [value intValue];
    if (![value length] || [value isEqualToString:@"auto"])
        return SenkoLocalizedText(@"Auto");
    if (index < 0 || index >= DEV_PF_MODE_COUNT) return value;
    return [NSString stringWithFormat:@"%d: %@", index, kPFModeNames[index]];
}

@implementation DevTogglesVC

- (void)dealloc {
    [_settings release];
    [super dealloc];
}

- (NSString *)devTitle {
    return SenkoLocalizedText(@"Force");
}

- (const char *)devScreenName {
    return "devtoggles";
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [_ctl daemonSettings:^(NSDictionary *values) {
        [_settings release];
        _settings = values ? [values mutableCopy] : nil;
        [_tv reloadData];
    }];
}

- (NSString *)value:(NSString *)key {
    return [_settings objectForKey:key] ?: @"";
}

- (BOOL)flag:(NSString *)key {
    return [[self value:key] isEqualToString:@"1"];
}

/* a switch the daemon refused must not keep showing the new position, and the
   daemon is the only place that knows what it took */
- (void)apply:(NSString *)key value:(NSString *)value {
    if (!_settings) return;
    [_ctl setSetting:key value:value reply:^(NSString *reply) {
        if ([reply hasPrefix:@"OK "])
            [_settings setObject:value forKey:key];
        else
            [self devSay:SenkoLocalizedText(@"Force")
                 message:reply ? SenkoHumanReadableError(reply)
                               : SenkoLocalizedText(@"Daemon is unreachable")];
        [_tv reloadData];
    }];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return 4;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    if (s == 0) return 2;
    if (s == 1) return 3;
    if (s == 2) return 2;
    return 3;
}

- (NSString *)devHeaderForSection:(NSInteger)s {
    if (s == 0) return SenkoLocalizedText(@"FORCE");
    if (s == 1) return SenkoLocalizedText(@"LISTENERS AND DNS");
    if (s == 2) return SenkoLocalizedText(@"SUBSCRIPTIONS");
    return SenkoLocalizedText(@"DIAGNOSTICS");
}

- (NSString *)devFooterForSection:(NSInteger)s {
    if (!_settings && s == 0) return SenkoLocalizedText(@"Daemon is unreachable");
    if (s == 0) return SenkoLocalizedText(@"Applied on the next connect.");
    return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    NSInteger rows = [self tableView:tv numberOfRowsInSection:ip.section];
    UITableViewCell *cell = [self devCellForTable:tv indexPath:ip rows:rows];
    UISwitch *toggle = nil;

    if (ip.section == 0) {
        if (ip.row == 0) {
            cell.textLabel.text = SenkoLocalizedText(@"Backend");
            cell.detailTextLabel.text = BackendTitle([self value:@"force_backend"]);
        } else {
            cell.textLabel.text = SenkoLocalizedText(@"pf variant");
            cell.detailTextLabel.text = PFModeTitle([self value:@"force_pf_mode"]);
        }
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        if (_settings) SenkoStyleSelectableCell(cell);
        return cell;
    }

    if (ip.section == 1) {
        if (ip.row == 0) {
            cell.textLabel.text = SenkoLocalizedText(@"SOCKS on 0.0.0.0");
            toggle = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            toggle.on = [self flag:@"socks_public"];
            [toggle addTarget:self action:@selector(socksPublicChanged:)
             forControlEvents:UIControlEventValueChanged];
        } else if (ip.row == 1) {
            cell.textLabel.text = SenkoLocalizedText(@"Blocked answers");
            cell.detailTextLabel.text = BlockTitle([self value:@"block_response"]);
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            if (_settings) SenkoStyleSelectableCell(cell);
        } else {
            cell.textLabel.text = SenkoLocalizedText(@"Flush DNS cache");
            cell.textLabel.textColor = kAccentBlue;
            SenkoStyleSelectableCell(cell);
        }
    } else if (ip.section == 2) {
        if (ip.row == 0) {
            cell.textLabel.text = SenkoLocalizedText(@"Ignore device gating");
            toggle = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            toggle.on = [self flag:@"sub_ignore_gating"];
            [toggle addTarget:self action:@selector(gatingChanged:)
             forControlEvents:UIControlEventValueChanged];
        } else {
            cell.textLabel.text = SenkoLocalizedText(@"Reset dynamic bypass");
            cell.textLabel.textColor = kAccentBlue;
            SenkoStyleSelectableCell(cell);
        }
    } else {
        if (ip.row == 0) {
            cell.textLabel.text = SenkoLocalizedText(@"Session trace");
            toggle = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            toggle.on = [self flag:@"trace"];
            [toggle addTarget:self action:@selector(traceChanged:)
             forControlEvents:UIControlEventValueChanged];
        } else if (ip.row == 1) {
            cell.textLabel.text = SenkoLocalizedText(@"FPS overlay");
            UISwitch *fps = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            fps.on = SenkoFPSOverlayEnabled();
            [fps addTarget:self action:@selector(fpsChanged:)
          forControlEvents:UIControlEventValueChanged];
            cell.accessoryView = fps;
            return cell;
        } else {
            cell.textLabel.text = SenkoLocalizedText(@"Reset settings");
            cell.textLabel.textColor = kAccentBlue;
            SenkoStyleSelectableCell(cell);
        }
    }

    if (toggle) {
        toggle.enabled = _settings != nil;
        cell.accessoryView = toggle;
    }
    return cell;
}

- (void)socksPublicChanged:(UISwitch *)sw {
    [self apply:@"socks_public" value:sw.on ? @"1" : @"0"];
}

- (void)gatingChanged:(UISwitch *)sw {
    [self apply:@"sub_ignore_gating" value:sw.on ? @"1" : @"0"];
}

- (void)traceChanged:(UISwitch *)sw {
    [self apply:@"trace" value:sw.on ? @"1" : @"0"];
}

- (void)fpsChanged:(UISwitch *)sw {
    SenkoFPSOverlaySetEnabled(sw.on);
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    if (ip.section == 0 && !_settings) return;
    if (ip.section == 0) {
        [self showPicker:ip.row == 0 ? DEV_SHEET_BACKEND : DEV_SHEET_PF_MODE];
    } else if (ip.section == 1) {
        if (ip.row == 1 && _settings) [self showPicker:DEV_SHEET_BLOCK];
        else if (ip.row == 2)
            [self devConfirm:SenkoLocalizedText(@"Flush DNS cache")
                     message:nil
                      button:SenkoLocalizedText(@"Flush")
                         tag:DEV_CONFIRM_FLUSH_DNS];
    } else if (ip.section == 2) {
        if (ip.row == 1)
            [self devConfirm:SenkoLocalizedText(@"Reset dynamic bypass")
                     message:nil
                      button:SenkoLocalizedText(@"Reset")
                         tag:DEV_CONFIRM_FLUSH_BYPASS];
    } else if (ip.section == 3 && ip.row == 2) {
        [self devConfirm:SenkoLocalizedText(@"Reset settings")
                 message:SenkoLocalizedText(@"Catalog and rules are kept.")
                  button:SenkoLocalizedText(@"Reset")
                     tag:DEV_CONFIRM_FLUSH_CONFIG];
    }
}

/* a sheet rather than an alert: nine entries in an alert run off a 3.5 inch
   screen */
- (void)showPicker:(NSInteger)tag {
    NSString *title = tag == DEV_SHEET_BACKEND ? SenkoLocalizedText(@"Backend")
                    : tag == DEV_SHEET_BLOCK ? SenkoLocalizedText(@"Blocked answers")
                    : SenkoLocalizedText(@"pf variant");
    UIActionSheet *sheet = [[[UIActionSheet alloc]
             initWithTitle:title
                  delegate:self
         cancelButtonTitle:nil
    destructiveButtonTitle:nil
         otherButtonTitles:nil] autorelease];
    if (tag == DEV_SHEET_BACKEND) {
        for (int i = 0; i < DEV_BACKEND_COUNT; ++i)
            [sheet addButtonWithTitle:BackendTitle(kBackendValues[i])];
    } else if (tag == DEV_SHEET_BLOCK) {
        for (int i = 0; i < DEV_BLOCK_COUNT; ++i)
            [sheet addButtonWithTitle:BlockTitle(kBlockValues[i])];
    } else {
        [sheet addButtonWithTitle:SenkoLocalizedText(@"Auto")];
        for (int i = 0; i < DEV_PF_MODE_COUNT; ++i)
            [sheet addButtonWithTitle:[NSString stringWithFormat:@"%d: %@", i, kPFModeNames[i]]];
    }
    sheet.tag = tag;
    sheet.cancelButtonIndex = [sheet addButtonWithTitle:SenkoLocalizedText(@"Cancel")];
    [sheet showInView:self.view];
}

- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)index {
    if (index == sheet.cancelButtonIndex || index < 0) return;
    if (sheet.tag == DEV_SHEET_BACKEND) {
        if (index < DEV_BACKEND_COUNT) [self apply:@"force_backend" value:kBackendValues[index]];
        return;
    }
    if (sheet.tag == DEV_SHEET_BLOCK) {
        if (index < DEV_BLOCK_COUNT) [self apply:@"block_response" value:kBlockValues[index]];
        return;
    }
    if (sheet.tag == DEV_SHEET_PF_MODE) {
        if (index == 0) { [self apply:@"force_pf_mode" value:@"auto"]; return; }
        if (index <= DEV_PF_MODE_COUNT)
            [self apply:@"force_pf_mode"
                  value:[NSString stringWithFormat:@"%d", (int)index - 1]];
    }
}

- (void)devConfirmed:(NSInteger)tag {
    NSString *target = tag == DEV_CONFIRM_FLUSH_DNS ? @"dns"
                     : tag == DEV_CONFIRM_FLUSH_BYPASS ? @"bypass"
                     : tag == DEV_CONFIRM_FLUSH_CONFIG ? @"config" : nil;
    if (!target) return;
    [_ctl flushTarget:target reply:^(NSString *reply) {
        [self devSay:SenkoLocalizedText(@"Force")
             message:[reply hasPrefix:@"OK "]
                         ? SenkoLocalizedText(@"Done.")
                         : SenkoHumanReadableError(reply)];
        if (tag == DEV_CONFIRM_FLUSH_CONFIG) {
            [_ctl daemonSettings:^(NSDictionary *values) {
                [_settings release];
                _settings = values ? [values mutableCopy] : nil;
                [_tv reloadData];
            }];
        }
    }];
}

@end

@implementation DevRescueVC

- (void)dealloc {
    [_hwid release];
    [super dealloc];
}

- (NSString *)devTitle {
    return SenkoLocalizedText(@"Rescue");
}

- (const char *)devScreenName {
    return "devrescue";
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [_hwid release];
    _hwid = [SenkoSharedDeviceHWID() copy];
    [_tv reloadData];
    if (_hwid) return;
    [_ctl deviceHWID:^(NSString *value) {
        [_hwid release];
        _hwid = [value copy];
        [_tv reloadData];
    }];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return 3;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    if (s == 0) return 3;
    if (s == 1) return 2;
    return 3;
}

- (NSString *)devHeaderForSection:(NSInteger)s {
    if (s == 0) return SenkoLocalizedText(@"LAUNCH");
    if (s == 1) return SenkoLocalizedText(@"DEVICE ID");
    return SenkoLocalizedText(@"REMOVE");
}

- (NSString *)crashReportText {
    NSMutableString *text = [NSMutableString string];
    NSString *previous = [NSString stringWithContentsOfFile:@SENKO_CRASH_PREV
                                                   encoding:NSUTF8StringEncoding
                                                      error:NULL];
    NSString *stage = [NSString stringWithContentsOfFile:@SENKO_CRASH_STAGE
                                                encoding:NSUTF8StringEncoding
                                                   error:NULL];
    [text appendFormat:@"%s\n%@\n\n", SENKO_CRASH_PREV,
                       [previous length] ? previous : SenkoLocalizedText(@"nothing recorded")];
    [text appendFormat:@"%s\n%@\n", SENKO_CRASH_STAGE,
                       [stage length] ? stage : SenkoLocalizedText(@"nothing recorded")];
    return text;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    NSInteger rows = [self tableView:tv numberOfRowsInSection:ip.section];
    UITableViewCell *cell = [self devCellForTable:tv indexPath:ip rows:rows];

    if (ip.section == 0) {
        if (ip.row == 0) {
            cell.textLabel.text = SenkoLocalizedText(@"Crash and launch log");
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else if (ip.row == 1) {
            int failed = SenkoCrashFailedLaunches();
            cell.textLabel.text = SenkoLocalizedText(@"Failed launches");
            cell.detailTextLabel.text = [NSString stringWithFormat:@"%d", failed];
        } else {
            BOOL safe = SenkoCrashSafeMode();
            cell.textLabel.text = safe
                ? SenkoLocalizedText(@"Leave safe mode")
                : SenkoLocalizedText(@"Safe mode next launch");
            cell.textLabel.textColor = kAccentBlue;
            SenkoStyleSelectableCell(cell);
        }
        return cell;
    }

    if (ip.section == 1) {
        if (ip.row == 0) {
            cell.textLabel.text = SenkoLocalizedText(@"Device id");
            cell.detailTextLabel.text = _hwid ? SenkoMaskSecret(_hwid) : @"-";
            if (_hwid) {
                cell.textLabel.textColor = kAccentBlue;
                SenkoStyleSelectableCell(cell);
            }
        } else {
            cell.textLabel.text = SenkoLocalizedText(@"New device id");
            cell.textLabel.textColor = kAccentBlue;
            SenkoStyleSelectableCell(cell);
        }
        return cell;
    }

    if (ip.row == 0) {
        cell.textLabel.text = SenkoLocalizedText(@"Delete all rules");
        cell.textLabel.textColor = kAccentBlue;
    } else if (ip.row == 1) {
        cell.textLabel.text = SenkoLocalizedText(@"Export debug bundle");
        cell.detailTextLabel.text = @"Documents/senko-diagnostics.txt";
        cell.textLabel.textColor = kAccentBlue;
    } else {
        cell.textLabel.text = SenkoLocalizedText(@"Test fixtures");
        cell.detailTextLabel.text = SenkoLocalizedText(@"Long names, duplicates and an empty manual list");
        cell.textLabel.textColor = kAccentBlue;
    }
    SenkoStyleSelectableCell(cell);
    return cell;
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    if (ip.section == 0) {
        if (ip.row == 0) {
            DevTextVC *vc = [[[DevTextVC alloc]
                initWithTitle:SenkoLocalizedText(@"Crash and launch report")
                         body:[self crashReportText]] autorelease];
            [self.navigationController pushViewController:vc animated:YES];
        } else if (ip.row == 2) {
            if (SenkoCrashSafeMode()) {
                SenkoCrashClearSafeMode();
                [_tv reloadData];
                [self devSay:SenkoLocalizedText(@"Rescue")
                     message:SenkoLocalizedText(@"Off from the next launch.")];
                return;
            }
            [self devConfirm:SenkoLocalizedText(@"Safe mode next launch")
                     message:SenkoLocalizedText(@"Stock theme, no glass, no decor. Your theme stays on disk.")
                      button:SenkoLocalizedText(@"Enter")
                         tag:DEV_CONFIRM_SAFE_MODE];
        }
        return;
    }
    if (ip.section == 1) {
        if (ip.row == 0) {
            if (!_hwid) return;
/* the id is masked on screen and whole on the clipboard: a panel operator
   needs the real value, a screenshot does not */
            [[UIPasteboard generalPasteboard] setString:_hwid];
            [self devSay:SenkoLocalizedText(@"Device id")
                 message:SenkoLocalizedText(@"Copied in full.")];
            return;
        }
        [self devConfirm:SenkoLocalizedText(@"New device id")
                 message:SenkoLocalizedText(@"The old id is gone for good.")
                  button:SenkoLocalizedText(@"Issue")
                     tag:DEV_CONFIRM_HWID_RESET];
        return;
    }
    if (ip.row == 0) {
        [self devConfirm:SenkoLocalizedText(@"Delete all rules")
                 message:SenkoLocalizedText(@"Catalog is kept.")
                  button:SenkoLocalizedText(@"Delete")
                     tag:DEV_CONFIRM_FLUSH_RULES];
        return;
    }
    if (ip.row == 1) {
        [self writeBundle];
        return;
    }
    UIActionSheet *sheet = [[[UIActionSheet alloc]
        initWithTitle:SenkoLocalizedText(@"Test fixtures")
              delegate:self cancelButtonTitle:SenkoLocalizedText(@"Cancel")
        destructiveButtonTitle:nil otherButtonTitles:
        SenkoLocalizedText(@"Long names"), SenkoLocalizedText(@"Duplicates"),
        SenkoLocalizedText(@"Empty manual list"), nil] autorelease];
    sheet.tag = DEV_SHEET_FIXTURES;
    [sheet showInView:self.view];
}

- (void)addFixtureLinks:(NSArray *)links at:(NSUInteger)index {
    if (index >= links.count) {
        [self devSay:SenkoLocalizedText(@"Test fixtures") message:SenkoLocalizedText(@"Done.")];
        return;
    }
    [_ctl addServerLink:[links objectAtIndex:index] reply:^(NSString *reply) {
        if (![reply hasPrefix:@"OK "]) {
            [self devSay:SenkoLocalizedText(@"Test fixtures") message:SenkoHumanReadableError(reply)];
            return;
        }
        [self addFixtureLinks:links at:index + 1];
    }];
}

- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)index {
    if (sheet.tag != DEV_SHEET_FIXTURES || index == sheet.cancelButtonIndex) return;
    if (index == 2) {
        [self devConfirm:SenkoLocalizedText(@"Empty manual list")
                 message:SenkoLocalizedText(@"Only manually added servers are removed.")
                  button:SenkoLocalizedText(@"Clear") tag:DEV_CONFIRM_TEST_EMPTY];
        return;
    }
    NSString *longName = @"vless://00000000-0000-4000-8000-000000000001@test-long-name-for-layout.example:443?security=tls#Finlandia-with-a-ridiculously-long-test-name-from-femboys";
    NSString *duplicate = @"trojan://fixture-password@test-duplicate.example:443?security=tls#Duplicate-fixture";
    NSArray *links = index == 0
        ? [NSArray arrayWithObjects:longName, [longName stringByAppendingString:@"-two"], nil]
        : [NSArray arrayWithObjects:duplicate, duplicate, duplicate, nil];
    [self addFixtureLinks:links at:0];
}

- (void)devConfirmed:(NSInteger)tag {
    if (tag == DEV_CONFIRM_SAFE_MODE) {
        SenkoCrashEnterSafeMode();
        [_tv reloadData];
        [self devSay:SenkoLocalizedText(@"Rescue")
             message:SenkoLocalizedText(@"The next launch runs in safe mode.")];
        return;
    }
    if (tag == DEV_CONFIRM_HWID_RESET) {
        [_ctl resetDeviceHWID:^(NSString *hwid, NSString *error) {
            if (hwid) {
                [_hwid release];
                _hwid = [hwid copy];
                [_tv reloadData];
            }
            [self devSay:SenkoLocalizedText(@"Device id")
                 message:hwid ? SenkoMaskSecret(hwid) : SenkoHumanReadableError(error)];
        }];
        return;
    }
    if (tag == DEV_CONFIRM_FLUSH_RULES) {
        [_ctl flushTarget:@"rules" reply:^(NSString *reply) {
            [self devSay:SenkoLocalizedText(@"Rescue")
                 message:[reply hasPrefix:@"OK "]
                             ? [reply substringFromIndex:3]
                             : SenkoHumanReadableError(reply)];
        }];
        return;
    }
    if (tag == DEV_CONFIRM_TEST_EMPTY) {
        [_ctl clearManualServers:^(NSString *reply) {
            [self devSay:SenkoLocalizedText(@"Empty manual list")
                 message:[reply hasPrefix:@"OK "] ? SenkoLocalizedText(@"Done.") : SenkoHumanReadableError(reply)];
        }];
    }
}

/* one file instead of a screenshot: the diagnostics report, the app's own
   launch record and the daemon log tail, with secrets stripped from all three */
- (void)writeBundle {
    [_ctl daemonDiagnostics:^(NSArray *facts) {
        [_ctl daemonLogTail:^(NSString *log) {
            NSMutableString *text = [NSMutableString string];
            [text appendFormat:@"senko %@\n%@\n\n", SENKO_VERSION,
                               [[NSDate date] description]];
            [text appendString:@"[diagnostics]\n"];
            if ([facts count]) {
                for (SenkoDiagFact *fact in facts)
                    [text appendFormat:@"%@ = %@\n", fact->key, fact->value];
            } else {
                [text appendString:SenkoLocalizedText(@"the daemon did not answer")];
                [text appendString:@"\n"];
            }
            [text appendFormat:@"\n[device]\n%@ %@ %@\n",
                               [[UIDevice currentDevice] model],
                               [[UIDevice currentDevice] systemName],
                               [[UIDevice currentDevice] systemVersion]];
            [text appendFormat:@"hwid = %@\n", _hwid ? SenkoMaskSecret(_hwid) : @"-"];
            [text appendFormat:@"failed launches = %d\nsafe mode = %@\n",
                               SenkoCrashFailedLaunches(),
                               SenkoCrashSafeMode() ? @"yes" : @"no"];
            [text appendFormat:@"\n[app faults]\n%@\n", [self crashReportText]];
            [text appendFormat:@"\n[daemon log]\n%@\n", [log length] ? log : @"-"];

            NSString *safe = SenkoRedactSecrets(text);
            NSString *documents = [NSSearchPathForDirectoriesInDomains(
                NSDocumentDirectory, NSUserDomainMask, YES) lastObject];
            NSString *path = [documents stringByAppendingPathComponent:
                              @"senko-diagnostics.txt"];
            NSError *err = nil;
            BOOL ok = [safe writeToFile:path atomically:YES
                               encoding:NSUTF8StringEncoding error:&err];
            [self devSay:SenkoLocalizedText(@"Diagnostics bundle")
                 message:ok ? path : [err localizedDescription]];
            if (ok) {
                Class shareClass = NSClassFromString(@"UIActivityViewController");
                SEL initSel = NSSelectorFromString(@"initWithActivityItems:applicationActivities:");
                SEL presentSel = NSSelectorFromString(@"presentViewController:animated:completion:");
                if (shareClass && [shareClass instancesRespondToSelector:initSel] &&
                    [self respondsToSelector:presentSel]) {
                    NSArray *items = [NSArray arrayWithObject:[NSURL fileURLWithPath:path]];
                    id share = [shareClass alloc];
                    share = ((id (*)(id, SEL, id, id))objc_msgSend)(
                        share, initSel, items, nil);
                    if (share) {
                        ((void (*)(id, SEL, id, BOOL, id))objc_msgSend)(
                            self, presentSel, share, YES, nil);
                        [share release];
                    }
                }
            }
        }];
    }];
}

@end
