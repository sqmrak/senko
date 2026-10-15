#import "app_routing_vc.h"

#import "app_catalog.h"
#import "app_common.h"
#import "control_client.h"
#import "crash_report.h"
#import "routing_ui.h"
#import "ui_theme.h"

#include <objc/message.h>

#define APP_SHEET_ACTION 4401
#define ICON_SIDE 40.0f

/* the daemon's own words, as the rules screen sends them */
static NSString *const kAppActions[] = { @"proxy", @"direct", @"block" };

@implementation AppRoutingVC

- (id)initWithControl:(SenkoControl *)control {
    if ((self = [super init])) {
        _ctl = [control retain];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    _search.delegate = nil;
    [_tv release];
    [_search release];
    [_ctl release];
    [_apps release];
    [_ruled release];
    [_plain release];
    [_rules release];
    [_query release];
    [_picked release];
    [super dealloc];
}

/* ios 5 releases the view of an offscreen controller, so the retained views must
   go with it instead of pointing into a freed hierarchy */
- (void)viewDidUnload {
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:SenkoThemeDidChangeNotification
                                                  object:nil];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    _search.delegate = nil;
    [_tv release];
    _tv = nil;
    [_search release];
    _search = nil;
    [super viewDidUnload];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"Apps");
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);
    SenkoApplyScreenChrome(self.view);
    _tv = [[UITableView alloc] initWithFrame:self.view.bounds style:UITableViewStylePlain];
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    SenkoClearTableBackground(_tv);
    _tv.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.18] : [UIColor colorWithWhite:1 alpha:0.14];
    if ([_tv respondsToSelector:@selector(setBackgroundView:)]) _tv.backgroundView = nil;
    [self.view addSubview:_tv];

    _search = [[UISearchBar alloc] initWithFrame:CGRectMake(0, 0, self.view.bounds.size.width, 44.0f)];
    _search.delegate = self;
    _search.placeholder = SenkoLocalizedText(@"Search apps");
    _search.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _search.autocorrectionType = UITextAutocorrectionTypeNo;
    _search.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    _tv.tableHeaderView = _search;

    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];

    /* the last scan is shown at once and the folders are read again off the main
       thread: a device with hundreds of apps takes a moment to scan */
    NSArray *known = [SenkoAppCatalog cachedApps];
    if (known) {
        [_apps release];
        _apps = [known retain];
        _appsLoaded = YES;
        [self rebuild];
    }
    [self retain];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSArray *fresh = [SenkoAppCatalog installedApps];
        dispatch_async(dispatch_get_main_queue(), ^{
            [_apps release];
            _apps = [fresh retain];
            _appsLoaded = YES;
            [self rebuild];
            [self release];
        });
    });
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("apps");
    [super viewWillAppear:animated];
    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    [self reloadRules];
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    SenkoApplyScreenChrome(self.view);
    SenkoClearTableBackground(_tv);
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.18] : [UIColor colorWithWhite:1 alpha:0.14];
    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

- (void)reloadRules {
    [_ctl listRules:^(NSArray *rules) {
        NSMutableDictionary *byApp = [NSMutableDictionary dictionary];
        for (SenkoRule *r in rules)
            if ([r->type isEqualToString:@"app"]) [byApp setObject:r forKey:r->value];
        [_rules release];
        _rules = rules ? [byApp copy] : nil;
        _rulesLoaded = rules != nil;
        [self rebuild];
    }];
}

/* with no search the apps that have a rule come first, as their own group; a
   search is one flat list of the matches */
- (void)rebuild {
    NSMutableArray *ruled = [NSMutableArray array];
    NSMutableArray *plain = [NSMutableArray array];
    BOOL searching = [_query length] > 0;
    for (SenkoInstalledApp *app in _apps) {
        if (searching) {
            if ([app->name rangeOfString:_query options:NSCaseInsensitiveSearch].location == NSNotFound &&
                [app->bundleID rangeOfString:_query options:NSCaseInsensitiveSearch].location == NSNotFound)
                continue;
            [plain addObject:app];
        } else if ([_rules objectForKey:app->bundleID]) {
            [ruled addObject:app];
        } else {
            [plain addObject:app];
        }
    }
    [_ruled release];
    _ruled = [ruled copy];
    [_plain release];
    _plain = [plain copy];
    [_tv reloadData];
}

- (void)showMessage:(NSString *)body {
    UIAlertView *a = [[UIAlertView alloc] initWithTitle:SenkoLocalizedText(@"Apps")
                                                message:SenkoHumanReadableError(body)
                                               delegate:nil
                                      cancelButtonTitle:@"OK"
                                      otherButtonTitles:nil];
    [a show];
    [a release];
}

- (SenkoInstalledApp *)appAtIndexPath:(NSIndexPath *)ip {
    NSArray *list = ([_ruled count] && ip.section == 0) ? _ruled : _plain;
    return ip.row < (NSInteger)[list count] ? [list objectAtIndex:ip.row] : nil;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return [_ruled count] ? 2 : 1;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    return (NSInteger)(([_ruled count] && s == 0) ? [_ruled count] : [_plain count]);
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return 58.0f;
}

- (NSString *)headerForSection:(NSInteger)s {
    if (![_ruled count]) return [_query length] ? nil : SenkoLocalizedText(@"All apps");
    return s == 0 ? SenkoLocalizedText(@"With a rule") : SenkoLocalizedText(@"All apps");
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    return [self headerForSection:s] ? 28.0f : 0.0f;
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    NSString *text = [self headerForSection:s];
    UIView *wrap = [[[UIView alloc] initWithFrame:
                     CGRectMake(0, 0, tv.bounds.size.width, 28.0f)] autorelease];
    wrap.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0.0f alpha:0.05f] : [UIColor colorWithWhite:1.0f alpha:0.06f];
    if (!text) return wrap;
    UILabel *label = [[[UILabel alloc] initWithFrame:
                       CGRectMake(16.0f, 0, tv.bounds.size.width - 32.0f, 28.0f)] autorelease];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    label.backgroundColor = [UIColor clearColor];
    label.font = [UIFont boldSystemFontOfSize:12.0f];
    label.text = text;
    SenkoStyleMutedLabel(label);
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
    [wrap addSubview:label];
    return wrap;
}

- (NSString *)footerText {
    if (!_appsLoaded) return SenkoLocalizedText(@"Reading the app list...");
    if (!_rulesLoaded) return SenkoLocalizedText(@"Daemon is unreachable");
    if (![_apps count]) return SenkoLocalizedText(@"No apps found.");
    return nil;
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    if (s != [self numberOfSectionsInTableView:tv] - 1 || ![[self footerText] length])
        return 0.0f;
    return SenkoCaptionHeight([self footerText], [UIFont systemFontOfSize:12.0f], tv) + 8.0f;
}

- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)s {
    if (s != [self numberOfSectionsInTableView:tv] - 1 || ![[self footerText] length])
        return [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    return SenkoCaptionView([self footerText], [UIFont systemFontOfSize:12.0f], tv);
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"app";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                       reuseIdentifier:cid] autorelease];
    SenkoInstalledApp *app = [self appAtIndexPath:ip];
    if (!app) return cell;
    SenkoRule *rule = [_rules objectForKey:app->bundleID];

    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.backgroundColor = kCellHi;
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];
    cell.textLabel.font = [UIFont systemFontOfSize:16.0f];
    cell.textLabel.textColor = kInk;
    cell.textLabel.shadowColor = nil;
    cell.textLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    cell.textLabel.text = app->name;
    cell.detailTextLabel.font = [UIFont systemFontOfSize:11.0f];
    cell.detailTextLabel.textColor = kInkMuted;
    cell.detailTextLabel.shadowColor = nil;
    cell.detailTextLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
    cell.detailTextLabel.text = app->bundleID;
    cell.imageView.image = [SenkoAppCatalog iconForApp:app side:ICON_SIDE];

    /* the way this app goes, in the color that action has everywhere else */
    UILabel *tag = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    tag.backgroundColor = [UIColor clearColor];
    tag.font = [UIFont boldSystemFontOfSize:12.0f];
    tag.text = rule ? SenkoActionTitle(rule->action) : SenkoLocalizedText(@"Default");
    tag.textColor = rule ? SenkoActionColor(rule->action) : kInkMuted;
    tag.textAlignment = NSTextAlignmentRight;
    [tag sizeToFit];
    tag.frame = CGRectMake(0, 0, MIN(tag.frame.size.width, 130.0f) + 6.0f, 20.0f);
    cell.accessoryView = tag;
    SenkoStyleSelectableCell(cell);
    return cell;
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    SenkoInstalledApp *app = [self appAtIndexPath:ip];
    if (!app) return;
    [_picked release];
    _picked = [app retain];
    SenkoRule *rule = [_rules objectForKey:app->bundleID];
    UIActionSheet *sheet = [[UIActionSheet alloc]
             initWithTitle:app->name
                  delegate:self
         cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    destructiveButtonTitle:rule ? SenkoLocalizedText(@"Default") : nil
         otherButtonTitles:SenkoActionTitle(kAppActions[0]),
                           SenkoActionTitle(kAppActions[1]),
                           SenkoActionTitle(kAppActions[2]), nil];
    sheet.tag = APP_SHEET_ACTION;
    [sheet showInView:self.view];
    [sheet release];
}

/* the destructive button, when there is one, is index 0 and clears the rule; the
   three actions follow it. without a rule they start at 0 */
- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)index {
    if (sheet.tag != APP_SHEET_ACTION || !_picked || index == sheet.cancelButtonIndex) return;
    SenkoRule *rule = [_rules objectForKey:_picked->bundleID];
    NSInteger first = rule ? 1 : 0;
    void (^done)(NSString *) = ^(NSString *reply) {
        if (![reply hasPrefix:@"OK "])
            [self showMessage:reply ? reply : SenkoLocalizedText(@"Daemon is unreachable")];
        [self reloadRules];
    };
    if (rule && index == 0) {
        /* removing a rule renumbers the ones after it, so the list is read back */
        [_ctl deleteRuleIndex:rule->index reply:done];
        return;
    }
    NSInteger pick = index - first;
    if (pick < 0 || pick > 2) return;
    [_ctl addRuleAction:kAppActions[pick] type:@"app" value:_picked->bundleID reply:done];
}

#pragma mark search

- (void)searchBar:(UISearchBar *)bar textDidChange:(NSString *)text {
    (void)bar;
    [_query release];
    _query = [[text stringByTrimmingCharactersInSet:
               [NSCharacterSet whitespaceAndNewlineCharacterSet]] copy];
    [self rebuild];
}

- (void)searchBarSearchButtonClicked:(UISearchBar *)bar {
    [bar resignFirstResponder];
}

- (void)scrollViewWillBeginDragging:(UIScrollView *)scroll {
    (void)scroll;
    [_search resignFirstResponder];
}

@end
