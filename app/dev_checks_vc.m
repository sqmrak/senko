#import "dev_checks_vc.h"

#import "control_client.h"
#import "ui_theme.h"
#import "app_common.h"

#define DEV_CHECK_SHEET_MODE   4401
#define DEV_CHECK_SHEET_SERVER 4402

/* the daemon's own words, kept in one place so a renamed button cannot start
   sending a mode the check parser does not know */
static NSString *const kCheckModes[] = { @"tcp", @"proxy", @"tunnel", @"handshake" };
#define DEV_CHECK_MODE_COUNT 4

static NSString *CheckModeTitle(NSString *mode) {
    if ([mode isEqualToString:@"proxy"]) return SenkoLocalizedText(@"Local proxy");
    if ([mode isEqualToString:@"tunnel"]) return SenkoLocalizedText(@"Active tunnel");
    if ([mode isEqualToString:@"handshake"]) return SenkoLocalizedText(@"Profile handshake");
    return SenkoLocalizedText(@"TCP to the node");
}

@implementation DevChecksVC

- (id)initWithControl:(SenkoControl *)control {
    if ((self = [super initWithControl:control])) {
        _mode = [@"tcp" copy];
        _serverIndex = -1;
    }
    return self;
}

- (void)dealloc {
    [_servers release];
    [_stages release];
    [_mode release];
    [_result release];
    [super dealloc];
}

- (NSString *)devTitle {
    return SenkoLocalizedText(@"Checks");
}

- (const char *)devScreenName {
    return "devchecks";
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [_ctl listServers:^(NSArray *servers) {
        [_servers release];
        _servers = [servers retain];
        if (_serverIndex < 0) {
            for (SenkoServer *server in _servers)
                if (server->selected) { _serverIndex = server->index; break; }
        }
        if (_serverIndex < 0 && [_servers count])
            _serverIndex = ((SenkoServer *)[_servers objectAtIndex:0])->index;
        [_tv reloadData];
    }];
}

- (SenkoServer *)selectedServer {
    for (SenkoServer *server in _servers)
        if (server->index == _serverIndex) return server;
    return nil;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return [_stages count] || [_result length] ? 3 : 2;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    if (s == 0) return 3;
    if (s == 1) return 1;
    return (NSInteger)[_stages count] + ([_result length] ? 1 : 0);
}

- (NSString *)devHeaderForSection:(NSInteger)s {
    if (s == 0) return SenkoLocalizedText(@"CHECK");
    if (s == 1) return SenkoLocalizedText(@"FIREWALL");
    return SenkoLocalizedText(@"STAGES");
}

- (NSString *)devFooterForSection:(NSInteger)s {
    (void)s;
    return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    NSInteger rows = [self tableView:tv numberOfRowsInSection:ip.section];
    UITableViewCell *cell = [self devCellForTable:tv indexPath:ip rows:rows];

    if (ip.section == 0) {
        if (ip.row == 0) {
            SenkoServer *server = [self selectedServer];
            cell.textLabel.text = SenkoLocalizedText(@"Server");
            cell.detailTextLabel.text = server
                ? [NSString stringWithFormat:@"#%d %@:%d", server->index,
                                             server->host, server->port]
                : SenkoLocalizedText(@"No server in the catalog");
            if (server) {
                cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
                SenkoStyleSelectableCell(cell);
            }
        } else if (ip.row == 1) {
            cell.textLabel.text = SenkoLocalizedText(@"Mode");
            cell.detailTextLabel.text = CheckModeTitle(_mode);
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            SenkoStyleSelectableCell(cell);
        } else {
            cell.textLabel.text = _running
                ? SenkoLocalizedText(@"Running...")
                : SenkoLocalizedText(@"Run");
            cell.textLabel.textColor = _running ? kInkMuted : kAccentBlue;
            if (!_running) SenkoStyleSelectableCell(cell);
        }
        return cell;
    }

    if (ip.section == 1) {
        cell.textLabel.text = SenkoLocalizedText(@"Show ruleset");
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        SenkoStyleSelectableCell(cell);
        return cell;
    }

    if (ip.row < (NSInteger)[_stages count]) {
        SenkoCheckStage *stage = [_stages objectAtIndex:ip.row];
        cell.textLabel.text = [NSString stringWithFormat:@"%@ %@",
                               stage->ok ? @"OK" : @"X", stage->name];
        cell.textLabel.textColor = stage->ok ? kInk : kConnOn;
        cell.detailTextLabel.text = [NSString stringWithFormat:@"%d ms", stage->ms];
        if (!stage->ok) cell.textLabel.textColor = [UIColor colorWithRed:0.85f green:0.32f blue:0.28f alpha:1.0f];
        return cell;
    }

    cell.textLabel.text = SenkoLocalizedText(@"Result");
    cell.detailTextLabel.text = _result;
    cell.detailTextLabel.numberOfLines = 3;
    return cell;
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    if (ip.section == 1) { [self showFirewallConfig]; return; }
    if (ip.section != 0) return;
    if (ip.row == 0) [self showServerPicker];
    else if (ip.row == 1) [self showModePicker];
    else [self runCheck];
}

/* a sheet rather than an alert: an alert with nine buttons runs off a 3.5 inch
   screen, which is the screen this section exists for */
- (UIActionSheet *)sheetWithTitle:(NSString *)title tag:(NSInteger)tag {
    UIActionSheet *sheet = [[[UIActionSheet alloc]
             initWithTitle:title
                  delegate:self
         cancelButtonTitle:nil
    destructiveButtonTitle:nil
         otherButtonTitles:nil] autorelease];
    sheet.tag = tag;
    return sheet;
}

- (void)showSheet:(UIActionSheet *)sheet {
    sheet.cancelButtonIndex = [sheet addButtonWithTitle:SenkoLocalizedText(@"Cancel")];
    [sheet showInView:self.view];
}

- (void)showModePicker {
    UIActionSheet *sheet = [self sheetWithTitle:SenkoLocalizedText(@"Mode")
                                            tag:DEV_CHECK_SHEET_MODE];
    for (int i = 0; i < DEV_CHECK_MODE_COUNT; ++i)
        [sheet addButtonWithTitle:CheckModeTitle(kCheckModes[i])];
    [self showSheet:sheet];
}

/* a sheet still has to fit, so a long catalog is picked from the server list
   and only the head of it is offered here */
#define DEV_CHECK_PICK_MAX 8

- (void)showServerPicker {
    if (![_servers count]) return;
    UIActionSheet *sheet = [self sheetWithTitle:SenkoLocalizedText(@"Server")
                                            tag:DEV_CHECK_SHEET_SERVER];
    NSUInteger shown = MIN((NSUInteger)DEV_CHECK_PICK_MAX, [_servers count]);
    for (NSUInteger i = 0; i < shown; ++i) {
        SenkoServer *server = [_servers objectAtIndex:i];
        [sheet addButtonWithTitle:[NSString stringWithFormat:@"#%d %@",
                                   server->index, [server->remark length]
                                       ? server->remark : server->host]];
    }
    [self showSheet:sheet];
}

- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)index {
    if (index == sheet.cancelButtonIndex || index < 0) return;
    if (sheet.tag == DEV_CHECK_SHEET_MODE) {
        if (index >= DEV_CHECK_MODE_COUNT) return;
        [_mode release];
        _mode = [kCheckModes[index] copy];
        [_tv reloadData];
        return;
    }
    if (sheet.tag == DEV_CHECK_SHEET_SERVER) {
        if (index >= (NSInteger)[_servers count]) return;
        _serverIndex = ((SenkoServer *)[_servers objectAtIndex:index])->index;
        [_tv reloadData];
    }
}

- (void)runCheck {
    if (_running) return;
    SenkoServer *server = [self selectedServer];
    if (!server) {
        [self devSay:SenkoLocalizedText(@"Checks")
             message:SenkoLocalizedText(@"No servers in the catalog.")];
        return;
    }
    _running = YES;
    [_stages release];
    _stages = nil;
    [_result release];
    _result = nil;
    [_tv reloadData];
    [_ctl checkIndex:_serverIndex mode:_mode
              stages:^(NSArray *stages, int ms, NSString *error) {
        _running = NO;
        [_stages release];
        _stages = [stages retain];
        [_result release];
        _result = error
            ? [SenkoHumanReadableError(error) copy]
            : [[NSString stringWithFormat:SenkoLocalizedText(@"passed in %d ms"), ms] copy];
        [_tv reloadData];
    }];
}

- (void)showFirewallConfig {
    [_ctl firewallConfig:^(NSString *text, NSString *error) {
        if (![text length]) {
            [self devSay:SenkoLocalizedText(@"Generated firewall ruleset")
                 message:SenkoHumanReadableError(error)];
            return;
        }
        DevTextVC *vc = [[[DevTextVC alloc]
            initWithTitle:SenkoLocalizedText(@"Firewall ruleset")
                     body:text] autorelease];
        [self.navigationController pushViewController:vc animated:YES];
    }];
}

@end
