#import "server_picker.h"
#import "home_view.h"
#import "ui_theme.h"
#import "app_common.h"

#import <objc/message.h>

static BOOL SenkoPickerIsPad(void) {
    return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad;
}

static const CGFloat kSenkoChipHeight = 34.0f;
static const CGFloat kSenkoAutoHeaderHeight = 78.0f;

@implementation SenkoServerPicker

- (UIButton *)plainButton:(SEL)action {
    UIButton *b = [UIButton buttonWithType:UIButtonTypeCustom];
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:b];
    return b;
}

- (UIButton *)plateButton:(SEL)action {
    UIButton *b = [[[SenkoReliefButton alloc] initWithFrame:CGRectZero] autorelease];
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:b];
    return b;
}

- (void)buildAutoRow {
    autoHeader = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 320.0f, kSenkoAutoHeaderHeight)];
    autoHeader.backgroundColor = [UIColor clearColor];
    autoRow = [[[SenkoReliefControl alloc] initWithFrame:
                CGRectMake(0, 4.0f, 320.0f, kSenkoAutoHeaderHeight - 10.0f)] autorelease];
    autoRow.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [autoRow addTarget:self action:@selector(autoPressed)
      forControlEvents:UIControlEventTouchUpInside];
    [autoRow addTarget:self action:@selector(autoDown)
      forControlEvents:UIControlEventTouchDown];
    [autoRow addTarget:self action:@selector(autoUp)
      forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                       UIControlEventTouchCancel];
    [autoHeader addSubview:autoRow];

    CGFloat h = autoRow.bounds.size.height;
    CGFloat w = autoRow.bounds.size.width;
    autoIcon = [[[UIImageView alloc] initWithFrame:
                 CGRectMake(12.0f, floorf((h - 36.0f) * 0.5f), 36.0f, 36.0f)] autorelease];
    autoIcon.contentMode = UIViewContentModeCenter;
    autoIcon.layer.cornerRadius = 18.0f;
    autoIcon.clipsToBounds = YES;
    [autoRow addSubview:autoIcon];

    CGFloat tx = 60.0f;
    autoTitle = [[[UILabel alloc] initWithFrame:
                  CGRectMake(tx, floorf(h * 0.5f) - 20.0f, w - tx - 50.0f, 21.0f)] autorelease];
    autoTitle.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    autoTitle.backgroundColor = [UIColor clearColor];
    autoTitle.font = SenkoFontBody(16.0f, YES);
    [autoRow addSubview:autoTitle];
    autoSubtitle = [[[UILabel alloc] initWithFrame:
                     CGRectMake(tx, floorf(h * 0.5f) + 1.0f, w - tx - 50.0f, 17.0f)] autorelease];
    autoSubtitle.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    autoSubtitle.backgroundColor = [UIColor clearColor];
    autoSubtitle.font = SenkoFontBody(12.0f, NO);
    [autoRow addSubview:autoSubtitle];

    autoRadio = [[[SenkoRadioView alloc] initWithFrame:
                  CGRectMake(w - 36.0f, floorf((h - 22.0f) * 0.5f), 22.0f, 22.0f)] autorelease];
    autoRadio.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
    [autoRow addSubview:autoRadio];
}

- (id)initWithFrame:(CGRect)frame
              table:(UITableView *)list
           delegate:(id<SenkoServerPickerDelegate>)delegate {
    if ((self = [super initWithFrame:frame])) {
        _delegate = delegate;
        self.backgroundColor = [UIColor clearColor];
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                UIViewAutoresizingFlexibleHeight;
        _chips = [[NSMutableArray alloc] init];

        closeButton = [self plainButton:@selector(closePressed)];
        addButton = [self plainButton:@selector(addPressed)];
        subscriptionButton = [self plainButton:@selector(subscriptionPressed)];
        titleLabel = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
        titleLabel.backgroundColor = [UIColor clearColor];
        titleLabel.textAlignment = NSTextAlignmentCenter;
        titleLabel.font = SenkoFontBody(18.0f, YES);
        [self addSubview:titleLabel];

        searchPlate = [[[SenkoReliefControl alloc] initWithFrame:CGRectZero] autorelease];
        [self addSubview:searchPlate];
        searchIcon = [[[UIImageView alloc] initWithFrame:CGRectZero] autorelease];
        searchIcon.contentMode = UIViewContentModeCenter;
        [searchPlate addSubview:searchIcon];
        searchField = [[[UITextField alloc] initWithFrame:CGRectZero] autorelease];
        searchField.delegate = self;
        searchField.borderStyle = UITextBorderStyleNone;
        searchField.backgroundColor = [UIColor clearColor];
        searchField.font = SenkoFontBody(15.0f, NO);
        searchField.returnKeyType = UIReturnKeySearch;
        searchField.autocorrectionType = UITextAutocorrectionTypeNo;
        searchField.autocapitalizationType = UITextAutocapitalizationTypeNone;
        searchField.clearButtonMode = UITextFieldViewModeWhileEditing;
        searchField.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
        [searchField addTarget:self action:@selector(queryEdited)
              forControlEvents:UIControlEventEditingChanged];
        [searchPlate addSubview:searchField];

        pingButton = [self plateButton:@selector(pingPressed)];
        sortButton = [self plateButton:@selector(sortPressed)];

        chipBar = [[[UIScrollView alloc] initWithFrame:CGRectZero] autorelease];
        chipBar.showsHorizontalScrollIndicator = NO;
        chipBar.showsVerticalScrollIndicator = NO;
        chipBar.alwaysBounceHorizontal = YES;
        chipBar.scrollsToTop = NO;
        [self addSubview:chipBar];

        table = list;
        [self addSubview:table];
        [self buildAutoRow];

/* the keyboard covers the lower rows while a search is typed */
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        [nc addObserver:self selector:@selector(keyboardWillShow:)
                   name:UIKeyboardWillShowNotification object:nil];
        [nc addObserver:self selector:@selector(keyboardWillHide:)
                   name:UIKeyboardWillHideNotification object:nil];

        [self relocalize];
        [self applyTheme];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    searchField.delegate = nil;
    [_chips release];
    [_chipTitles release];
    [autoHeader release];
    [super dealloc];
}

- (void)relocalize {
    titleLabel.text = SenkoLocalizedText(@"Choose a server");
    searchField.placeholder = SenkoLocalizedText(@"Search a country or a city");
    autoTitle.text = SenkoLocalizedText(@"Auto");
    closeButton.accessibilityLabel = SenkoLocalizedText(@"Close");
    addButton.accessibilityLabel = SenkoLocalizedText(@"Add");
    subscriptionButton.accessibilityLabel = SenkoLocalizedText(@"Subscription");
    sortButton.accessibilityLabel = SenkoLocalizedText(@"Sort servers");
    pingButton.accessibilityLabel = SenkoLocalizedText(@"Check servers");
    [self applyTheme];
}

- (void)styleChip:(UIButton *)chip selected:(BOOL)selected {
    SenkoStyleHomePlate(chip, kSenkoChipHeight * 0.5f, selected ? kAccentBlue : nil);
    UIColor *ink = selected ? SenkoPillLabelColor(kAccentBlue) : kInk;
    [chip setTitleColor:ink forState:UIControlStateNormal];
    [chip setTitleColor:[ink colorWithAlphaComponent:0.55f]
               forState:UIControlStateHighlighted];
    chip.titleLabel.shadowOffset = CGSizeZero;
}

- (void)applyTheme {
    BOOL light = SenkoThemeIsLight();
    titleLabel.textColor = kInk;
    titleLabel.shadowColor = nil;
    [closeButton setImage:SenkoIconClose(20.0f, kInk) forState:UIControlStateNormal];
    [addButton setImage:SenkoPlusIcon(22.0f, kInk) forState:UIControlStateNormal];
    [subscriptionButton setTitle:@"•••" forState:UIControlStateNormal];
    subscriptionButton.titleLabel.font = [UIFont boldSystemFontOfSize:18.0f];
    [subscriptionButton setTitleColor:kInk forState:UIControlStateNormal];
    SenkoStyleHomePlate(searchPlate, searchPlate.bounds.size.height > 1.0f
                        ? searchPlate.bounds.size.height * 0.5f : 20.0f, nil);
    SenkoStyleHomePlate(pingButton, 12.0f, nil);
    SenkoStyleHomePlate(sortButton, 12.0f, nil);
    [pingButton setImage:GaugeIcon(20.0f, kInk) forState:UIControlStateNormal];
    [sortButton setImage:TintedIconNamed(@"glyph-sort.png", 20.0f, kInk)
                forState:UIControlStateNormal];
    searchIcon.image = TintedIconNamed(@"glyph-search.png", 18.0f, kInkMuted);
    searchField.textColor = kInk;
    searchField.keyboardAppearance = light ? UIKeyboardAppearanceDefault
                                           : UIKeyboardAppearanceAlert;
/* NSForegroundColorAttributeName is an ios 6 symbol, and a strong reference to
   it stops the ios 5 loader, so the attribute goes in under its string value */
    if ([searchField.placeholder length] &&
        [searchField respondsToSelector:@selector(setAttributedPlaceholder:)]) {
        NSDictionary *attrs = [NSDictionary dictionaryWithObject:
                               [kInkMuted colorWithAlphaComponent:0.8f] forKey:@"NSColor"];
        NSAttributedString *hint = [[[NSAttributedString alloc]
            initWithString:searchField.placeholder attributes:attrs] autorelease];
        ((void (*)(id, SEL, id))objc_msgSend)(searchField,
            @selector(setAttributedPlaceholder:), hint);
    }

    SenkoStyleHomePlate(autoRow, SenkoHomePlateRadius(), nil);
    autoIcon.backgroundColor = [kAccentBlue colorWithAlphaComponent:0.14f];
    autoIcon.image = TintedIconNamed(@"glyph-bolt.png", 20.0f, kAccentBlue);
    autoTitle.textColor = kInk;
    autoTitle.shadowColor = nil;
    autoSubtitle.textColor = kInkMuted;
    autoSubtitle.shadowColor = nil;
    [autoRadio applyTheme];

    for (UIButton *chip in _chips)
        [self styleChip:chip selected:chip.tag == _selectedChip];
    table.backgroundColor = [UIColor clearColor];
    [self setNeedsLayout];
}

- (void)chipPressed:(UIButton *)chip {
    if (chip.tag == _selectedChip) return;
    _selectedChip = chip.tag;
    for (UIButton *other in _chips)
        [self styleChip:other selected:other.tag == _selectedChip];
    [_delegate serverPickerChooseChip:_selectedChip];
}

- (void)layoutChips {
    CGFloat x = 0.0f;
    UIFont *font = SenkoFontBody(14.0f, YES);
    for (UIButton *chip in _chips) {
        CGFloat w = ceilf(SenkoTextWidth([chip titleForState:UIControlStateNormal], font)) + 30.0f;
        if (w < 64.0f) w = 64.0f;
        chip.frame = CGRectMake(x, 1.0f, w, kSenkoChipHeight);
        x += w + 8.0f;
    }
    chipBar.contentSize = CGSizeMake(x > 8.0f ? x - 8.0f : 0.0f, kSenkoChipHeight + 2.0f);
}

- (void)setChipTitles:(NSArray *)titles selected:(NSInteger)selected {
    if (selected < 0 || selected >= (NSInteger)[titles count]) selected = 0;
    BOOL same = _chipTitles && [_chipTitles isEqualToArray:titles];
    if (!same) {
        [_chipTitles release];
        _chipTitles = [titles copy];
        for (UIButton *chip in _chips) [chip removeFromSuperview];
        [_chips removeAllObjects];
        NSInteger i = 0;
        for (NSString *title in _chipTitles) {
            UIButton *chip = [[[SenkoReliefButton alloc] initWithFrame:CGRectZero] autorelease];
            chip.tag = i++;
            chip.titleLabel.font = SenkoFontBody(14.0f, YES);
            chip.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
            [chip setTitle:title forState:UIControlStateNormal];
            [chip addTarget:self action:@selector(chipPressed:)
           forControlEvents:UIControlEventTouchUpInside];
            [chipBar addSubview:chip];
            [_chips addObject:chip];
        }
        [self layoutChips];
    }
    _selectedChip = selected;
    for (UIButton *chip in _chips)
        [self styleChip:chip selected:chip.tag == _selectedChip];
}

- (void)setAutoVisible:(BOOL)visible picked:(BOOL)picked subtitle:(NSString *)subtitle {
    autoSubtitle.text = subtitle;
    [autoRadio setChecked:picked];
    if (!visible) {
        if (table.tableHeaderView) table.tableHeaderView = nil;
        return;
    }
    if (table.tableHeaderView != autoHeader) {
        CGRect f = autoHeader.frame;
        f.size.width = table.bounds.size.width > 1.0f ? table.bounds.size.width : f.size.width;
        autoHeader.frame = f;
        table.tableHeaderView = autoHeader;
    }
}

- (void)clearQuery {
    searchField.text = nil;
    if ([searchField isFirstResponder]) [searchField resignFirstResponder];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect b = self.bounds;
    CGFloat W = b.size.width;
    CGFloat H = b.size.height;
    if (W < 1.0f || H < 1.0f) return;
    BOOL pad = SenkoPickerIsPad();
    UIEdgeInsets safe = SenkoSafeAreaInsets(self);
    CGFloat top = MAX(safe.top, GetTopOffset());
    CGFloat margin = pad ? 28.0f : 16.0f;
    CGFloat x0 = safe.left + margin;
    CGFloat x1 = W - safe.right - margin;
    CGFloat colW = x1 - x0;
    if (pad && colW > 620.0f) colW = 620.0f;
    CGFloat colX = floorf((W - colW) * 0.5f);
    BOOL shortScreen = H < 420.0f;

    CGFloat headerY = top + 4.0f;
    CGFloat headerH = shortScreen ? 36.0f : 44.0f;
    closeButton.frame = CGRectMake(colX - 10.0f, headerY, 44.0f, headerH);
    addButton.frame = CGRectMake(colX + colW - 34.0f, headerY, 44.0f, headerH);
    subscriptionButton.frame = CGRectMake(colX + colW - 78.0f, headerY, 44.0f, headerH);
    titleLabel.frame = CGRectMake(colX + 44.0f, headerY, colW - 132.0f, headerH);

    CGFloat ctl = shortScreen ? 36.0f : 40.0f;
    CGFloat y = headerY + headerH + (shortScreen ? 2.0f : 6.0f);
    sortButton.frame = CGRectMake(colX + colW - ctl, y, ctl, ctl);
    pingButton.frame = CGRectMake(colX + colW - ctl * 2.0f - 8.0f, y, ctl, ctl);
    CGFloat searchW = colW - ctl * 2.0f - 16.0f;
    searchPlate.frame = CGRectMake(colX, y, searchW, ctl);
    searchPlate.layer.cornerRadius = ctl * 0.5f;
    searchIcon.frame = CGRectMake(10.0f, floorf((ctl - 20.0f) * 0.5f), 20.0f, 20.0f);
    searchField.frame = CGRectMake(36.0f, 0.0f, searchW - 42.0f, ctl);

    y += ctl + (shortScreen ? 6.0f : 10.0f);
    chipBar.frame = CGRectMake(0.0f, y, W, kSenkoChipHeight + 2.0f);
    UIEdgeInsets chipInset = UIEdgeInsetsMake(0.0f, colX, 0.0f, W - colX - colW);
    if (!UIEdgeInsetsEqualToEdgeInsets(chipBar.contentInset, chipInset)) {
        chipBar.contentInset = chipInset;
        chipBar.contentOffset = CGPointMake(-colX, 0.0f);
    }
    y += kSenkoChipHeight + (shortScreen ? 6.0f : 10.0f);

    CGRect listFrame = CGRectMake(colX, y, colW, H - y);
    if (!CGRectEqualToRect(table.frame, listFrame)) table.frame = listFrame;
    UIEdgeInsets inset = UIEdgeInsetsMake(0.0f, 0.0f,
                                          safe.bottom + 12.0f + _keyboardInset, 0.0f);
    if (!UIEdgeInsetsEqualToEdgeInsets(table.contentInset, inset)) {
        table.contentInset = inset;
        table.scrollIndicatorInsets = inset;
    }
}

- (void)keyboardWillShow:(NSNotification *)note {
    if (self.hidden || !self.superview) return;
    NSValue *value = [[note userInfo] objectForKey:UIKeyboardFrameEndUserInfoKey];
    if (!value) return;
    CGRect kb = [self convertRect:[value CGRectValue] fromView:nil];
    CGFloat overlap = CGRectGetMaxY(self.bounds) - CGRectGetMinY(kb);
    _keyboardInset = overlap > 0.0f ? overlap : 0.0f;
    [self setNeedsLayout];
}

- (void)keyboardWillHide:(NSNotification *)note {
    (void)note;
    _keyboardInset = 0.0f;
    [self setNeedsLayout];
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    [field resignFirstResponder];
    return YES;
}

- (BOOL)textFieldShouldClear:(UITextField *)field {
    (void)field;
/* the clear button empties the field without an editing-changed event */
    [_delegate serverPickerQueryChanged:@""];
    return YES;
}

- (void)queryEdited {
    [_delegate serverPickerQueryChanged:searchField.text ? searchField.text : @""];
}

- (void)closePressed {
    [searchField resignFirstResponder];
    [_delegate serverPickerClose];
}

- (void)addPressed {
    [searchField resignFirstResponder];
    [_delegate serverPickerAdd];
}

- (void)subscriptionPressed {
    [searchField resignFirstResponder];
    [_delegate serverPickerManageSubscriptions];
}

- (void)sortPressed {
    [searchField resignFirstResponder];
    [_delegate serverPickerSort];
}

- (void)pingPressed {
    [searchField resignFirstResponder];
    [_delegate serverPickerPing];
}

- (void)autoDown { SenkoPressPop(autoRow, YES); }
- (void)autoUp { SenkoPressPop(autoRow, NO); }

- (void)autoPressed {
    [searchField resignFirstResponder];
    [_delegate serverPickerChooseAuto];
}

@end
