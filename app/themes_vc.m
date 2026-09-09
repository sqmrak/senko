#import "themes_vc.h"
#import "ui_theme.h"
#import "meow.h"
#import "app_common.h"
#import "crash_report.h"
#import "theme_edit_vc.h"
#import "theme/theme_custom.h"
#include <objc/message.h>

@interface ThemeGroupVC : UIViewController <UITableViewDataSource, UITableViewDelegate,
                                             UIActionSheetDelegate,
                                             FileImportDelegate> {
    UITableView *_tv;
    NSString *_groupId;
    NSArray *_ids;
    BOOL _isCustomGroup;
}
- (id)initWithGroupId:(NSString *)groupId;
@end

@implementation ThemeGroupVC

- (id)initWithGroupId:(NSString *)groupId {
    if ((self = [super init])) {
        _groupId = [groupId copy];
        _ids = [SenkoThemeIdsInGroup(groupId) retain];
        _isCustomGroup = [groupId isEqualToString:@SENKO_THEME_GROUP_CUSTOM];
    }
    return self;
}

- (void)reloadIds {
    NSArray *fresh = [SenkoThemeIdsInGroup(_groupId) retain];
    [_ids release];
    _ids = fresh;
    [_tv reloadData];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    [_groupId release];
    [_ids release];
    [super dealloc];
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

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoThemeGroupTitle(_groupId);
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);
    self.view.backgroundColor = kBG;
    _tv = [[UITableView alloc] initWithFrame:self.view.bounds
                                       style:UITableViewStyleGrouped];
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
    if (_isCustomGroup) {
        UIBarButtonItem *add = [[[UIBarButtonItem alloc]
            initWithBarButtonSystemItem:UIBarButtonSystemItemAdd
                                 target:self
                                 action:@selector(addTapped)] autorelease];
        self.navigationItem.rightBarButtonItem = add;
    }
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("themes");
    [super viewWillAppear:animated];
    if (_isCustomGroup) [self reloadIds];
}

- (void)showMessage:(NSString *)title body:(NSString *)body {
    UIAlertView *a = [[UIAlertView alloc] initWithTitle:title
                                                message:body
                                               delegate:nil
                                      cancelButtonTitle:SenkoLocalizedText(@"OK")
                                      otherButtonTitles:nil];
    [a show];
    [a release];
}

- (void)editThemeId:(NSString *)tid {
    ThemeEditVC *vc = [[[ThemeEditVC alloc] initWithThemeId:tid] autorelease];
    [self.navigationController pushViewController:vc animated:YES];
}

/* a new theme starts as a saved copy of the active one, so the editor always
   works on stored colors and nothing can be lost half-edited */
- (void)createFromCurrent {
    NSMutableDictionary *draft = SenkoCustomDraftFromTheme(SenkoThemeCurrentId());
    NSString *tid = draft ? SenkoCustomSaveDraft(draft) : nil;
    if (!tid) {
        [self showMessage:SenkoLocalizedText(@"Could not create theme")
                     body:SenkoLocalizedText(@"The custom theme limit is reached.")];
        return;
    }
    [self reloadIds];
    [self editThemeId:tid];
}

- (void)importTheme {
    FileImportVC *files = [[[FileImportVC alloc]
        initWithPath:SenkoCustomExportDirectory() delegate:self] autorelease];
    files.title = SenkoLocalizedText(@"Import theme");
    [self.navigationController pushViewController:files animated:YES];
}

- (void)addTapped {
    SenkoThemeSfxPlay();
    UIActionSheet *sheet = [[UIActionSheet alloc]
             initWithTitle:SenkoLocalizedText(@"New theme")
                  delegate:self
         cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    destructiveButtonTitle:nil
         otherButtonTitles:SenkoLocalizedText(@"Copy current theme"),
                           SenkoLocalizedText(@"Import from Documents"), nil];
    [sheet showInView:self.view];
    [sheet release];
}

- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)idx {
    if (idx == sheet.cancelButtonIndex) return;
    if (idx == 0) [self createFromCurrent];
    else if (idx == 1) [self importTheme];
}

- (void)fileImportVCDidCancel:(FileImportVC *)vc {
    (void)vc;
    [self.navigationController popToViewController:self animated:YES];
}

- (void)fileImportVC:(FileImportVC *)vc didPickPath:(NSString *)path {
    (void)vc;
    [self.navigationController popToViewController:self animated:YES];
    NSString *err = nil;
    NSString *tid = SenkoCustomImportFile(path, &err);
    if (!tid) {
        [self showMessage:SenkoLocalizedText(@"Import failed")
                     body:SenkoLocalizedText(err ? err : @"Unknown error")];
        return;
    }
    [self reloadIds];
    [self showMessage:SenkoLocalizedText(@"Theme imported")
                 body:SenkoThemeDisplayName(tid)];
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
    NSString *tid = [_ids objectAtIndex:ip.row];
    NSString *blurb = SenkoLocalizedText(SenkoThemeBlurb(tid));
    CGFloat width = tv.bounds.size.width - 76.0f;
    if (width < 160.0f) width = 160.0f;
    CGSize size = SenkoTextSize(blurb, [UIFont systemFontOfSize:12.0f], width);
    if (size.height > 120.0f) size.height = 120.0f;
    return MAX(52.0f, 22.0f + size.height + 18.0f);
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
    cell.textLabel.lineBreakMode = NSLineBreakByClipping;
    cell.textLabel.adjustsFontSizeToFitWidth = YES;
    cell.textLabel.minimumFontSize = 11.0f;
    cell.detailTextLabel.lineBreakMode = NSLineBreakByClipping;
    cell.detailTextLabel.adjustsFontSizeToFitWidth = YES;
    cell.detailTextLabel.minimumFontSize = 9.0f;
    cell.detailTextLabel.numberOfLines = 0;
    cell.detailTextLabel.lineBreakMode = NSLineBreakByWordWrapping;
    BOOL on = [tid isEqualToString:SenkoThemeCurrentId()];
    if (SenkoCustomIsCustomId(tid))
        cell.accessoryType = UITableViewCellAccessoryDetailDisclosureButton;
    else
        cell.accessoryType = on ? UITableViewCellAccessoryCheckmark
                                : UITableViewCellAccessoryNone;
    SenkoStyleSelectableCell(cell);
    return cell;
}

- (void)tableView:(UITableView *)tv
        accessoryButtonTappedForRowWithIndexPath:(NSIndexPath *)ip {
    (void)tv;
    NSString *tid = [_ids objectAtIndex:ip.row];
    if (SenkoCustomIsCustomId(tid)) [self editThemeId:tid];
}

- (BOOL)tableView:(UITableView *)tv canEditRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv;
    return SenkoCustomIsCustomId([_ids objectAtIndex:ip.row]);
}

- (void)tableView:(UITableView *)tv
        commitEditingStyle:(UITableViewCellEditingStyle)style
         forRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv;
    if (style != UITableViewCellEditingStyleDelete) return;
    NSString *tid = [_ids objectAtIndex:ip.row];
    if (!SenkoCustomDelete(tid)) return;
    [self reloadIds];
}

- (void)applyThemeId:(NSString *)tid {
    if (![tid length]) return;
    if ([tid isEqualToString:SenkoThemeCurrentId()]) return;
    SenkoThemeApplyId(tid);
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    SenkoThemeSfxPlay();
    [self applyThemeId:[_ids objectAtIndex:ip.row]];
}

@end

@implementation ThemesVC {
    UITableView *_tv;
    NSArray *_groups;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    [_groups release];
    [super dealloc];
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

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Themes";
    _groups = [SenkoThemeGroupIds() retain];
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);
    self.view.backgroundColor = kBG;
    _tv = [[UITableView alloc] initWithFrame:self.view.bounds
                                       style:UITableViewStyleGrouped];
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
            lab.text = @"Senko-Boykisser: pink paper or rose ink, with falling boykissers on the home screen.";
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
                    [NSArray arrayWithObjects:SenkoLocalizedText(@"Dark"),
                     SenkoLocalizedText(@"Light"), nil]] autorelease];
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
    cell.textLabel.lineBreakMode = NSLineBreakByClipping;
    cell.textLabel.adjustsFontSizeToFitWidth = YES;
    cell.textLabel.minimumFontSize = 11.0f;
    cell.detailTextLabel.lineBreakMode = NSLineBreakByClipping;
    cell.detailTextLabel.adjustsFontSizeToFitWidth = YES;
    cell.detailTextLabel.minimumFontSize = 9.0f;
    cell.accessoryView = nil;
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    SenkoStyleSelectableCell(cell);
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
