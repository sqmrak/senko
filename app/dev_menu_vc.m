#import "dev_menu_vc.h"

#import "dev_checks_vc.h"
#import "dev_console_vc.h"
#import "dev_tools_vc.h"
#import "control_client.h"
#import "ui_theme.h"
#import "app_common.h"

#include <mach/mach.h>
#include <sys/sysctl.h>

/* a title for the keys this build knows. an unknown key is shown as it came
   over the wire, so an app older than the daemon still reports the new fact
   instead of hiding it */
static NSString *DiagTitle(NSString *key) {
    static NSDictionary *titles;
    if (!titles) {
        titles = [[NSDictionary alloc] initWithObjectsAndKeys:
            @"Tunnel state",            @"state",
            @"Connected for",           @"state.uptime",
            @"Redial",                  @"redial",
            @"Egress interface",        @"egress",
            @"Egress address",          @"egress.address",
            @"Catalog",                 @"catalog",
            @"Selected server",         @"server",
            @"Transport",               @"server.transport",
            @"Flow",                    @"server.flow",
            @"Vision",                  @"server.vision",
            @"Daemon pid",              @"daemon.pid",
            @"Daemon uptime",           @"daemon.uptime",
            @"Jailbreak",               @"daemon.jailbreak",
            @"Config file",             @"daemon.config",
            @"iOS major",               @"ios.major",
            @"iOS read from",          @"ios.source",
            @"Backend",                @"backend",
            @"Go core usable",         @"backend.go_supported",
            @"Backend forced to",       @"backend.pinned",
            @"Last backend error",      @"backend.last_error",
            @"Firewall",                @"firewall",
            @"pf variant",             @"firewall.pf_mode",
            @"pf variant forced to",    @"firewall.pf_pinned",
            @"pf rejected",            @"firewall.last_reject",
            @"Bypass table",            @"firewall.bypass_table",
            @"Bypass evicted",         @"firewall.bypass_evicted",
            @"Redirect port",           @"port.redirect",
            @"DNS port",                @"port.dns",
            @"SOCKS port",              @"port.socks",
            @"SOCKS bound to",          @"socks.bind",
            @"Live connections",        @"conns",
            @"Upstream DNS",            @"dns.upstream",
            @"Block response",         @"dns.block_response",
            @"DNS cache",               @"dns.cache",
            @"Top rule",               @"rules.top1",
            @"Rule 2",                 @"rules.top2",
            @"Rule 3",                 @"rules.top3",
            @"Rules",                  @"rules.top",
            @"Device gating",           @"sub.gating",
            @"Session trace",           @"trace",
            @"Jailbreak root",          @"path.jbroot",
            @"Device id file",          @"path.hwid",
            @"System log",              @"path.log",
            @"Substrate directory",     @"path.substrate",
            @"senkoawgd",               @"proc.senkoawgd",
            @"senko-kick",              @"proc.senko_kick",
            @"senkotlsfix",            @"substrate.tlsfix",
            @"senkostatus",           @"substrate.status",
            @"Free memory",            @"device.memory_free",
            @"Senko resident",         @"device.app_memory",
            @"Device uptime",          @"device.uptime",
            @"Device",                 @"device.model",
            @"Battery",                @"device.battery",
            nil];
    }
    NSString *title = [titles objectForKey:key];
    return title ? SenkoLocalizedText(title) : key;
}

/* the wire order is one flat list, which is unreadable at forty lines. the
   grouping is by meaning: choices first, then the numbers that move */
static NSString *DiagGroup(NSString *key) {
    if ([key hasPrefix:@"state"] || [key isEqualToString:@"redial"] ||
        [key hasPrefix:@"egress"] || [key isEqualToString:@"conns"] ||
        [key isEqualToString:@"catalog"] ||
        [key hasPrefix:@"dns.cache"] || [key hasPrefix:@"firewall.bypass"] ||
        [key hasPrefix:@"rules.top"])
        return @"LIVE";
    if ([key hasPrefix:@"server"]) return @"SERVER";
    if ([key isEqualToString:@"backend.pinned"] ||
        [key isEqualToString:@"firewall.pf_pinned"] ||
        [key hasPrefix:@"sub."] || [key isEqualToString:@"trace"] ||
        [key isEqualToString:@"socks.bind"])
        return @"FORCED";
    if ([key hasPrefix:@"path."]) return @"PATHS";
    if ([key hasPrefix:@"device."]) return @"DEVICE";
    if ([key hasPrefix:@"proc."] || [key hasPrefix:@"substrate."])
        return @"PROCESSES";
    return @"CHOSEN";
}

static NSArray *DiagGroupOrder(void) {
    return [NSArray arrayWithObjects:
            @"CHOSEN", @"SERVER", @"LIVE", @"DEVICE",
            @"FORCED", @"PROCESSES", @"PATHS", nil];
}

@implementation DevMenuVC

- (NSString *)devTitle {
    return SenkoLocalizedText(@"Developer");
}

- (const char *)devScreenName {
    return "devmenu";
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return 1;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 5;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    UITableViewCell *cell = [self devCellForTable:tv indexPath:ip rows:5];
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    SenkoStyleSelectableCell(cell);
    if (ip.row == 0) {
        cell.textLabel.text = SenkoLocalizedText(@"State");
        cell.detailTextLabel.text = SenkoLocalizedText(@"Backend, firewall, ports, DNS, rules");
    } else if (ip.row == 1) {
        cell.textLabel.text = SenkoLocalizedText(@"Checks");
        cell.detailTextLabel.text = SenkoLocalizedText(@"Staged probes and the firewall ruleset");
    } else if (ip.row == 2) {
        cell.textLabel.text = SenkoLocalizedText(@"Force");
        cell.detailTextLabel.text = SenkoLocalizedText(@"Backend, pf variant, listeners, trace");
    } else if (ip.row == 3) {
        cell.textLabel.text = SenkoLocalizedText(@"Rescue");
        cell.detailTextLabel.text = SenkoLocalizedText(@"Crash log, safe mode, device id, bundle");
    } else {
        cell.textLabel.text = SenkoLocalizedText(@"Console");
        cell.detailTextLabel.text = SenkoLocalizedText(@"Talk to the control socket without ssh");
    }
    return cell;
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    UIViewController *next = nil;
    if (ip.row == 0) next = [[[DevFactsVC alloc] initWithControl:_ctl] autorelease];
    else if (ip.row == 1) next = [[[DevChecksVC alloc] initWithControl:_ctl] autorelease];
    else if (ip.row == 2) next = [[[DevTogglesVC alloc] initWithControl:_ctl] autorelease];
    else if (ip.row == 3) next = [[[DevRescueVC alloc] initWithControl:_ctl] autorelease];
    else next = [[[DevConsoleVC alloc] initWithControl:_ctl] autorelease];
    [self.navigationController pushViewController:next animated:YES];
}

@end

/* what the daemon cannot see: this process and this handset. an iphone 4s has
   half a gigabyte, and the live glass themes are the reason to watch it */
static NSArray *DeviceFacts(void) {
    NSMutableArray *facts = [NSMutableArray array];
    mach_port_t host = mach_host_self();
    vm_size_t page = 0;
    vm_statistics_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    struct task_basic_info task;
    mach_msg_type_number_t task_count = TASK_BASIC_INFO_COUNT;

    if (host_page_size(host, &page) == KERN_SUCCESS &&
        host_statistics(host, HOST_VM_INFO, (host_info_t)&vm, &count) == KERN_SUCCESS) {
        unsigned long long freeBytes =
            (unsigned long long)(vm.free_count + vm.inactive_count) * page;
        SenkoDiagFact *fact = [[[SenkoDiagFact alloc] init] autorelease];
        fact->key = [@"device.memory_free" copy];
        fact->value = [[NSString stringWithFormat:@"%llu MB",
                        freeBytes / (1024ull * 1024ull)] copy];
        [facts addObject:fact];
    }
    if (task_info(mach_task_self(), TASK_BASIC_INFO,
                  (task_info_t)&task, &task_count) == KERN_SUCCESS) {
        SenkoDiagFact *fact = [[[SenkoDiagFact alloc] init] autorelease];
        fact->key = [@"device.app_memory" copy];
        fact->value = [[NSString stringWithFormat:@"%llu MB",
                        (unsigned long long)task.resident_size / (1024ull * 1024ull)] copy];
        [facts addObject:fact];
    }
    {
        struct timeval boot;
        size_t size = sizeof boot;
        int mib[2] = { CTL_KERN, KERN_BOOTTIME };
        if (sysctl(mib, 2, &boot, &size, NULL, 0) == 0 && boot.tv_sec) {
            long up = (long)(time(NULL) - boot.tv_sec);
            SenkoDiagFact *fact = [[[SenkoDiagFact alloc] init] autorelease];
            fact->key = [@"device.uptime" copy];
            fact->value = [[NSString stringWithFormat:@"%ldh %ldm",
                            up / 3600, (up % 3600) / 60] copy];
            [facts addObject:fact];
        }
    }
    {
        UIDevice *device = [UIDevice currentDevice];
        SenkoDiagFact *fact = [[[SenkoDiagFact alloc] init] autorelease];
        fact->key = [@"device.model" copy];
        fact->value = [[NSString stringWithFormat:@"%@ %@", device.model,
                        device.systemVersion] copy];
        [facts addObject:fact];
        device.batteryMonitoringEnabled = YES;
        if (device.batteryLevel >= 0.0f) {
            SenkoDiagFact *power = [[[SenkoDiagFact alloc] init] autorelease];
            power->key = [@"device.battery" copy];
            power->value = [[NSString stringWithFormat:@"%d%%%@",
                             (int)(device.batteryLevel * 100.0f),
                             device.batteryState == UIDeviceBatteryStateCharging
                                 ? @", charging" : @""] copy];
            [facts addObject:power];
        }
    }
    return facts;
}

@implementation DevFactsVC

- (void)dealloc {
    [_refresh invalidate];
    [_refresh release];
    [_groups release];
    [super dealloc];
}

- (NSString *)devTitle {
    return SenkoLocalizedText(@"State");
}

- (const char *)devScreenName {
    return "devfacts";
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithTitle:SenkoLocalizedText(@"Copy")
                                          style:UIBarButtonItemStylePlain
                                         target:self
                                         action:@selector(copyReport)] autorelease];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self reload];
/* the connection count and the cache numbers move while the screen is open,
   and a report that has to be left and reopened to change is a report nobody
   trusts */
    [_refresh invalidate];
    [_refresh release];
    _refresh = [[NSTimer scheduledTimerWithTimeInterval:3.0
                                                 target:self
                                               selector:@selector(reload)
                                               userInfo:nil
                                                repeats:YES] retain];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [_refresh invalidate];
    [_refresh release];
    _refresh = nil;
}

/* one pass over the wire order keeps each group in the order the daemon wrote
   it, which is the order it tried things in */
- (void)rebuildGroupsFrom:(NSArray *)facts {
    NSMutableArray *order = [NSMutableArray array];
    NSMutableDictionary *buckets = [NSMutableDictionary dictionary];
    for (NSString *name in DiagGroupOrder()) {
        [order addObject:name];
        [buckets setObject:[NSMutableArray array] forKey:name];
    }
    NSMutableArray *all = [NSMutableArray arrayWithArray:facts ? facts : [NSArray array]];
    [all addObjectsFromArray:DeviceFacts()];
    for (SenkoDiagFact *fact in all) {
        NSString *name = DiagGroup(fact->key);
        NSMutableArray *bucket = [buckets objectForKey:name];
        if (!bucket) {
            bucket = [NSMutableArray array];
            [buckets setObject:bucket forKey:name];
            [order addObject:name];
        }
        [bucket addObject:fact];
    }
    NSMutableArray *groups = [NSMutableArray array];
    for (NSString *name in order) {
        NSArray *bucket = [buckets objectForKey:name];
        if (![bucket count]) continue;
        [groups addObject:[NSArray arrayWithObjects:name, bucket, nil]];
    }
    [_groups release];
    _groups = [groups copy];
}

- (void)reload {
    [_ctl daemonDiagnostics:^(NSArray *facts) {
        [self rebuildGroupsFrom:facts];
        _loaded = YES;
        [_tv reloadData];
    }];
}

- (NSArray *)factsInSection:(NSInteger)section {
    if (section < 0 || section >= (NSInteger)[_groups count]) return nil;
    return [[_groups objectAtIndex:section] objectAtIndex:1];
}

/* the report is meant to leave the device, so the copy is the raw key value
   text rather than the localized titles */
- (void)copyReport {
    [[UIPasteboard generalPasteboard] setString:[self reportText]];
    [self devSay:SenkoLocalizedText(@"State")
         message:SenkoLocalizedText(@"Copied to the clipboard.")];
}

- (NSString *)reportText {
    NSMutableString *text = [NSMutableString string];
    [text appendFormat:@"senko %@\n", SENKO_VERSION];
    for (NSArray *group in _groups) {
        [text appendFormat:@"\n[%@]\n", [group objectAtIndex:0]];
        for (SenkoDiagFact *fact in [group objectAtIndex:1])
            [text appendFormat:@"%@ = %@\n", fact->key, fact->value];
    }
    return SenkoRedactSecrets(text);
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return MAX((NSInteger)[_groups count], 1);
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    return (NSInteger)[[self factsInSection:s] count];
}

- (NSString *)devHeaderForSection:(NSInteger)s {
    if (s >= (NSInteger)[_groups count]) return nil;
    return SenkoLocalizedText([[_groups objectAtIndex:s] objectAtIndex:0]);
}

- (NSString *)devFooterForSection:(NSInteger)s {
    if (s + 1 != (NSInteger)[self numberOfSectionsInTableView:_tv]) return nil;
    if (!_loaded) return SenkoLocalizedText(@"Asking the daemon...");
    if (![_groups count]) return SenkoLocalizedText(@"Daemon is unreachable");
    return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    NSArray *facts = [self factsInSection:ip.section];
    UITableViewCell *cell = [self devCellForTable:tv indexPath:ip
                                             rows:(NSInteger)[facts count]];
    if (ip.row >= (NSInteger)[facts count]) return cell;
    SenkoDiagFact *fact = [facts objectAtIndex:ip.row];
    cell.textLabel.font = [UIFont systemFontOfSize:15.0f];
    cell.textLabel.textColor = kInkMuted;
    cell.textLabel.text = DiagTitle(fact->key);
/* every value here is machine text, so it is set in the face that makes a port
   number line up under a port number */
    cell.detailTextLabel.font = [UIFont fontWithName:@"Courier-Bold" size:13.0f]
        ?: [UIFont boldSystemFontOfSize:13.0f];
    cell.detailTextLabel.textColor = kInk;
    cell.detailTextLabel.text = fact->value;
    return cell;
}

@end
