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

enum { kSenkoSettingsSide = 10 };

@interface SettingsVC () <UITableViewDataSource, UITableViewDelegate,
                          EditServerDelegate, FileImportDelegate>
@end

@interface SenkoSettingsHeaderPlate : UIView {
    CAGradientLayer *_grad;
    UIView *_shine;
    UILabel *_title;
}
- (void)setGradColors:(NSArray *)colors title:(NSString *)title;
@end

@implementation SenkoSettingsHeaderPlate

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    self.opaque = YES;
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    _grad = [[CAGradientLayer layer] retain];
    _grad.masksToBounds = YES;
    [self.layer insertSublayer:_grad atIndex:0];
    _shine = [[UIView alloc] initWithFrame:CGRectZero];
    _shine.userInteractionEnabled = NO;
    _shine.backgroundColor = [UIColor colorWithWhite:1 alpha:0.22f];
    [self addSubview:_shine];
    _title = [[UILabel alloc] initWithFrame:CGRectZero];
    _title.backgroundColor = [UIColor clearColor];
    _title.font = [UIFont boldSystemFontOfSize:15];
    _title.lineBreakMode = NSLineBreakByTruncatingTail;
    _title.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [self addSubview:_title];
    return self;
}

- (void)dealloc {
    [_grad release];
    [_shine release];
    [_title release];
    [super dealloc];
}

- (void)setGradColors:(NSArray *)colors title:(NSString *)title {
    _grad.colors = colors;
    _title.text = title;
    if (SenkoThemeIsBoykisser() || SenkoThemeIsFrutigeraero() ||
        (SenkoThemeIsFlat() && SenkoThemeIsLight())) {
        _title.textColor = kInk;
        _title.shadowColor = [UIColor colorWithWhite:1 alpha:0.55f];
        _title.shadowOffset = CGSizeMake(0, 1);
    } else {
/* emboss on dark chrome */
        _title.textColor = [UIColor colorWithRed:1.00 green:0.94 blue:0.86 alpha:1.0];
        _title.shadowColor = [UIColor colorWithWhite:0 alpha:0.90f];
        _title.shadowOffset = CGSizeMake(0, 1);
    }
    [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    CGFloat r = self.layer.cornerRadius;
    _grad.frame = b;
    _grad.cornerRadius = r;
    _shine.frame = CGRectMake(1, 1, MAX(0, b.size.width - 2), 1);
    _title.frame = CGRectMake(14, 3, MAX(0, b.size.width - 28), 20);
}

@end

/* settings row card; keep mask rebuild rare (scroll/reuse cost) */
@interface SenkoSettingsGroupBg : UIView {
    UIView *_fill;
    UIView *_sep;
    CAShapeLayer *_mask;
    BOOL _first;
    BOOL _last;
    BOOL _showSep;
    CGSize _laidSize;
    int _laidCornerKey;
}
- (void)configureFirst:(BOOL)first last:(BOOL)last showSep:(BOOL)showSep;
@end

@implementation SenkoSettingsGroupBg

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    self.backgroundColor = [UIColor clearColor];
    self.opaque = NO;
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _fill = [[UIView alloc] initWithFrame:CGRectZero];
    _fill.backgroundColor = kCellHi;
/* opaque=yes kills alpha on ios26 glass cells */
    _fill.opaque = !SenkoThemeIsIos26();
    _fill.clipsToBounds = YES;
    [self addSubview:_fill];
    _sep = [[UIView alloc] initWithFrame:CGRectZero];
    _sep.userInteractionEnabled = NO;
    _sep.hidden = YES;
    [self addSubview:_sep];
    _mask = [[CAShapeLayer layer] retain];
    _laidCornerKey = -1;
    return self;
}

- (void)dealloc {
    [_fill release];
    [_sep release];
    [_mask release];
    [super dealloc];
}

- (void)configureFirst:(BOOL)first last:(BOOL)last showSep:(BOOL)showSep {
    BOOL dirty = (_first != first) || (_last != last) || (_showSep != showSep);
    _first = first;
    _last = last;
    _showSep = showSep;
    _fill.backgroundColor = kCellHi;
    if (dirty)
        [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    if (b.size.width < 2.0f || b.size.height < 2.0f)
        return;

/* match daemon/utilities plate width exactly. grouped cells can already be inset */
    CGFloat side = (CGFloat)kSenkoSettingsSide;
    CGFloat radius = 10.0f;
    CGFloat fillX = 0.0f;
    CGFloat fillW = b.size.width;

    UIView *walk = self.superview;
    UITableView *tv = nil;
    while (walk) {
        if ([walk isKindOfClass:[UITableView class]]) {
            tv = (UITableView *)walk;
            break;
        }
        walk = walk.superview;
    }
    if (tv && tv.bounds.size.width > 2.0f) {
        CGRect plateInTable = CGRectMake(side, 0,
                                         tv.bounds.size.width - side * 2.0f,
                                         b.size.height);
        CGRect plateInSelf = [self convertRect:plateInTable fromView:tv];
        fillX = plateInSelf.origin.x;
        fillW = plateInSelf.size.width;
/* clamp into own bounds so we never paint past the cell */
        if (fillX < 0.0f) {
            fillW += fillX;
            fillX = 0.0f;
        }
        if (fillX + fillW > b.size.width)
            fillW = b.size.width - fillX;
        if (fillW < 1.0f) {
            fillX = 0.0f;
            fillW = b.size.width;
        }
    }

    _fill.frame = CGRectMake(fillX, 0, fillW, b.size.height);
    if (SenkoThemeIsIos26()) {
        BOOL light = SenkoThemeIsLight();
        _fill.opaque = NO;
        _fill.backgroundColor = light
            ? [UIColor colorWithWhite:1.0 alpha:0.42]
            : [UIColor colorWithWhite:1.0 alpha:0.12];
    } else {
        _fill.opaque = YES;
        _fill.backgroundColor = kCellHi;
    }

    int cornerKey = (_first ? 1 : 0) | (_last ? 2 : 0);
    BOOL sizeDirty = !CGSizeEqualToSize(_laidSize, b.size);
    BOOL cornerDirty = (_laidCornerKey != cornerKey);
    if (sizeDirty || cornerDirty) {
        _laidSize = b.size;
        _laidCornerKey = cornerKey;
        UIRectCorner corners = 0;
        if (_first && _last)
            corners = UIRectCornerAllCorners;
        else if (_first)
            corners = UIRectCornerTopLeft | UIRectCornerTopRight;
        else if (_last)
            corners = UIRectCornerBottomLeft | UIRectCornerBottomRight;
        if (corners != 0) {
            UIBezierPath *path =
                [UIBezierPath bezierPathWithRoundedRect:_fill.bounds
                                      byRoundingCorners:corners
                                            cornerRadii:CGSizeMake(radius, radius)];
            _mask.frame = _fill.bounds;
            _mask.path = path.CGPath;
            _fill.layer.mask = _mask;
        } else {
            _fill.layer.mask = nil;
        }
    }

    if (_showSep && !_last) {
        CGFloat lineH = 1.0f / MAX(1.0f, [UIScreen mainScreen].scale);
        if (lineH < 0.5f) lineH = 0.5f;
        CGFloat pad = 12.0f;
        if (pad >= fillW) pad = 0.0f;
        _sep.hidden = NO;
        _sep.frame = CGRectMake(fillX + pad, b.size.height - lineH,
                                fillW - pad, lineH);
        _sep.backgroundColor = SenkoThemeIsLight()
            ? [UIColor colorWithWhite:0 alpha:0.16]
            : [UIColor colorWithWhite:1 alpha:0.22];
    } else {
        _sep.hidden = YES;
    }
}

@end

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
    _tv.backgroundColor = [UIColor clearColor];
/* solid cell bg kills system seps on ios6 */
    _tv.separatorStyle = UITableViewCellSeparatorStyleNone;
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
    _tv.backgroundColor = [UIColor clearColor];
    _tv.separatorStyle = UITableViewCellSeparatorStyleNone;
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
        [_tv reloadData];
    }];
}

- (void)layoutSettings {
    CGRect b = SenkoViewBounds(self.view);
    UIView *bg = [self.view viewWithTag:9111];
    if (bg) bg.frame = b;
    _tv.frame = CGRectMake(0, 0, b.size.width, b.size.height);
    _tv.backgroundColor = [UIColor clearColor];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutSettings];
}

- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)io
                                         duration:(NSTimeInterval)dur {
    (void)io; (void)dur;
    [self layoutSettings];
    [_tv reloadData];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io {
    (void)io;
    [self layoutSettings];
    [_tv reloadData];
}

- (void)donePressed {
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv { return 2; }

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    if (s == 0) return 4;
    return 5; /* hide, themes, logs, update, about */
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
/* skeuo chrome bar; gradient resizes in senkosettingsheaderplate */
    static NSString *titles[] = { @"DAEMON", @"UTILITIES" };
    if (s < 0 || s > 1) return nil;

    CGFloat w = SenkoViewBounds(self.view).size.width;
    if (w < 160.0f) w = tv.bounds.size.width;
    if (w < 160.0f) w = 160.0f;
    CGFloat wrapH = 34.0f;
    CGFloat plateH = 26.0f;
    CGFloat plateY = 5.0f;
    CGFloat plateR = 7.0f;
    CGFloat side = (CGFloat)kSenkoSettingsSide;
    CGFloat plateW = w - side * 2.0f;

    UIView *wrap = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, w, wrapH)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    wrap.clipsToBounds = NO;
    wrap.autoresizingMask = UIViewAutoresizingFlexibleWidth;

    SenkoSettingsHeaderPlate *plate =
        [[[SenkoSettingsHeaderPlate alloc]
          initWithFrame:CGRectMake(side, plateY, plateW, plateH)] autorelease];
    plate.layer.cornerRadius = plateR;
    plate.layer.masksToBounds = NO;
    plate.layer.borderWidth = 1.0f;
    plate.layer.borderColor = [kAccentBlue colorWithAlphaComponent:0.45f].CGColor;
    plate.layer.shadowColor = [UIColor blackColor].CGColor;
    plate.layer.shadowOffset = CGSizeMake(0, 2);
    plate.layer.shadowRadius = 2;
    plate.layer.shadowOpacity = SenkoThemeIsLight() ? 0.30f : 0.70f;
    plate.backgroundColor = [UIColor colorWithRed:0.12 green:0.08 blue:0.04 alpha:1.0];

    NSArray *cols = nil;
    if (SenkoThemeIsMiside()) {
        cols = [NSArray arrayWithObjects:
                (id)[UIColor colorWithRed:0.32 green:0.14 blue:0.30 alpha:1].CGColor,
                (id)[UIColor colorWithRed:0.14 green:0.06 blue:0.16 alpha:1].CGColor, nil];
    } else if (SenkoThemeIsBoykisser()) {
        cols = [NSArray arrayWithObjects:
                (id)[UIColor colorWithRed:1.00 green:0.78 blue:0.88 alpha:1].CGColor,
                (id)[UIColor colorWithRed:0.95 green:0.55 blue:0.72 alpha:1].CGColor, nil];
    } else if (SenkoThemeIsFrutigeraero()) {
        cols = [NSArray arrayWithObjects:
                (id)[UIColor colorWithRed:0.55 green:0.88 blue:1.00 alpha:1].CGColor,
                (id)[UIColor colorWithRed:0.20 green:0.68 blue:0.92 alpha:1].CGColor, nil];
    } else if (SenkoThemeIsFlat()) {
        UIColor *hi = SenkoThemeIsLight()
            ? [UIColor colorWithRed:0.92 green:0.93 blue:0.96 alpha:1]
            : [UIColor colorWithRed:0.22 green:0.24 blue:0.30 alpha:1];
        UIColor *lo = SenkoThemeIsLight()
            ? [UIColor colorWithRed:0.82 green:0.84 blue:0.90 alpha:1]
            : [UIColor colorWithRed:0.12 green:0.13 blue:0.18 alpha:1];
        cols = [NSArray arrayWithObjects:(id)hi.CGColor, (id)lo.CGColor, nil];
    } else {
        cols = [NSArray arrayWithObjects:
                (id)[UIColor colorWithRed:0.34 green:0.20 blue:0.08 alpha:1].CGColor,
                (id)[UIColor colorWithRed:0.12 green:0.08 blue:0.04 alpha:1].CGColor, nil];
    }
    [plate setGradColors:cols title:titles[s]];
    [wrap addSubview:plate];
    return wrap;
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 34;
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    (void)tv;
    return (s == 0) ? 36.0f : 10.0f;
}

- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)s {
    if (s != 0) {
        UIView *v = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, 1, 10)] autorelease];
        v.backgroundColor = [UIColor clearColor];
        return v;
    }
/* default is localhost; people flip socks_public in cfg without noticing lan risk */
    CGFloat w = tv.bounds.size.width;
    if (w < 160.0f) w = 320.0f;
    UIView *wrap = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, w, 36)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    UILabel *lab = [[[UILabel alloc] initWithFrame:CGRectMake(14, 2, w - 28, 30)] autorelease];
    lab.backgroundColor = [UIColor clearColor];
    lab.numberOfLines = 2;
    lab.font = [UIFont systemFontOfSize:11];
    lab.textColor = [UIColor colorWithWhite:0.45 alpha:1.0];
    lab.text = @"SOCKS is localhost-only by default. socks_public=1 in config opens it to the LAN.";
    [wrap addSubview:lab];
    return wrap;
}

- (void)tableView:(UITableView *)tv willDisplayCell:(UITableViewCell *)cell
forRowAtIndexPath:(NSIndexPath *)ip {
    cell.backgroundColor = [UIColor clearColor];
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];

    NSInteger n = [tv numberOfRowsInSection:ip.section];
    BOOL first = (ip.row == 0);
    BOOL last = (ip.row == n - 1);

    SenkoSettingsGroupBg *bg = nil;
    if ([cell.backgroundView isKindOfClass:[SenkoSettingsGroupBg class]])
        bg = (SenkoSettingsGroupBg *)cell.backgroundView;
    else {
        bg = [[[SenkoSettingsGroupBg alloc] initWithFrame:cell.bounds] autorelease];
        cell.backgroundView = bg;
    }
    if (!CGRectEqualToRect(bg.frame, cell.bounds))
        bg.frame = cell.bounds;
    [bg configureFirst:first last:last showSep:!last];
    cell.selectedBackgroundView = nil;
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
/* pop whole file-browser stack back to settings */
    [self.navigationController popToViewController:self animated:YES];
}

- (void)presentUpdateInstallForPath:(NSString *)path {
    if (![path length]) return;
    NSString *pkg = [[path copy] autorelease];
    UINavigationController *nav = self.navigationController;
    void (^showInstall)(void) = ^{
/* present from the settings nav (or self), never from main while settings is already the active modal - ios */
        UIViewController *host = nav ? (UIViewController *)nav : (UIViewController *)self;
        if (host.presentedViewController) {
/* something already up; fall back to self */
            host = self;
        }
        UpdateInstallVC *uvc = [[[UpdateInstallVC alloc] initWithControl:_ctl
                                                             packagePath:pkg] autorelease];
        uvc.modalTransitionStyle = UIModalTransitionStyleCoverVertical;
        uvc.modalPresentationStyle = UIModalPresentationFullScreen;
        [host presentViewController:uvc animated:YES completion:nil];
    };
    if (nav && nav.topViewController != self) {
/* wait for pop animation; presenting mid-transition fails on ios 6 */
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
        [_ctl listCatalog:^(NSArray *servers, NSArray *subs) {
            (void)subs;
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

