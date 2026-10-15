#import "profile_entries_vc.h"

#import "app_common.h"
#import "control_client.h"
#import "crash_report.h"
#import "routing_ui.h"
#import "ui_theme.h"

#include <objc/message.h>

@implementation ProfileEntriesVC

- (id)initWithControl:(SenkoControl *)control subscription:(int)sub name:(NSString *)name {
    if ((self = [super init])) {
        _ctl = [control retain];
        _sub = sub;
        _profileName = [name copy];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    [_ctl release];
    [_profileName release];
    [_entries release];
    [super dealloc];
}

/* ios 5 releases the view of an offscreen controller, so the retained table
   must go with it instead of pointing into a freed hierarchy */
- (void)viewDidUnload {
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:SenkoThemeDidChangeNotification
                                                  object:nil];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    _tv = nil;
    [super viewDidUnload];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = [_profileName length] ? _profileName : SenkoLocalizedText(@"Routing profile");
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
    SenkoCrashScreen("profile-entries");
    [super viewWillAppear:animated];
    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    [_ctl listRoutingEntriesIndex:_sub done:^(NSArray *entries, int total) {
        [_entries release];
        _entries = [entries retain];
        _total = total;
        _loaded = YES;
        [_tv reloadData];
    }];
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    SenkoApplyScreenChrome(self.view);
    SenkoClearTableBackground(_tv);
    if (self.navigationController) StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

- (NSString *)footerText {
    if (!_loaded) return SenkoLocalizedText(@"Reading the profile from the daemon...");
    if (!_entries) return SenkoLocalizedText(@"Daemon is unreachable");
    NSMutableString *text = [NSMutableString string];
    if (_total > (int)[_entries count])
        [text appendFormat:SenkoLocalizedText(@"Showing the first %d of %d entries. "),
                           (int)[_entries count], _total];
    [text appendString:SenkoLocalizedText(@"Entries marked as not supported (regexp, ext) are skipped. A name that is not written as domain:, full:, keyword: or geosite: matches as part of a name.")];
    return text;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv { (void)tv; return 1; }

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv; (void)s;
    return (NSInteger)[_entries count];
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return 50.0f;
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    (void)s;
    return SenkoCaptionHeight([self footerText], [UIFont systemFontOfSize:12.0f], tv);
}

- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)s {
    (void)s;
    return SenkoCaptionView([self footerText], [UIFont systemFontOfSize:12.0f], tv);
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"entry";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                       reuseIdentifier:cid] autorelease];
    if (ip.row >= (NSInteger)[_entries count]) return cell;
    SenkoRoutingEntry *e = [_entries objectAtIndex:ip.row];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.backgroundColor = kCellHi;
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];
    cell.textLabel.font = [UIFont systemFontOfSize:15.0f];
    cell.textLabel.textColor = e->supported ? kInk : kInkMuted;
    cell.textLabel.shadowColor = nil;
    /* a name clipped in the middle is a name nobody can read back */
    cell.textLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    cell.textLabel.text = e->entry;
    cell.detailTextLabel.font = [UIFont systemFontOfSize:12.0f];
    cell.detailTextLabel.shadowColor = nil;
    cell.detailTextLabel.textColor = e->supported ? SenkoActionColor(e->action) : kInkMuted;
    cell.detailTextLabel.text = e->supported
        ? [NSString stringWithFormat:@"%@ · %@", SenkoActionTitle(e->action),
           e->isIP ? SenkoLocalizedText(@"Addresses") : SenkoLocalizedText(@"Names")]
        : SenkoLocalizedText(@"Not supported, skipped");
    return cell;
}

@end
