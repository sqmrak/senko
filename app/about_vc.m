#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/message.h>
#include <sys/sysctl.h>
#import "ui_theme.h"
#import "app_common.h"
#import "crash_report.h"
#import "boykisser_field.h"

static NSString * const kAboutTitle = @"t";
static NSString * const kAboutValue = @"v";
static NSString * const kAboutGlyph = @"g";
static NSString * const kAboutImage = @"i";
static NSString * const kAboutAction = @"a";

static NSString * const kAboutThanks =
    @"@CookieValerka, @inraxx, @not_a_modder, @s3dativee, @Lineysom, @shizotoaster, "
    @"@fly1ngangel, @anazerka, @fr0n1k, @vladosik228, @kfrom7, @ningmeng";
static NSString * const kAboutSponsors = @"@s3dativee, @shizotoaster, @not_a_modder";
static NSString * const kAboutTesters =
    @"@inraxx, @s3dativee, @rafal_official, @QuaIcomm, @belo4kaFLUNI, "
    @"@Wolfer_QUIC, @koshdevv, @Lineysom, @Lime_iOS6, @ogeprint, @ra1n_developer";

/* cap the width so ipad landscape rows do not stretch across the screen */
static CGFloat AboutSideMargin(CGFloat width) {
    CGFloat m = floorf((width - 700.0f) * 0.5f);
    return m > 12.0f ? m : 12.0f;
}

/* ios 5 and 6 inset grouped rows themselves; from ios 7 they run edge to edge */
static BOOL AboutRowsInsetBySystem(void) {
    return ![UITableView instancesRespondToSelector:@selector(setSeparatorInset:)];
}

static CGFloat AboutRowInset(UITableView *tv) {
    if (!AboutRowsInsetBySystem()) return AboutSideMargin(tv.bounds.size.width);
    return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad
        ? 45.0f : 10.0f;
}

static NSString *AboutModel(void) {
    char machine[64];
    size_t len = sizeof machine;
    if (sysctlbyname("hw.machine", machine, &len, NULL, 0) != 0 || len == 0 ||
        len > sizeof machine)
        return [[UIDevice currentDevice] model];
    machine[sizeof machine - 1] = '\0';
    NSString *code = [NSString stringWithUTF8String:machine];
    return [code length] ? code : [[UIDevice currentDevice] model];
}

static NSString *AboutSlice(void) {
#if defined(__arm64__) || defined(__aarch64__)
    return @"arm64";
#else
    return @"armv7";
#endif
}

static UIFont *AboutTitleFont(void) { return SenkoFontBody(16.0f, NO); }
static UIFont *AboutValueFont(void) { return SenkoFontBody(14.0f, NO); }

static void AboutStylePlate(UIView *plate) {
    BOOL light = SenkoThemeIsLight();
    plate.layer.cornerRadius = SenkoThemeCardRadius();
    if (SenkoThemeIsIos26()) {
        plate.backgroundColor = [UIColor colorWithWhite:1 alpha:light ? 0.34f : 0.10f];
        plate.layer.borderWidth = 0.5f;
        plate.layer.borderColor = [UIColor colorWithWhite:1 alpha:light ? 0.55f : 0.22f].CGColor;
    } else {
        plate.backgroundColor = kCellHi;
        plate.layer.borderWidth = 0.0f;
    }
}

@interface SenkoAboutCell : UITableViewCell {
@public
    UIImageView *icon;
    UILabel     *titleLbl;
    UILabel     *valueLbl;
}
@end

@implementation SenkoAboutCell

- (id)initWithReuseIdentifier:(NSString *)rid {
    if ((self = [super initWithStyle:UITableViewCellStyleDefault reuseIdentifier:rid])) {
        icon = [[UIImageView alloc] initWithFrame:CGRectZero];
        icon.contentMode = UIViewContentModeScaleAspectFit;
        [self.contentView addSubview:icon];
        titleLbl = [[UILabel alloc] initWithFrame:CGRectZero];
        titleLbl.backgroundColor = [UIColor clearColor];
        titleLbl.lineBreakMode = NSLineBreakByTruncatingTail;
        [self.contentView addSubview:titleLbl];
        valueLbl = [[UILabel alloc] initWithFrame:CGRectZero];
        valueLbl.backgroundColor = [UIColor clearColor];
        valueLbl.numberOfLines = 0;
        valueLbl.lineBreakMode = NSLineBreakByWordWrapping;
        [self.contentView addSubview:valueLbl];
    }
    return self;
}

- (void)dealloc {
    [icon release];
    [titleLbl release];
    [valueLbl release];
    [super dealloc];
}

/* ios 7 and later draw grouped rows edge to edge; the cards need the margin */
- (void)setFrame:(CGRect)frame {
    if (!AboutRowsInsetBySystem() && frame.origin.x < 1.0f) {
        CGFloat m = AboutSideMargin(frame.size.width);
        frame = CGRectInset(frame, m, 0.0f);
    }
    [super setFrame:frame];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.contentView.bounds;
    CGFloat x = 16.0f;
    if (icon.image) {
        icon.frame = CGRectMake(16.0f, 14.0f, 24.0f, 24.0f);
        x = 52.0f;
    } else {
        icon.frame = CGRectZero;
    }
    CGFloat w = b.size.width - x - 12.0f;
    if (w < 40.0f) w = 40.0f;
    titleLbl.frame = CGRectMake(x, 12.0f, w, 20.0f);
    CGFloat valueH = b.size.height - 36.0f - 10.0f;
    valueLbl.frame = CGRectMake(x, 34.0f, w, valueH > 0.0f ? valueH : 0.0f);
}

@end

@interface SenkoAboutHero : UIView {
@public
    UIImageView *icon;
    UILabel     *name;
    UILabel     *tagline;
    UIView      *versionTile;
    UIView      *systemTile;
    UILabel     *versionCaption;
    UILabel     *versionValue;
    UILabel     *systemCaption;
    UILabel     *systemValue;
}
- (void)applyTheme;
@end

enum { kAboutHeroHeight = 262 };

static UILabel *AboutHeroLabel(UIView *host, UIFont *font) {
    UILabel *l = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    l.backgroundColor = [UIColor clearColor];
    l.font = font;
    l.textAlignment = NSTextAlignmentCenter;
    l.lineBreakMode = NSLineBreakByTruncatingTail;
    l.adjustsFontSizeToFitWidth = YES;
    [host addSubview:l];
    return l;
}

@implementation SenkoAboutHero

- (id)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.backgroundColor = [UIColor clearColor];
        NSString *path = [[NSBundle mainBundle] pathForResource:@"Icon-60@3x" ofType:@"png"];
        icon = [[UIImageView alloc] initWithImage:
                path ? [UIImage imageWithContentsOfFile:path] : nil];
        icon.contentMode = UIViewContentModeScaleAspectFill;
        icon.layer.masksToBounds = YES;
        icon.layer.cornerRadius = 20.0f;
        [self addSubview:icon];
        name = [AboutHeroLabel(self, SenkoFontDisplay(30.0f)) retain];
        name.text = @"Senko";
        tagline = [AboutHeroLabel(self, SenkoFontBody(14.0f, NO)) retain];

        versionTile = [[UIView alloc] initWithFrame:CGRectZero];
        systemTile = [[UIView alloc] initWithFrame:CGRectZero];
        [self addSubview:versionTile];
        [self addSubview:systemTile];
        versionCaption = [AboutHeroLabel(versionTile, SenkoFontBody(12.0f, NO)) retain];
        versionValue = [AboutHeroLabel(versionTile, SenkoFontBody(17.0f, YES)) retain];
        systemCaption = [AboutHeroLabel(systemTile, SenkoFontBody(12.0f, NO)) retain];
        systemValue = [AboutHeroLabel(systemTile, SenkoFontBody(17.0f, YES)) retain];
        versionValue.text = [SENKO_VERSION hasPrefix:@"v"]
            ? [SENKO_VERSION substringFromIndex:1] : SENKO_VERSION;
        systemCaption.text = @"iOS";
        systemValue.text = [NSString stringWithFormat:@"%@ · %@",
                            [[UIDevice currentDevice] systemVersion], AboutSlice()];
    }
    return self;
}

- (void)dealloc {
    [icon release];
    [name release];
    [tagline release];
    [versionTile release];
    [systemTile release];
    [versionCaption release];
    [versionValue release];
    [systemCaption release];
    [systemValue release];
    [super dealloc];
}

- (void)applyTheme {
    name.textColor = kInk;
    tagline.textColor = kInkMuted;
    versionCaption.textColor = kInkMuted;
    systemCaption.textColor = kInkMuted;
    versionValue.textColor = kInk;
    systemValue.textColor = kInk;
    AboutStylePlate(versionTile);
    AboutStylePlate(systemTile);
    tagline.text = SenkoLocalizedText(@"Amnezia/VLESS Client");
    versionCaption.text = SenkoLocalizedText(@"Version");
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat w = self.bounds.size.width;
    CGFloat side = AboutSideMargin(w);
    if (AboutRowsInsetBySystem())
        side = [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad
            ? 45.0f : 10.0f;
    icon.frame = CGRectMake(floorf((w - 88.0f) * 0.5f), 22.0f, 88.0f, 88.0f);
    name.frame = CGRectMake(side, 118.0f, w - side * 2.0f, 38.0f);
    tagline.frame = CGRectMake(side, 156.0f, w - side * 2.0f, 18.0f);
    CGFloat gap = 10.0f;
    CGFloat tileW = floorf((w - side * 2.0f - gap) * 0.5f);
    versionTile.frame = CGRectMake(side, 188.0f, tileW, 66.0f);
    systemTile.frame = CGRectMake(w - side - tileW, 188.0f, tileW, 66.0f);
    for (UIView *tile in [NSArray arrayWithObjects:versionTile, systemTile, nil]) {
        UILabel *caption = tile == versionTile ? versionCaption : systemCaption;
        UILabel *value = tile == versionTile ? versionValue : systemValue;
        caption.frame = CGRectMake(8.0f, 11.0f, tileW - 16.0f, 16.0f);
        value.frame = CGRectMake(8.0f, 29.0f, tileW - 16.0f, 24.0f);
    }
}

@end

@interface AboutVC () <UITableViewDataSource, UITableViewDelegate>
@end

@implementation AboutVC {
    UITableView *_tv;
    SenkoAboutHero *_hero;
    SenkoBoykisserField *_field;
    NSArray *_sections;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"About");
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setAutomaticallyAdjustsScrollViewInsets:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setAutomaticallyAdjustsScrollViewInsets:), NO);
    SenkoApplyScreenChrome(self.view);

    CGRect b = SenkoViewBounds(self.view);
/* the plates are opaque, so the sprites show only between and above them */
    _field = [[SenkoBoykisserField alloc] initWithFrame:b spriteName:@"senko-fall"];
    _field.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:_field];

    _tv = [[UITableView alloc] initWithFrame:b style:UITableViewStyleGrouped];
    SenkoScrollViewUseManualInsets(_tv);
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.separatorStyle = UITableViewCellSeparatorStyleNone;
    _tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    SenkoClearTableBackground(_tv);
    [self.view addSubview:_tv];

    _hero = [[SenkoAboutHero alloc] initWithFrame:
             CGRectMake(0, 0, b.size.width, kAboutHeroHeight)];
/* five taps on the version open the developer section, like android's build number */
    _hero->versionTile.userInteractionEnabled = YES;
    [_hero->versionTile addGestureRecognizer:
        [[[UITapGestureRecognizer alloc] initWithTarget:self
                                                 action:@selector(versionTapped)] autorelease]];
    [_hero applyTheme];
    _tv.tableHeaderView = _hero;

    [self rebuildRows];

    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:self selector:@selector(languageDidChange:)
               name:SenkoLanguageDidChangeNotification object:nil];
    [nc addObserver:self selector:@selector(themeDidChange:)
               name:SenkoThemeDidChangeNotification object:nil];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    [_hero release];
/* the display link retains the field; stop it before the release */
    [_field stop];
    [_field release];
    [_sections release];
    [super dealloc];
}

- (NSDictionary *)row:(NSString *)title value:(NSString *)value
                glyph:(NSString *)glyph action:(SEL)action {
    NSMutableDictionary *row = [NSMutableDictionary dictionaryWithCapacity:4];
    [row setObject:SenkoLocalizedText(title) forKey:kAboutTitle];
    [row setObject:value ? value : @"" forKey:kAboutValue];
    if (glyph) [row setObject:glyph forKey:kAboutGlyph];
    if (action) [row setObject:NSStringFromSelector(action) forKey:kAboutAction];
    return row;
}

- (void)rebuildRows {
    NSDictionary *developer = [self row:@"Developer" value:@"sqmrak" glyph:nil action:NULL];
    NSString *avatar = [[NSBundle mainBundle] pathForResource:@"sqmrak" ofType:@"jpg"];
    UIImage *face = avatar ? [UIImage imageWithContentsOfFile:avatar] : nil;
    if (face) {
        NSMutableDictionary *withFace = [[developer mutableCopy] autorelease];
        [withFace setObject:face forKey:kAboutImage];
        developer = withFace;
    }
    NSArray *sections = [NSArray arrayWithObjects:
        [NSDictionary dictionaryWithObjectsAndKeys:
            SenkoLocalizedText(@"Device"), kAboutTitle,
            [NSArray arrayWithObjects:
                [self row:@"Model" value:AboutModel() glyph:@"glyph-info.png" action:NULL],
                [self row:@"iOS version" value:[[UIDevice currentDevice] systemVersion]
                    glyph:@"glyph-gear.png" action:NULL],
                [self row:@"Architecture" value:AboutSlice() glyph:@"glyph-hash.png" action:NULL],
                [self row:@"TLS mode" value:SenkoAboutTLSMode() glyph:@"glyph-shield.png" action:NULL],
                nil], kAboutValue, nil],
        [NSDictionary dictionaryWithObjectsAndKeys:
            @"Senko", kAboutTitle,
            [NSArray arrayWithObjects:
                [self row:@"How it works"
                    value:SenkoLocalizedText(@"Apps and system traffic use the selected profile. "
                                             @"Routing needs root, and a jailbreak already provides it.")
                    glyph:@"glyph-globe.png" action:NULL],
                [self row:@"Transports"
                    value:@"TCP · TLS · REALITY + Vision\nWebSocket · XHTTP · gRPC\n"
                          @"AmneziaWG · SOCKS5 · HTTP(S) CONNECT"
                    glyph:@"glyph-split.png" action:NULL],
                [self row:@"Compatibility" value:@"iOS 5-16 · armv7 + arm64 + arm64e"
                    glyph:@"glyph-repeat.png" action:NULL],
                [self row:@"Security"
                    value:SenkoLocalizedText(@"Token-authenticated control socket · subscription SSRF "
                                             @"protection · secret redaction · real transport checks.")
                    glyph:@"glyph-shield.png" action:NULL],
                [self row:@"System log" value:@"/var/log/senko-system.log"
                    glyph:@"glyph-logs.png" action:NULL],
                nil], kAboutValue, nil],
        [NSDictionary dictionaryWithObjectsAndKeys:
            SenkoLocalizedText(@"Links"), kAboutTitle,
            [NSArray arrayWithObjects:
                [self row:@"GitHub" value:@"github.com/sqmrak/Senko"
                    glyph:@"glyph-code.png" action:@selector(githubPressed)],
                [self row:@"Telegram" value:@"t.me/sqmrakdev"
                    glyph:@"glyph-globe.png" action:@selector(telegramPressed)],
                nil], kAboutValue, nil],
        [NSDictionary dictionaryWithObjectsAndKeys:
            SenkoLocalizedText(@"Credits"), kAboutTitle,
            [NSArray arrayWithObjects:
                developer,
                [self row:@"Special thanks" value:kAboutThanks glyph:@"glyph-bolt.png" action:NULL],
                [self row:@"Sponsors" value:kAboutSponsors glyph:@"glyph-rocket.png" action:NULL],
                [self row:@"Testers" value:kAboutTesters glyph:@"glyph-search.png" action:NULL],
                [self row:@"Emoji artwork"
                    value:SenkoLocalizedText(@"Twemoji by Twitter, Inc. and contributors (CC BY 4.0)")
                    glyph:@"glyph-palette.png" action:NULL],
                nil], kAboutValue, nil],
        nil];
    [_sections release];
    _sections = [sections retain];
    [_tv reloadData];
}

- (NSDictionary *)rowAtIndexPath:(NSIndexPath *)ip {
    NSArray *rows = [[_sections objectAtIndex:ip.section] objectForKey:kAboutValue];
    return [rows objectAtIndex:ip.row];
}

- (void)languageDidChange:(NSNotification *)n {
    (void)n;
    self.title = SenkoLocalizedText(@"About");
    [_hero applyTheme];
    [self rebuildRows];
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    SenkoApplyScreenChrome(self.view);
    SenkoClearTableBackground(_tv);
    [_hero applyTheme];
    [self rebuildRows];
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("about");
    [super viewWillAppear:animated];
    SenkoApplyScreenChrome(self.view);
    [_field start];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [_field stop];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGRect b = SenkoViewBounds(self.view);
    _tv.frame = b;
    _field.frame = b;
    UIEdgeInsets safe = SenkoSafeAreaInsets(self.view);
    _tv.contentInset = UIEdgeInsetsMake(0.0f, 0.0f, safe.bottom + 16.0f, 0.0f);
    _tv.scrollIndicatorInsets = _tv.contentInset;
/* uitableview only rereads the header height when the view is set again */
    if (fabsf((float)(_hero.frame.size.width - b.size.width)) > 0.5f) {
        _hero.frame = CGRectMake(0, 0, b.size.width, kAboutHeroHeight);
        _tv.tableHeaderView = _hero;
        [_tv reloadData];
    }
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return (NSInteger)[_sections count];
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    return (NSInteger)[[[_sections objectAtIndex:s] objectForKey:kAboutValue] count];
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    NSDictionary *row = [self rowAtIndexPath:ip];
    NSString *value = [row objectForKey:kAboutValue];
    if (![value length]) return 52.0f;
    BOOL leading = [row objectForKey:kAboutGlyph] || [row objectForKey:kAboutImage];
    CGFloat w = tv.bounds.size.width - AboutRowInset(tv) * 2.0f -
                (leading ? 52.0f : 16.0f) - 12.0f -
                ([row objectForKey:kAboutAction] ? 34.0f : 0.0f);
    if (w < 40.0f) w = 40.0f;
    CGFloat h = SenkoTextSize(value, AboutValueFont(), w).height;
    if (h > 400.0f) h = 400.0f;
    return MAX(56.0f, ceilf(h) + 36.0f + 12.0f);
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 36.0f;
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 4.0f;
}

/* ios 14 header views reset their text colour, so the label is our own */
- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    CGFloat w = tv.bounds.size.width;
    UIView *wrap = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, w, 36.0f)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    CGFloat x = AboutRowInset(tv) + 16.0f;
    UILabel *label = [[[UILabel alloc] initWithFrame:
                       CGRectMake(x, 12.0f, w - x * 2.0f, 20.0f)] autorelease];
    label.backgroundColor = [UIColor clearColor];
    label.font = SenkoFontBody(13.0f, YES);
    label.textColor = kInkMuted;
    label.text = [[_sections objectAtIndex:s] objectForKey:kAboutTitle];
    [wrap addSubview:label];
    return wrap;
}

- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)s {
    (void)tv; (void)s;
    return [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"about";
    SenkoAboutCell *cell = (SenkoAboutCell *)[tv dequeueReusableCellWithIdentifier:cid];
    if (!cell) cell = [[[SenkoAboutCell alloc] initWithReuseIdentifier:cid] autorelease];
    NSDictionary *row = [self rowAtIndexPath:ip];
    BOOL tappable = [row objectForKey:kAboutAction] != nil;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.accessoryType = tappable ? UITableViewCellAccessoryDisclosureIndicator
                                  : UITableViewCellAccessoryNone;
    if (tappable) SenkoStyleSelectableCell(cell);
    SenkoStyleGroupCell(cell, ip, [self tableView:tv numberOfRowsInSection:ip.section]);

    cell->titleLbl.font = AboutTitleFont();
    cell->titleLbl.textColor = kInk;
    cell->titleLbl.text = [row objectForKey:kAboutTitle];
    cell->valueLbl.font = AboutValueFont();
    cell->valueLbl.textColor = kInkMuted;
    cell->valueLbl.text = [row objectForKey:kAboutValue];

    UIImage *image = [row objectForKey:kAboutImage];
    NSString *glyph = [row objectForKey:kAboutGlyph];
    cell->icon.image = image ? image : glyph ? TintedIconNamed(glyph, 22.0f, kInk) : nil;
    BOOL face = image && !glyph && ![[row objectForKey:kAboutAction] length];
    cell->icon.layer.cornerRadius = face ? 12.0f : 0.0f;
    cell->icon.layer.masksToBounds = face;
    [cell setNeedsLayout];
    return cell;
}

- (void)tableView:(UITableView *)tv willDisplayCell:(UITableViewCell *)cell
 forRowAtIndexPath:(NSIndexPath *)ip {
    SenkoStyleGroupCell(cell, ip, [self tableView:tv numberOfRowsInSection:ip.section]);
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    NSString *action = [[self rowAtIndexPath:ip] objectForKey:kAboutAction];
    if ([action length]) [self performSelector:NSSelectorFromString(action)];
}

- (void)dismissHint:(UIAlertView *)hint {
    [hint dismissWithClickedButtonIndex:0 animated:YES];
}

- (void)versionTapped {
    if (SenkoDevMenuEnabled()) return; /* the section turns itself off */
    int left = SenkoDevMenuTapsLeft();
    if (left > 0) {
/* silence until the third tap, so an accidental double tap says nothing */
        if (left > 2) return;
        UIAlertView *hint = [[[UIAlertView alloc]
            initWithTitle:nil
                  message:[NSString stringWithFormat:
                           SenkoLocalizedText(@"%d more"), left]
                 delegate:nil
        cancelButtonTitle:nil
        otherButtonTitles:nil] autorelease];
        [hint show];
/* dismissWithClickedButtonIndex:animated: takes two scalars, which
   performSelector:withObject: cannot pass, so the delay goes through a method
   that takes the alert itself */
        [self performSelector:@selector(dismissHint:) withObject:hint afterDelay:0.6];
        return;
    }
    SenkoSetDevMenuEnabled(YES);
    UIAlertView *done = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Developer settings")
              message:SenkoLocalizedText(@"Developer settings are now visible in Settings. Tap the section heading five times to hide them again.")
             delegate:nil
    cancelButtonTitle:@"OK"
    otherButtonTitles:nil] autorelease];
    [done show];
}

- (void)githubPressed {
    NSURL *url = [NSURL URLWithString:@"https://github.com/sqmrak/Senko"];
    if (url) [[UIApplication sharedApplication] openURL:url];
}

- (void)telegramPressed {
    NSURL *url = [NSURL URLWithString:@"https://t.me/sqmrakdev"];
    if (url) [[UIApplication sharedApplication] openURL:url];
}

@end
