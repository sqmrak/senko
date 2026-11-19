#import "dev_checks_vc.h"

#import "control_client.h"
#import "ui_theme.h"
#import "app_common.h"


/* the daemon's own words, kept in one place so a renamed button cannot start
   sending a mode the check parser does not know */
static NSString *const kCheckModes[] = { @"tcp", @"real", @"proxy", @"tunnel", @"handshake" };
#define DEV_CHECK_MODE_COUNT 5

static NSString *CheckModeTitle(NSString *mode) {
    if ([mode isEqualToString:@"proxy"]) return SenkoLocalizedText(@"Local proxy");
    if ([mode isEqualToString:@"tunnel"]) return SenkoLocalizedText(@"Active tunnel");
    if ([mode isEqualToString:@"handshake"]) return SenkoLocalizedText(@"Profile handshake");
    if ([mode isEqualToString:@"real"]) return SenkoLocalizedText(@"Real delay");
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
        cell.textLabel.text = SenkoLocalizedText(@"Show tunnel routes");
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
    if (ip.section == 1) { [self showTunnelRoutes]; return; }
    if (ip.section != 0) return;
    if (ip.row == 0) [self showServerPicker];
    else if (ip.row == 1) [self showModePicker];
    else [self runCheck];
}

/* a list rather than buttons: nine buttons run off a 3.5 inch screen, which is
   the screen this section exists for, and the card scrolls its own list */
- (void)showModePicker {
    NSMutableArray *labels = [NSMutableArray array];
    for (int i = 0; i < DEV_CHECK_MODE_COUNT; ++i)
        [labels addObject:CheckModeTitle(kCheckModes[i])];
    SenkoDialogMenu(SenkoLocalizedText(@"Mode"), nil, labels, ^(NSInteger index) {
        if (index < 0 || index >= DEV_CHECK_MODE_COUNT) return;
        [_mode release];
        _mode = [kCheckModes[index] copy];
        [_tv reloadData];
    });
}

/* a long catalog is picked from the server list and only the head of it is
   offered here */
#define DEV_CHECK_PICK_MAX 8

- (void)showServerPicker {
    if (![_servers count]) return;
    NSUInteger shown = MIN((NSUInteger)DEV_CHECK_PICK_MAX, [_servers count]);
    NSMutableArray *labels = [NSMutableArray array];
    for (NSUInteger i = 0; i < shown; ++i) {
        SenkoServer *server = [_servers objectAtIndex:i];
        [labels addObject:[NSString stringWithFormat:@"#%d %@",
                           server->index, [server->remark length]
                               ? server->remark : server->host]];
    }
    SenkoDialogMenu(SenkoLocalizedText(@"Server"), nil, labels, ^(NSInteger index) {
        if (index < 0 || index >= (NSInteger)[_servers count]) return;
        _serverIndex = ((SenkoServer *)[_servers objectAtIndex:index])->index;
        [_tv reloadData];
    });
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

- (void)showTunnelRoutes {
    [_ctl tunnelRoutes:^(NSString *text, NSString *error) {
        if (![text length]) {
            [self devSay:SenkoLocalizedText(@"Tunnel routes")
                 message:SenkoHumanReadableError(error)];
            return;
        }
        DevTextVC *vc = [[[DevTextVC alloc]
            initWithTitle:SenkoLocalizedText(@"Tunnel routes")
                     body:text] autorelease];
        [self.navigationController pushViewController:vc animated:YES];
    }];
}

@end
