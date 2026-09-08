#import "theme_edit_vc.h"
#import "ui_theme.h"
#import "app_common.h"
#import "theme/theme_custom.h"
#include <objc/message.h>

/* one palette slot on r/g/b/a sliders; edits land in the draft immediately */
@interface ThemeColorVC : UIViewController <UITextFieldDelegate> {
    NSMutableDictionary *_draft;
    NSString *_themeId;
    SenkoThemeSlot _slot;
    BOOL _light;
    UIView *_preview;
    UITextField *_hex;
    UISlider *_sliders[4];
    UILabel *_values[4];
}
- (id)initWithDraft:(NSMutableDictionary *)draft themeId:(NSString *)themeId
               slot:(SenkoThemeSlot)slot light:(BOOL)light;
@end

@implementation ThemeColorVC

static NSString * const kChannelNames[4] = { @"R", @"G", @"B", @"A" };

- (id)initWithDraft:(NSMutableDictionary *)draft themeId:(NSString *)themeId
               slot:(SenkoThemeSlot)slot light:(BOOL)light {
    if ((self = [super init])) {
        _draft = [draft retain];
        _themeId = [themeId copy];
        _slot = slot;
        _light = light;
    }
    return self;
}

- (void)dealloc {
    [_draft release];
    [_themeId release];
    [super dealloc];
}

/* these are borrowed from the view hierarchy; on ios 5 an unload frees them and
   the ivars must not keep pointing into freed views */
- (void)viewDidUnload {
    _preview = nil;
    _hex = nil;
    for (int i = 0; i < 4; ++i) {
        _sliders[i] = nil;
        _values[i] = nil;
    }
    [super viewDidUnload];
}

- (void)loadValuesFromColor:(UIColor *)color {
    CGFloat comps[4] = { 0, 0, 0, 1 };
    NSString *hex = SenkoHexFromColor(color);
    UIColor *parsed = SenkoColorFromHex(hex);
    if ([parsed respondsToSelector:@selector(getRed:green:blue:alpha:)])
        [parsed getRed:&comps[0] green:&comps[1] blue:&comps[2] alpha:&comps[3]];
    for (int i = 0; i < 4; ++i) {
        _sliders[i].value = (float)comps[i];
        _values[i].text = [NSString stringWithFormat:@"%d", (int)(comps[i] * 255.0f + 0.5f)];
    }
    _preview.backgroundColor = parsed;
    _hex.text = hex;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(SenkoThemeSlotTitle(_slot));
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    self.view.backgroundColor = kBG;

    CGFloat top = 20.0f;
    CGFloat width = self.view.bounds.size.width;

    _preview = [[[UIView alloc] initWithFrame:CGRectMake(16, top, width - 32, 64)] autorelease];
    _preview.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    _preview.layer.cornerRadius = SenkoThemeCardRadius();
    _preview.layer.borderWidth = 1.0f;
    _preview.layer.borderColor = [[UIColor colorWithWhite:0.5f alpha:0.4f] CGColor];
    [self.view addSubview:_preview];
    top += 76.0f;

    _hex = [[[UITextField alloc] initWithFrame:CGRectMake(16, top, width - 32, 34)] autorelease];
    _hex.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    _hex.borderStyle = UITextBorderStyleRoundedRect;
    _hex.autocapitalizationType = UITextAutocapitalizationTypeAllCharacters;
    _hex.autocorrectionType = UITextAutocorrectionTypeNo;
    _hex.clearButtonMode = UITextFieldViewModeWhileEditing;
    _hex.delegate = self;
    _hex.placeholder = @"RRGGBBAA";
    [self.view addSubview:_hex];
    top += 46.0f;

    for (int i = 0; i < 4; ++i) {
        UILabel *tag = [[[UILabel alloc]
            initWithFrame:CGRectMake(16, top, 22, 28)] autorelease];
        tag.text = kChannelNames[i];
        tag.backgroundColor = [UIColor clearColor];
        tag.font = [UIFont boldSystemFontOfSize:14.0f];
        SenkoStyleInkLabel(tag);
        [self.view addSubview:tag];

        _values[i] = [[[UILabel alloc]
            initWithFrame:CGRectMake(width - 60, top, 44, 28)] autorelease];
        _values[i].backgroundColor = [UIColor clearColor];
        _values[i].textAlignment = NSTextAlignmentRight;
        _values[i].font = [UIFont systemFontOfSize:13.0f];
        _values[i].autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
        SenkoStyleMutedLabel(_values[i]);
        [self.view addSubview:_values[i]];

        _sliders[i] = [[[UISlider alloc]
            initWithFrame:CGRectMake(44, top, width - 112, 28)] autorelease];
        _sliders[i].autoresizingMask = UIViewAutoresizingFlexibleWidth;
        _sliders[i].minimumValue = 0.0f;
        _sliders[i].maximumValue = 1.0f;
        _sliders[i].tag = i;
        [_sliders[i] addTarget:self action:@selector(sliderMoved:)
              forControlEvents:UIControlEventValueChanged];
        /* a full save rebuilds the theme registry and repaints every window, so
           dragging only previews and the value is stored on release */
        [_sliders[i] addTarget:self action:@selector(sliderReleased:)
              forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                               UIControlEventTouchCancel];
        [self.view addSubview:_sliders[i]];
        top += 38.0f;
    }

    [self loadValuesFromColor:SenkoCustomDraftColor(_draft, _light, _slot)];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
}

- (void)commitColor:(UIColor *)color {
    if (!color) return;
    SenkoCustomDraftSetColor(_draft, _light, _slot, color);
    SenkoCustomSaveDraft(_draft);
}

- (void)sliderMoved:(UISlider *)sender {
    (void)sender;
    UIColor *c = [UIColor colorWithRed:_sliders[0].value
                                 green:_sliders[1].value
                                  blue:_sliders[2].value
                                 alpha:_sliders[3].value];
    for (int i = 0; i < 4; ++i)
        _values[i].text = [NSString stringWithFormat:@"%d",
                           (int)(_sliders[i].value * 255.0f + 0.5f)];
    _preview.backgroundColor = c;
    _hex.text = SenkoHexFromColor(c);
    SenkoCustomDraftSetColor(_draft, _light, _slot, c);
}

- (void)sliderReleased:(UISlider *)sender {
    (void)sender;
    SenkoCustomSaveDraft(_draft);
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    [field resignFirstResponder];
    return NO;
}

- (void)textFieldDidEndEditing:(UITextField *)field {
    UIColor *c = SenkoColorFromHex(field.text);
    if (!c) {
        [self loadValuesFromColor:SenkoCustomDraftColor(_draft, _light, _slot)];
        return;
    }
    [self loadValuesFromColor:c];
    [self commitColor:c];
}

@end

/* ---------------------------------------------------------------------- */

@interface ThemeEditVC () <UITableViewDataSource, UITableViewDelegate,
                           UITextFieldDelegate, UIAlertViewDelegate> {
    UITableView *_tv;
    NSString *_themeId;
    NSMutableDictionary *_draft;
    BOOL _light;
    NSInteger _pendingAction; /* 1 delete */
}
@end

@implementation ThemeEditVC

enum { SecName = 0, SecStyle, SecVariant, SecColors, SecActions, SecCount };

- (id)initWithThemeId:(NSString *)themeId {
    if ((self = [super init])) {
        _themeId = [themeId copy];
        _draft = [SenkoCustomDraftForId(themeId) retain];
        _light = SenkoThemeIsLight();
        if (!SenkoCustomDraftAllowsDark(_draft)) _light = YES;
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    [_themeId release];
    [_draft release];
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
    self.title = SenkoCustomDraftName(_draft);
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    self.view.backgroundColor = kBG;

    _tv = [[UITableView alloc] initWithFrame:self.view.bounds
                                       style:UITableViewStyleGrouped];
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _tv.backgroundColor = kBG;
    if ([_tv respondsToSelector:@selector(setBackgroundView:)])
        _tv.backgroundView = nil;
    [self.view addSubview:_tv];

    /* editing the active theme is what makes changes visible while you work */
    if (![SenkoThemeCurrentId() isEqualToString:_themeId])
        SenkoThemeApplyId(_themeId);

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    self.view.backgroundColor = kBG;
    _tv.backgroundColor = kBG;
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

- (void)persist {
    SenkoCustomSaveDraft(_draft);
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return SecCount;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    if (s == SecName) return 1;
    if (s == SecStyle) return 2;
    if (s == SecVariant) return 1;
    if (s == SecColors) return SenkoSlotCount;
    return 2;
}

/* a plain string header keeps uikit's own dark label colour, which disappears on
   dark themes; the rest of the app draws its own themed label instead */
- (NSString *)headerTitleForSection:(NSInteger)s {
    if (s == SecName) return SenkoLocalizedText(@"NAME");
    if (s == SecStyle) return SenkoLocalizedText(@"STYLE");
    if (s == SecVariant) return SenkoLocalizedText(@"VARIANT");
    if (s == SecColors)
        return _light ? SenkoLocalizedText(@"LIGHT COLORS")
                      : SenkoLocalizedText(@"DARK COLORS");
    return nil;
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    NSString *title = [self headerTitleForSection:s];
    if (![title length]) return nil;
    CGFloat w = tv.bounds.size.width;
    UIView *wrap = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, w, 32)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    UILabel *lab = [[[UILabel alloc]
        initWithFrame:CGRectMake(16, 8, w - 32, 18)] autorelease];
    lab.backgroundColor = [UIColor clearColor];
    lab.font = [UIFont boldSystemFontOfSize:13];
    lab.text = title;
    SenkoStyleMutedLabel(lab);
    lab.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [wrap addSubview:lab];
    return wrap;
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv;
    return [[self headerTitleForSection:s] length] ? 32.0f : 12.0f;
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv;
    return ip.section == SecColors ? 54.0f : 44.0f;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    UITableViewCell *cell = nil;

    if (ip.section == SecColors) {
        static NSString *cid = @"slot";
        cell = [tv dequeueReusableCellWithIdentifier:cid];
        if (!cell)
            cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                           reuseIdentifier:cid] autorelease];
        SenkoThemeSlot slot = (SenkoThemeSlot)ip.row;
        UIColor *c = SenkoCustomDraftColor(_draft, _light, slot);
        cell.textLabel.text = SenkoLocalizedText(SenkoThemeSlotTitle(slot));
        cell.detailTextLabel.text = [NSString stringWithFormat:@"%@   %@",
                                     SenkoLocalizedText(SenkoThemeSlotHint(slot)),
                                     SenkoHexFromColor(c)];
        UIView *swatch = [[[UIView alloc]
            initWithFrame:CGRectMake(0, 0, 46, 30)] autorelease];
        swatch.backgroundColor = c;
        swatch.layer.cornerRadius = 6.0f;
        swatch.layer.borderWidth = 1.0f;
        swatch.layer.borderColor = [[UIColor colorWithWhite:0.5f alpha:0.45f] CGColor];
        cell.accessoryView = swatch;
        cell.selectionStyle = UITableViewCellSelectionStyleBlue;
    } else {
        static NSString *cid = @"row";
        cell = [tv dequeueReusableCellWithIdentifier:cid];
        if (!cell)
            cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                           reuseIdentifier:cid] autorelease];
        cell.accessoryView = nil;
        cell.accessoryType = UITableViewCellAccessoryNone;
        cell.detailTextLabel.text = nil;
        cell.selectionStyle = UITableViewCellSelectionStyleNone;

        if (ip.section == SecName) {
            UITextField *f = [[[UITextField alloc]
                initWithFrame:CGRectMake(0, 0, 190, 30)] autorelease];
            f.text = SenkoCustomDraftName(_draft);
            f.textAlignment = NSTextAlignmentRight;
            f.autocorrectionType = UITextAutocorrectionTypeNo;
            f.returnKeyType = UIReturnKeyDone;
            f.delegate = self;
            f.placeholder = SenkoLocalizedText(@"Theme name");
            f.textColor = kInk;
            cell.textLabel.text = SenkoLocalizedText(@"Name");
            cell.accessoryView = f;
        } else if (ip.section == SecStyle && ip.row == 0) {
            UISegmentedControl *seg = [[[UISegmentedControl alloc] initWithItems:
                [NSArray arrayWithObjects:SenkoLocalizedText(@"Classic"),
                                          SenkoLocalizedText(@"Flat"),
                                          SenkoLocalizedText(@"Glass"), nil]] autorelease];
            seg.frame = CGRectMake(0, 0, 190, 30);
            seg.selectedSegmentIndex = [[_draft objectForKey:@"style"] intValue];
            [seg addTarget:self action:@selector(styleChanged:)
          forControlEvents:UIControlEventValueChanged];
            cell.textLabel.text = SenkoLocalizedText(@"Look");
            cell.accessoryView = seg;
        } else if (ip.section == SecStyle) {
            UISlider *s = [[[UISlider alloc]
                initWithFrame:CGRectMake(0, 0, 150, 30)] autorelease];
            s.minimumValue = 0.0f;
            s.maximumValue = 32.0f;
            s.value = (float)[[_draft objectForKey:@"radius"] doubleValue];
            [s addTarget:self action:@selector(radiusChanged:)
        forControlEvents:UIControlEventValueChanged];
            /* saving rebuilds the theme and reloads this table, which would tear
               the slider out from under the finger; commit on release instead */
            [s addTarget:self action:@selector(radiusCommitted:)
        forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                         UIControlEventTouchCancel];
            cell.textLabel.text = [self radiusTitleFor:s.value];
            cell.accessoryView = s;
        } else if (ip.section == SecVariant) {
            if (SenkoCustomDraftAllowsDark(_draft)) {
                UISegmentedControl *seg = [[[UISegmentedControl alloc] initWithItems:
                    [NSArray arrayWithObjects:SenkoLocalizedText(@"Light"),
                                              SenkoLocalizedText(@"Dark"), nil]] autorelease];
                seg.frame = CGRectMake(0, 0, 150, 30);
                seg.selectedSegmentIndex = _light ? 0 : 1;
                [seg addTarget:self action:@selector(variantChanged:)
              forControlEvents:UIControlEventValueChanged];
                cell.textLabel.text = SenkoLocalizedText(@"Editing");
                cell.accessoryView = seg;
            } else {
                cell.textLabel.text = SenkoLocalizedText(@"Add dark variant");
                cell.selectionStyle = UITableViewCellSelectionStyleBlue;
                cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            }
        } else {
            cell.selectionStyle = UITableViewCellSelectionStyleBlue;
            cell.textLabel.text = ip.row == 0
                ? SenkoLocalizedText(@"Export to Documents")
                : SenkoLocalizedText(@"Delete theme");
            if (ip.row == 1) cell.textLabel.textColor = [UIColor colorWithRed:0.8f
                                                                       green:0.2f
                                                                        blue:0.2f
                                                                       alpha:1.0f];
        }
    }

    cell.backgroundColor = kCellHi;
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];
    if (!(ip.section == SecActions && ip.row == 1))
        SenkoStyleInkLabel(cell.textLabel);
    SenkoStyleMutedLabel(cell.detailTextLabel);
    SenkoStyleSelectableCell(cell);
    return cell;
}

- (void)styleChanged:(UISegmentedControl *)seg {
    [_draft setObject:[NSNumber numberWithInt:(int)seg.selectedSegmentIndex]
               forKey:@"style"];
    [self persist];
}

- (NSString *)radiusTitleFor:(float)value {
    return [NSString stringWithFormat:@"%@  %d",
            SenkoLocalizedText(@"Corners"), (int)(value + 0.5f)];
}

/* walk up to the owning cell so the readout survives cell reuse */
static UITableViewCell *SenkoCellOf(UIView *view) {
    while (view && ![view isKindOfClass:[UITableViewCell class]])
        view = view.superview;
    return (UITableViewCell *)view;
}

- (void)radiusChanged:(UISlider *)slider {
    [_draft setObject:[NSNumber numberWithDouble:(double)slider.value] forKey:@"radius"];
    UITableViewCell *cell = SenkoCellOf(slider);
    cell.textLabel.text = [self radiusTitleFor:slider.value];
}

- (void)radiusCommitted:(UISlider *)slider {
    (void)slider;
    [self persist];
}

- (void)variantChanged:(UISegmentedControl *)seg {
    _light = (seg.selectedSegmentIndex == 0);
    SenkoThemeSetLight(_light);
    [_tv reloadData];
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    [field resignFirstResponder];
    return NO;
}

- (void)textFieldDidEndEditing:(UITextField *)field {
    SenkoCustomDraftSetName(_draft, field.text);
    field.text = SenkoCustomDraftName(_draft);
    self.title = SenkoCustomDraftName(_draft);
    [self persist];
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

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];

    if (ip.section == SecColors) {
        ThemeColorVC *vc = [[[ThemeColorVC alloc] initWithDraft:_draft
                                                        themeId:_themeId
                                                           slot:(SenkoThemeSlot)ip.row
                                                          light:_light] autorelease];
        [self.navigationController pushViewController:vc animated:YES];
        return;
    }
    if (ip.section == SecVariant && !SenkoCustomDraftAllowsDark(_draft)) {
        /* seed the dark palette from the light one so every slot stays defined */
        NSDictionary *light = [_draft objectForKey:@"light"];
        [_draft setObject:[NSMutableDictionary dictionaryWithDictionary:light]
                   forKey:@"dark"];
        [self persist];
        [_tv reloadData];
        return;
    }
    if (ip.section == SecActions && ip.row == 0) {
        NSString *err = nil;
        NSString *path = SenkoCustomExport(_themeId, &err);
        if (path)
            [self showMessage:SenkoLocalizedText(@"Theme exported")
                         body:[path lastPathComponent]];
        else
            [self showMessage:SenkoLocalizedText(@"Export failed")
                         body:SenkoLocalizedText(err ? err : @"Unknown error")];
        return;
    }
    if (ip.section == SecActions && ip.row == 1) {
        _pendingAction = 1;
        UIAlertView *a = [[UIAlertView alloc]
            initWithTitle:SenkoLocalizedText(@"Delete theme?")
                  message:SenkoCustomDraftName(_draft)
                 delegate:self
        cancelButtonTitle:SenkoLocalizedText(@"Cancel")
        otherButtonTitles:SenkoLocalizedText(@"Remove"), nil];
        [a show];
        [a release];
    }
}

- (void)alertView:(UIAlertView *)alert clickedButtonAtIndex:(NSInteger)idx {
    (void)alert;
    if (_pendingAction == 1 && idx == 1) {
        SenkoCustomDelete(_themeId);
        [self.navigationController popViewControllerAnimated:YES];
    }
    _pendingAction = 0;
}

@end
