#import "routing_vc.h"

#import "app_catalog.h"
#import "app_common.h"
#import "app_routing_vc.h"
#import "control_client.h"
#import "crash_report.h"
#import "profile_entries_vc.h"
#import "routing_ui.h"
#import "rules_vc.h"
#import "ui_theme.h"

#include <objc/message.h>

typedef enum {
    RoutingSectionProfile = 0,
    RoutingSectionGeo,
    RoutingSectionTraffic,
    RoutingSectionCount
} RoutingSection;

#define ICON_STRIP_MAX 4

@implementation RoutingVC

- (id)initWithControl:(SenkoControl *)control {
    if ((self = [super init])) {
        _ctl = [control retain];
        _active = -1;
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_geoTimer invalidate];
    _geoTimer = nil;
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    [_ctl release];
    [_profiles release];
    [_geo release];
    [_rules release];
    [_notice release];
    [super dealloc];
}

/* ios 5 releases the view of an offscreen controller, so the retained table
   must go with it instead of pointing into a freed hierarchy */
- (void)viewDidUnload {
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:SenkoThemeDidChangeNotification
                                                  object:nil];
    [_geoTimer invalidate];
    _geoTimer = nil;
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    _tv = nil;
    [super viewDidUnload];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"Routing");
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);
    SenkoApplyScreenChrome(self.view);
    _tv = [[UITableView alloc] initWithFrame:self.view.bounds style:UITableViewStyleGrouped];
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    SenkoClearTableBackground(_tv);
    _tv.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    if ([_tv respondsToSelector:@selector(setBackgroundView:)]) _tv.backgroundView = nil;
    [self.view addSubview:_tv];
    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("routing");
    [super viewWillAppear:animated];
    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    [self reload];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [_geoTimer invalidate];
    _geoTimer = nil;
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    SenkoApplyScreenChrome(self.view);
    SenkoClearTableBackground(_tv);
    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

#pragma mark data

- (void)reload {
    [_ctl listRoutingProfiles:^(NSArray *profiles, int active) {
        [_profiles release];
        _profiles = [profiles retain];
        _profilesLoaded = profiles != nil;
        _active = active;
        [_tv reloadData];
        [self reloadGeo];
    }];
    [_ctl listRules:^(NSArray *rules) {
        [_rules release];
        _rules = [rules retain];
        [_tv reloadData];
    }];
    /* the apps are scanned for their icons, which the apps row shows */
    [self retain];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        (void)[SenkoAppCatalog installedApps];
        dispatch_async(dispatch_get_main_queue(), ^{
            [_tv reloadData];
            [self release];
        });
    });
}

- (void)reloadGeo {
    [_ctl geoState:^(SenkoGeoState *state) {
        [_geo release];
        _geo = [state retain];
        [_tv reloadData];
        /* a running download is polled until it ends, and not once it has */
        if (state && state->busy && !_geoTimer) {
            _geoTimer = [NSTimer scheduledTimerWithTimeInterval:2.0
                                                         target:self
                                                       selector:@selector(geoTick:)
                                                       userInfo:nil
                                                        repeats:YES];
        } else if ((!state || !state->busy) && _geoTimer) {
            [_geoTimer invalidate];
            _geoTimer = nil;
        }
    }];
}

- (void)geoTick:(NSTimer *)timer {
    (void)timer;
    [self reloadGeo];
}

- (SenkoRoutingProfile *)activeProfile {
    for (SenkoRoutingProfile *p in _profiles)
        if (p->index == _active) return p;
    return nil;
}

- (int)appRuleCount {
    int n = 0;
    for (SenkoRule *r in _rules) if ([r->type isEqualToString:@"app"]) ++n;
    return n;
}

- (int)otherRuleCount {
    return (int)[_rules count] - [self appRuleCount];
}

/* the tiles of up to four apps that have a rule, overlapping, with the disclosure
   chevron after them: the apps row shows who is routed without opening it */
- (UIView *)appStrip {
    NSMutableArray *icons = [NSMutableArray array];
    NSArray *apps = [SenkoAppCatalog cachedApps];
    for (SenkoRule *r in _rules) {
        if (![r->type isEqualToString:@"app"] || [icons count] >= ICON_STRIP_MAX) continue;
        for (SenkoInstalledApp *app in apps) {
            if (![app->bundleID isEqualToString:r->value]) continue;
            [icons addObject:[SenkoAppCatalog iconForApp:app side:26.0f]];
            break;
        }
    }
    CGFloat tile = 26.0f, step = 17.0f, gap = 8.0f, chevron = 10.0f;
    CGFloat width = [icons count] ? tile + step * ([icons count] - 1) + gap : 0.0f;
    UIView *strip = [[[UIView alloc] initWithFrame:
                      CGRectMake(0, 0, width + chevron + 4.0f, tile)] autorelease];
    for (NSUInteger i = 0; i < [icons count]; ++i) {
        UIImageView *iv = [[[UIImageView alloc] initWithImage:[icons objectAtIndex:i]] autorelease];
        iv.frame = CGRectMake(step * (CGFloat)i, 0, tile, tile);
        /* the later tile sits under the earlier one's edge */
        [strip insertSubview:iv atIndex:0];
    }
    UIImageView *arrow = [[[UIImageView alloc] initWithImage:SenkoIconChevron(chevron, kInkMuted)] autorelease];
    arrow.frame = CGRectMake(width, (tile - chevron) / 2.0f, chevron, chevron);
    [strip addSubview:arrow];
    return strip;
}

#pragma mark table

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return RoutingSectionCount;
}

- (BOOL)showsGeo {
    SenkoRoutingProfile *p = [self activeProfile];
    return p && p->geoTags > 0;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    if (s == RoutingSectionProfile) return 1 + (NSInteger)[_profiles count];
    if (s == RoutingSectionGeo) return [self showsGeo] ? 1 : 0;
    return [self activeProfile] ? 3 : 2;
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return 52.0f;
}

- (NSString *)headerForSection:(NSInteger)s {
    if (s == RoutingSectionProfile) return SenkoLocalizedText(@"Routing profile");
    if (s == RoutingSectionGeo) return [self showsGeo] ? SenkoLocalizedText(@"Geo data") : nil;
    return SenkoLocalizedText(@"Rules");
}

- (NSString *)footerForSection:(NSInteger)s {
    if (s == RoutingSectionProfile) {
        if (!_profilesLoaded) return SenkoLocalizedText(@"Daemon is unreachable");
        NSMutableString *text = [NSMutableString string];
        if ([_notice length]) [text appendFormat:@"%@\n", _notice];
        [text appendString:[_profiles count]
            ? SenkoLocalizedText(@"Profiles come from your subscriptions. A change applies the next time the tunnel connects.")
            : SenkoLocalizedText(@"No subscription has sent a routing profile. A happ panel sends one in its response, and it shows up here after the next refresh.")];
        return text;
    }
    if (s == RoutingSectionGeo)
        return [self showsGeo]
            ? SenkoLocalizedText(@"The profile names geosite and geoip lists. They are downloaded once and kept for a week.")
            : nil;
    return SenkoLocalizedText(@"A rule for an app wins over every domain and address rule. Block wins over direct, and direct over the tunnel.");
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    return [self headerForSection:s] ? 36.0f : 0.5f;
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    NSString *text = [self headerForSection:s];
    if (!text) return [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    return SenkoCaptionView(text, SenkoFontBody(13.0f, YES), tv);
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    NSString *text = [self footerForSection:s];
    return text ? SenkoCaptionHeight(text, [UIFont systemFontOfSize:12.0f], tv) : 0.5f;
}

- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)s {
    NSString *text = [self footerForSection:s];
    if (!text) return [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    return SenkoCaptionView(text, [UIFont systemFontOfSize:12.0f], tv);
}

- (void)tableView:(UITableView *)tv willDisplayCell:(UITableViewCell *)cell
 forRowAtIndexPath:(NSIndexPath *)ip {
    SenkoStyleGroupCell(cell, ip, [self tableView:tv numberOfRowsInSection:ip.section]);
}

- (NSString *)geoStatusText {
    if (!_geo) return SenkoLocalizedText(@"unreachable");
    if (_geo->busy) return SenkoLocalizedText(@"Downloading...");
    if (_geo->result == 2)
        return [NSString stringWithFormat:SenkoLocalizedText(@"Failed: %@"), _geo->message];
    if (_geo->missing > 0)
        return [NSString stringWithFormat:SenkoLocalizedText(@"%d lists missing"), _geo->missing];
    if (_geo->stamp == 0) return SenkoLocalizedText(@"Not downloaded");
    NSDateFormatter *f = [[[NSDateFormatter alloc] init] autorelease];
    [f setDateStyle:NSDateFormatterMediumStyle];
    [f setTimeStyle:NSDateFormatterNoStyle];
    return [NSString stringWithFormat:SenkoLocalizedText(@"Updated %@"),
            [f stringFromDate:[NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)_geo->stamp]]];
}

- (NSString *)detailForProfile:(SenkoRoutingProfile *)p {
    NSMutableString *text = [NSMutableString string];
    [text appendFormat:SenkoLocalizedText(@"%d rules"), p->rules];
    if (p->unsupported)
        [text appendFormat:@" (%@)", [NSString stringWithFormat:SenkoLocalizedText(@"%d not supported"), p->unsupported]];
    [text appendString:@" · "];
    [text appendString:p->globalProxy ? SenkoLocalizedText(@"rest through the tunnel")
                                      : SenkoLocalizedText(@"rest direct")];
    return text;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"route";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                       reuseIdentifier:cid] autorelease];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.clipsToBounds = YES;
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.accessoryView = nil;
    cell.imageView.image = nil;
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];
    cell.textLabel.font = SenkoFontBody(16.0f, NO);
    cell.textLabel.textColor = kInk;
    cell.textLabel.shadowColor = nil;
    cell.textLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    cell.detailTextLabel.font = SenkoFontBody(12.0f, NO);
    cell.detailTextLabel.textColor = kInkMuted;
    cell.detailTextLabel.shadowColor = nil;
    cell.detailTextLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    cell.detailTextLabel.text = nil;

    if (ip.section == RoutingSectionProfile) {
        BOOL off = ip.row == 0;
        SenkoRoutingProfile *p = off ? nil : [_profiles objectAtIndex:(NSUInteger)ip.row - 1];
        BOOL on = off ? _active < 0 : p->active;
        cell.textLabel.text = off ? SenkoLocalizedText(@"Off") : p->name;
        cell.detailTextLabel.text = off ? SenkoLocalizedText(@"Everything goes through the tunnel, apart from your own rules")
                                        : [self detailForProfile:p];
        cell.imageView.image = TintedIconNamed(off ? @"glyph-shield.png" : @"glyph-split.png",
                                               22.0f, on ? kConnOn : kInk);
        cell.accessoryType = on ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
        SenkoStyleSelectableCell(cell);
        return cell;
    }
    if (ip.section == RoutingSectionGeo) {
        cell.textLabel.text = SenkoLocalizedText(@"Update geo data");
        cell.detailTextLabel.text = [self geoStatusText];
        cell.imageView.image = TintedIconNamed(@"glyph-cloud.png", 22.0f, kInk);
        if (_geo && _geo->busy) {
            UIActivityIndicatorView *spin = [[[UIActivityIndicatorView alloc]
                initWithActivityIndicatorStyle:SenkoThemeIsLight() ? UIActivityIndicatorViewStyleGray
                                                                   : UIActivityIndicatorViewStyleWhite] autorelease];
            [spin startAnimating];
            cell.accessoryView = spin;
        } else {
            SenkoStyleSelectableCell(cell);
        }
        return cell;
    }
    switch (ip.row) {
        case 0:
            cell.textLabel.text = SenkoLocalizedText(@"Apps");
            cell.detailTextLabel.text = [self appRuleCount]
                ? [NSString stringWithFormat:SenkoLocalizedText(@"%d with a rule"), [self appRuleCount]]
                : SenkoLocalizedText(@"Choose how each app is routed");
            cell.imageView.image = TintedIconNamed(@"glyph-hash.png", 22.0f, kInk);
            cell.accessoryView = [self appStrip];
            break;
        case 1:
            cell.textLabel.text = SenkoLocalizedText(@"Domains and addresses");
            cell.detailTextLabel.text = [self otherRuleCount]
                ? [NSString stringWithFormat:SenkoLocalizedText(@"%d rules"), [self otherRuleCount]]
                : SenkoLocalizedText(@"No rules yet");
            cell.imageView.image = TintedIconNamed(@"glyph-globe.png", 22.0f, kInk);
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            break;
        default:
            cell.textLabel.text = SenkoLocalizedText(@"Profile entries");
            {
                SenkoRoutingProfile *current = [self activeProfile];
                cell.detailTextLabel.text = current ? current->name : nil;
            }
            cell.imageView.image = TintedIconNamed(@"glyph-logs.png", 22.0f, kInk);
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            break;
    }
    SenkoStyleSelectableCell(cell);
    return cell;
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    if (ip.section == RoutingSectionProfile) {
        int sub = ip.row == 0 ? -1 : ((SenkoRoutingProfile *)[_profiles objectAtIndex:(NSUInteger)ip.row - 1])->index;
        if (sub == _active) return;
        [_ctl setRoutingProfileIndex:sub reply:^(NSString *reply) {
            [_notice release];
            _notice = [([reply hasPrefix:@"OK "] ? [reply substringFromIndex:3]
                        : SenkoHumanReadableError(reply ? reply : SenkoLocalizedText(@"Daemon is unreachable"))) copy];
            [self reload];
        }];
        return;
    }
    if (ip.section == RoutingSectionGeo) {
        if (_geo && _geo->busy) return;
        [_ctl updateGeo:^(NSString *reply) {
            [_notice release];
            _notice = [([reply hasPrefix:@"OK "] ? nil : SenkoHumanReadableError(reply ? reply : SenkoLocalizedText(@"Daemon is unreachable"))) copy];
            [self reloadGeo];
        }];
        return;
    }
    if (ip.row == 0) {
        [self.navigationController pushViewController:
            [[[AppRoutingVC alloc] initWithControl:_ctl] autorelease] animated:YES];
    } else if (ip.row == 1) {
        [self.navigationController pushViewController:
            [[[RulesVC alloc] initWithControl:_ctl] autorelease] animated:YES];
    } else {
        SenkoRoutingProfile *p = [self activeProfile];
        if (!p) return;
        [self.navigationController pushViewController:
            [[[ProfileEntriesVC alloc] initWithControl:_ctl subscription:p->index name:p->name] autorelease]
                                             animated:YES];
    }
}

@end
