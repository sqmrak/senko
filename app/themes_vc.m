#import "themes_vc.h"
#import "ui_theme.h"
#import "meow.h"
#include <objc/message.h>

/* theme list inside one group (ios / custom /...) */
@interface ThemeGroupVC : UIViewController <UITableViewDataSource, UITableViewDelegate,
                                             UIAlertViewDelegate> {
    UITableView *_tv;
    NSString *_groupId;
    NSArray *_ids;
    NSString *_pendingTid; /* ios26 lag warn on old firmware */
}
- (id)initWithGroupId:(NSString *)groupId;
@end

@implementation ThemeGroupVC

static BOOL SenkoHostIsIos6or7(void) {
    return [[[UIDevice currentDevice] systemVersion] floatValue] < 8.0f;
}

- (id)initWithGroupId:(NSString *)groupId {
    if ((self = [super init])) {
        _groupId = [groupId copy];
        _ids = [SenkoThemeIdsInGroup(groupId) retain];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_groupId release];
    [_ids release];
    [_pendingTid release];
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoThemeGroupTitle(_groupId);
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);
    self.view.backgroundColor = kBG;
    _tv = [[[UITableView alloc] initWithFrame:self.view.bounds
                                        style:UITableViewStyleGrouped] autorelease];
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _tv.backgroundColor = kBG;
    _tv.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.18]
        : [UIColor colorWithWhite:1 alpha:0.14];
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
    self.view.backgroundColor = kBG;
    _tv.backgroundColor = kBG;
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.18]
        : [UIColor colorWithWhite:1 alpha:0.14];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return 1;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv; (void)s;
    return (NSInteger)[_ids count];
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return 52.0f;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"theme";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                       reuseIdentifier:cid] autorelease];
    NSString *tid = [_ids objectAtIndex:ip.row];
    cell.textLabel.text = SenkoThemeDisplayName(tid);
    cell.detailTextLabel.text = SenkoThemeBlurb(tid);
    SenkoStyleInkLabel(cell.textLabel);
    SenkoStyleMutedLabel(cell.detailTextLabel);
    cell.backgroundColor = kCellHi;
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];
    BOOL on = [tid isEqualToString:SenkoThemeCurrentId()];
    cell.accessoryType = on ? UITableViewCellAccessoryCheckmark
                            : UITableViewCellAccessoryNone;
    cell.selectionStyle = UITableViewCellSelectionStyleBlue;
    return cell;
}

- (void)applyThemeId:(NSString *)tid {
    if (![tid length]) return;
    if ([tid isEqualToString:SenkoThemeCurrentId()]) return;
    SenkoThemeApplyId(tid);
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    SenkoThemeSfxPlay();
    NSString *tid = [_ids objectAtIndex:ip.row];
    if ([tid isEqualToString:SenkoThemeCurrentId()]) return;

    if ([tid isEqualToString:@"senko-ios26"] && SenkoHostIsIos6or7()) {
        [_pendingTid release];
        _pendingTid = [tid copy];
        UIAlertView *a = [[UIAlertView alloc]
            initWithTitle:@"Senko-iOS26"
                  message:@"This theme will lag on iOS 6/7. Liquid glass is laggy on older device."
                 delegate:self
        cancelButtonTitle:@"Cancel"
        otherButtonTitles:@"Apply anyway", nil];
        [a show];
        [a release];
        return;
    }
    [self applyThemeId:tid];
}

- (void)alertView:(UIAlertView *)alert clickedButtonAtIndex:(NSInteger)idx {
    (void)alert;
    if (idx == 1 && _pendingTid)
        [self applyThemeId:_pendingTid];
    [_pendingTid release];
    _pendingTid = nil;
}

@end

@implementation ThemesVC {
    UITableView *_tv;
    NSArray *_groups;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_groups release];
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Themes";
    _groups = [SenkoThemeGroupIds() retain];
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);
    self.view.backgroundColor = kBG;
    _tv = [[[UITableView alloc] initWithFrame:self.view.bounds
                                        style:UITableViewStyleGrouped] autorelease];
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _tv.backgroundColor = kBG;
    _tv.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.18]
        : [UIColor colorWithWhite:1 alpha:0.14];
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

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
/* refresh current style labels after picking inside a group */
    [_tv reloadData];
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    self.view.backgroundColor = kBG;
    _tv.backgroundColor = kBG;
    _tv.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.18]
        : [UIColor colorWithWhite:1 alpha:0.14];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    SenkoThemeSfxPrepare();
    [_groups release];
    _groups = [SenkoThemeGroupIds() retain];
    [_tv reloadData];
}

- (void)modeSegChanged:(UISegmentedControl *)seg {
/* 0 = dark, 1 = light */
    if (!SenkoThemeAllowsDark()) {
        seg.selectedSegmentIndex = SenkoThemeIsMiside() ? 0 : 1;
        return;
    }
    SenkoThemeSetLight(seg.selectedSegmentIndex == 1);
}

- (void)meowSwitchChanged:(UISwitch *)sw {
    SenkoMeowSetEnabled(sw.on);
    if (sw.on)
        SenkoMeowPlay();
}

- (void)ouchSwitchChanged:(UISwitch *)sw {
    SenkoOuchSetEnabled(sw.on);
    if (sw.on)
        SenkoOuchPlay();
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
/* style groups | appearance | theme sfx */
    return (SenkoThemeIsBoykisser() || SenkoThemeIsMiside()) ? 3 : 2;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    if (s == 0) return (NSInteger)[_groups count];
    if (s == 1) return 1; /* dark / light */
    return 1; /* sfx toggle */
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    CGFloat w = tv.bounds.size.width;
    UIView *wrap = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, w, 32)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    UILabel *lab = [[[UILabel alloc] initWithFrame:CGRectMake(16, 8, w - 32, 18)] autorelease];
    lab.backgroundColor = [UIColor clearColor];
    lab.font = [UIFont boldSystemFontOfSize:13];
    if (s == 0) lab.text = @"Style";
    else if (s == 1) lab.text = @"Appearance";
    else lab.text = SenkoThemeIsMiside() ? @"ooouch" : @"meowmeowmeow";
    SenkoStyleMutedLabel(lab);
    lab.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [wrap addSubview:lab];
    return wrap;
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 32;
}

- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)s {
    CGFloat w = tv.bounds.size.width;
    if (s == 1) {
        UIView *wrap = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, w, 54)] autorelease];
        wrap.backgroundColor = [UIColor clearColor];
        UILabel *lab = [[[UILabel alloc] initWithFrame:CGRectMake(16, 6, w - 32, 44)] autorelease];
        lab.backgroundColor = [UIColor clearColor];
        lab.numberOfLines = 0;
        lab.font = [UIFont systemFontOfSize:12];
        if (SenkoThemeIsMiside())
            lab.text = @"Senko-Miside is Dark only: pattern wallpaper and candy heart ON.";
        else if (SenkoThemeIsBoykisser())
            lab.text = @"Senko-Boykisser is Light only: pink paper and falling boykissers on the home screen.";
        else if (SenkoThemeIsFrutigeraero())
            lab.text = @"Senko-Aero is Light only: sky wallpaper and floating gloss bubbles.";
        else
            lab.text = @"Dark / Light applies to the selected style. Choice is stored on device.";
        SenkoStyleMutedLabel(lab);
        lab.autoresizingMask = UIViewAutoresizingFlexibleWidth;
        [wrap addSubview:lab];
        return wrap;
    }
    if (s == 2 && (SenkoThemeIsBoykisser() || SenkoThemeIsMiside())) {
        UIView *wrap = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, w, 40)] autorelease];
        wrap.backgroundColor = [UIColor clearColor];
        UILabel *lab = [[[UILabel alloc] initWithFrame:CGRectMake(16, 4, w - 32, 32)] autorelease];
        lab.backgroundColor = [UIColor clearColor];
        lab.numberOfLines = 0;
        lab.font = [UIFont systemFontOfSize:12];
        lab.text = SenkoThemeIsMiside()
            ? @"Play a short ouch on every button tap."
            : @"Play a short meow on every button tap.";
        SenkoStyleMutedLabel(lab);
        lab.autoresizingMask = UIViewAutoresizingFlexibleWidth;
        [wrap addSubview:lab];
        return wrap;
    }
    return nil;
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    (void)tv;
    if (s == 1) return 54.0f;
    if (s == 2 && (SenkoThemeIsBoykisser() || SenkoThemeIsMiside())) return 40.0f;
    return 10.0f;
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv;
    if (ip.section == 1 || ip.section == 2) return 52.0f;
    return 48.0f;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    if (ip.section == 2) {
        static NSString *cid = @"sfx";
        UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
        if (!cell)
            cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                           reuseIdentifier:cid] autorelease];
        BOOL miside = SenkoThemeIsMiside();
        cell.textLabel.text = miside ? @"ooouch" : @"meowmeowmeow";
        SenkoStyleInkLabel(cell.textLabel);
        cell.backgroundColor = kCellHi;
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        cell.accessoryType = UITableViewCellAccessoryNone;
        UISwitch *sw = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
        if (miside) {
            sw.on = SenkoOuchEnabled();
            [sw addTarget:self action:@selector(ouchSwitchChanged:)
         forControlEvents:UIControlEventValueChanged];
            if ([sw respondsToSelector:@selector(setOnTintColor:)])
                sw.onTintColor = [UIColor colorWithRed:1.00 green:0.36 blue:0.70 alpha:1.0];
        } else {
            sw.on = SenkoMeowEnabled();
            [sw addTarget:self action:@selector(meowSwitchChanged:)
         forControlEvents:UIControlEventValueChanged];
            if ([sw respondsToSelector:@selector(setOnTintColor:)])
                sw.onTintColor = [UIColor colorWithRed:1.00 green:0.38 blue:0.68 alpha:1.0];
        }
        cell.accessoryView = sw;
        return cell;
    }

    if (ip.section == 1) {
        static NSString *cid = @"mode";
        UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
        if (!cell)
            cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                           reuseIdentifier:cid] autorelease];
        cell.textLabel.text = nil;
        cell.detailTextLabel.text = nil;
        cell.backgroundColor = kCellHi;
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        cell.accessoryType = UITableViewCellAccessoryNone;
        cell.accessoryView = nil;

        UISegmentedControl *seg = (UISegmentedControl *)[cell.contentView viewWithTag:9201];
        if (![seg isKindOfClass:[UISegmentedControl class]]) {
            [[cell.contentView viewWithTag:9201] removeFromSuperview];
            seg = [[[UISegmentedControl alloc] initWithItems:
                    [NSArray arrayWithObjects:@"Dark", @"Light", nil]] autorelease];
            seg.tag = 9201;
            seg.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                   UIViewAutoresizingFlexibleTopMargin |
                                   UIViewAutoresizingFlexibleBottomMargin;
            [seg addTarget:self action:@selector(modeSegChanged:)
          forControlEvents:UIControlEventValueChanged];
            [cell.contentView addSubview:seg];
        }
        CGFloat pad = 14.0f;
        CGFloat h = 32.0f;
        seg.frame = CGRectMake(pad, (52.0f - h) / 2.0f,
                               cell.contentView.bounds.size.width - pad * 2.0f, h);
        if (seg.frame.size.width < 1)
            seg.frame = CGRectMake(pad, 10, tv.bounds.size.width - 40, h);
        if (!SenkoThemeAllowsDark()) {
            seg.selectedSegmentIndex = SenkoThemeIsMiside() ? 0 : 1;
            seg.enabled = NO;
            seg.alpha = 0.55f;
        } else {
            seg.enabled = YES;
            seg.alpha = 1.0f;
            seg.selectedSegmentIndex = SenkoThemeIsLight() ? 1 : 0;
        }
        if ([seg respondsToSelector:@selector(setTintColor:)])
            seg.tintColor = kAccentBlue;
        return cell;
    }

/* style groups */
    static NSString *cid = @"group";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                       reuseIdentifier:cid] autorelease];
    NSString *gid = [_groups objectAtIndex:ip.row];
    cell.textLabel.text = SenkoThemeGroupTitle(gid);
    NSString *curG = SenkoThemeGroupOfId(SenkoThemeCurrentId());
    if ([curG isEqualToString:gid])
        cell.detailTextLabel.text = SenkoThemeDisplayName(SenkoThemeCurrentId());
    else
        cell.detailTextLabel.text = nil;
    SenkoStyleInkLabel(cell.textLabel);
    SenkoStyleMutedLabel(cell.detailTextLabel);
    cell.backgroundColor = kCellHi;
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];
    cell.accessoryView = nil;
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    cell.selectionStyle = UITableViewCellSelectionStyleBlue;
    return cell;
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    if (ip.section != 0) return;
    SenkoThemeSfxPlay();
    NSString *gid = [_groups objectAtIndex:ip.row];
    ThemeGroupVC *vc = [[[ThemeGroupVC alloc] initWithGroupId:gid] autorelease];
    [self.navigationController pushViewController:vc animated:YES];
}

@end
