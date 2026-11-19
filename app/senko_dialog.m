#import "senko_dialog.h"
#import "app_common.h"
#import "card_kit.h"
#import "ui_theme.h"
#import "senko_animation.h"

#import <QuartzCore/QuartzCore.h>
#include <math.h>
#import <objc/message.h>

#define DIALOG_CARD_MAX 400.0f
#define DIALOG_MARGIN 20.0f
#define DIALOG_BUTTON_H 44.0f
#define DIALOG_ITEM_H 46.0f
#define DIALOG_PAD 18.0f

static NSString *StickerName(SenkoDialogMood mood) {
    switch (mood) {
        case SenkoDialogMoodAsk: return @"sticker-ask";
        case SenkoDialogMoodInput: return @"sticker-input";
        case SenkoDialogMoodError: return @"sticker-error";
        default: return @"sticker-smile";
    }
}

static UIImage *StickerImage(SenkoDialogMood mood) {
    /* a plain file read: imageNamed would keep four 256 px bitmaps for the life of
       the process for a card that shows one of them at a time */
    NSString *path = [[NSBundle mainBundle] pathForResource:StickerName(mood) ofType:@"png"];
    return path ? [UIImage imageWithContentsOfFile:path] : nil;
}

static NSString *Localized(NSString *text) {
    return [text length] ? SenkoLocalizedText(text) : text;
}

/* one button of the card. the filled one carries the accent, the others are
   outlined in it, so the choice reads on every theme */
@interface SenkoDialogButton : UIButton {
    SenkoDialogButtonStyle _style;
    BOOL _filled;
    CAGradientLayer *_fill;
}
- (id)initWithTitle:(NSString *)title style:(SenkoDialogButtonStyle)style filled:(BOOL)filled;
- (SenkoDialogButtonStyle)buttonStyle;
@end

@implementation SenkoDialogButton

- (id)initWithTitle:(NSString *)title style:(SenkoDialogButtonStyle)style filled:(BOOL)filled {
    if ((self = [super initWithFrame:CGRectZero])) {
        _style = style;
        _filled = filled;
        UIColor *accent = kAccentBlue ? kAccentBlue : [UIColor colorWithWhite:0.4f alpha:1.0f];
        UIColor *tone = style == SenkoDialogButtonDestructive
            ? [UIColor colorWithRed:0.86f green:0.22f blue:0.22f alpha:1.0f] : accent;
        self.backgroundColor = [UIColor clearColor];
        [self setTitle:title forState:UIControlStateNormal];
        self.titleLabel.font = SenkoFontBody(15.0f, YES);
        self.titleLabel.numberOfLines = 1;
        self.titleLabel.textAlignment = NSTextAlignmentCenter;
        self.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
        self.contentEdgeInsets = UIEdgeInsetsMake(0.0f, 10.0f, 0.0f, 10.0f);
        self.layer.cornerRadius = fmaxf(8.0f, SenkoThemeCardRadius());
        if (filled) {
            [self setTitleColor:SenkoPillLabelColor(tone) forState:UIControlStateNormal];
            _fill = [[CAGradientLayer layer] retain];
            _fill.colors = [NSArray arrayWithObjects:
                            (id)SenkoShadeColor(tone, 0.20f).CGColor,
                            (id)SenkoShadeColor(tone, -0.20f).CGColor, nil];
            _fill.cornerRadius = self.layer.cornerRadius;
            [self.layer insertSublayer:_fill atIndex:0];
        } else {
            [self setTitleColor:tone forState:UIControlStateNormal];
            self.layer.borderWidth = 1.0f;
            self.layer.borderColor = [tone colorWithAlphaComponent:0.55f].CGColor;
        }
        [self addTarget:self action:@selector(pressDown) forControlEvents:UIControlEventTouchDown | UIControlEventTouchDragEnter];
        [self addTarget:self action:@selector(pressUp) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchDragExit | UIControlEventTouchCancel];
    }
    return self;
}

- (void)dealloc {
    [_fill release];
    [super dealloc];
}

- (SenkoDialogButtonStyle)buttonStyle { return _style; }

- (void)pressDown { SenkoPressPop(self, YES); }
- (void)pressUp { SenkoPressPop(self, NO); }

- (void)layoutSubviews {
    [super layoutSubviews];
    if (_fill) {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        _fill.frame = self.bounds;
        [CATransaction commit];
    }
}

@end

/* one row of a list of choices */
@interface SenkoDialogRow : UIButton
@end

@implementation SenkoDialogRow
- (void)setHighlighted:(BOOL)highlighted {
    [super setHighlighted:highlighted];
    self.backgroundColor = highlighted ? [kInk colorWithAlphaComponent:0.10f] : [UIColor clearColor];
}
@end

@class SenkoDialogViewController;

@interface SenkoDialog () {
    NSString *_title;
    NSString *_message;
    SenkoDialogMood _mood;
    BOOL _backdropSet;
    BOOL _dismissOnBackdrop;
    NSMutableArray *_buttons;   /* dictionaries: title, style, handler */
    NSMutableArray *_items;     /* dictionaries: title, handler */
    UITextField *_textField;
    UIView *_content;
    UIWindow *_window;
    UIWindow *_previousKey;
    SenkoDialogViewController *_controller;
    BOOL _shown;
}
- (void)runButtonAtIndex:(NSInteger)index;
- (void)runItemAtIndex:(NSInteger)index;
- (void)backdropTapped;
- (void)primaryTriggered;
- (SenkoDialogMood)resolvedMood;
- (NSArray *)buttonSpecs;
- (NSArray *)itemSpecs;
- (UIView *)contentView;
@end

/* the card and everything on it, laid out by hand: UIKit has no inline sizing
   for a plate that must stay inside the room the keyboard leaves */
@interface SenkoDialogView : UIView <UITextFieldDelegate> {
    SenkoDialog *_dialog;        /* assigned, the dialog owns the window that owns this */
    UIControl *_backdrop;
    SenkoCardView *_card;
    UIImageView *_face;
    UILabel *_titleLabel;
    UILabel *_messageLabel;
    UIScrollView *_scroll;
    NSMutableArray *_rows;
    NSMutableArray *_rules;
    NSMutableArray *_buttons;
    UIView *_content;
    UITextField *_field;
    CGFloat _keyboardInset;
    BOOL _entered;
}
- (id)initWithDialog:(SenkoDialog *)dialog;
- (void)setKeyboardInset:(CGFloat)inset;
- (void)playEntrance;
- (void)focusField;
@end

@interface SenkoDialogViewController : UIViewController {
    SenkoDialog *_dialog;
    UIViewController *_host;
}
- (id)initWithDialog:(SenkoDialog *)dialog host:(UIViewController *)host;
@end

@implementation SenkoDialog
@synthesize title = _title;
@synthesize message = _message;
@synthesize mood = _mood;
@synthesize textField = _textField;

+ (SenkoDialog *)dialogWithTitle:(NSString *)title message:(NSString *)message {
    SenkoDialog *dialog = [[[SenkoDialog alloc] init] autorelease];
    dialog.title = title;
    dialog.message = message;
    return dialog;
}

- (id)init {
    if ((self = [super init])) {
        _mood = SenkoDialogMoodAuto;
        _buttons = [[NSMutableArray alloc] init];
        _items = [[NSMutableArray alloc] init];
    }
    return self;
}

- (void)dealloc {
    [_title release];
    [_message release];
    [_buttons release];
    [_items release];
    [_textField release];
    [_content release];
    [_window release];
    [_previousKey release];
    [_controller release];
    [super dealloc];
}

- (BOOL)hasCancel {
    for (NSDictionary *b in _buttons)
        if ([[b objectForKey:@"style"] intValue] == SenkoDialogButtonCancel) return YES;
    return NO;
}

- (BOOL)dismissOnBackdrop {
    return _backdropSet ? _dismissOnBackdrop : [self hasCancel];
}

- (void)setDismissOnBackdrop:(BOOL)value {
    _backdropSet = YES;
    _dismissOnBackdrop = value;
}

/* the sticker follows the content unless the screen picked one: a choice asks,
   a field inputs, a note with a way out asks and a plain note smiles */
- (SenkoDialogMood)resolvedMood {
    if (_mood != SenkoDialogMoodAuto) return _mood;
    if ([_items count]) return SenkoDialogMoodAsk;
    if (_textField || _content) return SenkoDialogMoodInput;
    return [self hasCancel] ? SenkoDialogMoodAsk : SenkoDialogMoodHappy;
}

- (void)addButtonWithTitle:(NSString *)title
                     style:(SenkoDialogButtonStyle)style
                   handler:(void (^)(SenkoDialog *))handler {
    NSMutableDictionary *b = [NSMutableDictionary dictionary];
    [b setObject:Localized(title) ? Localized(title) : @"" forKey:@"title"];
    [b setObject:[NSNumber numberWithInt:style] forKey:@"style"];
    if (handler) [b setObject:[[handler copy] autorelease] forKey:@"handler"];
    [_buttons addObject:b];
}

- (void)addItemWithTitle:(NSString *)title handler:(void (^)(SenkoDialog *))handler {
    NSMutableDictionary *i = [NSMutableDictionary dictionary];
    [i setObject:Localized(title) ? Localized(title) : @"" forKey:@"title"];
    if (handler) [i setObject:[[handler copy] autorelease] forKey:@"handler"];
    [_items addObject:i];
}

- (UITextField *)addTextFieldWithText:(NSString *)text
                          placeholder:(NSString *)placeholder
                             keyboard:(UIKeyboardType)keyboard {
    if (_textField) return _textField;
    UITextField *field = [[UITextField alloc] initWithFrame:CGRectMake(0, 0, 100, 40)];
    field.backgroundColor = kWell ? kWell : [UIColor colorWithWhite:0.5f alpha:0.15f];
    field.textColor = kInk;
    field.font = [UIFont systemFontOfSize:16.0f];
    field.text = text;
    field.placeholder = Localized(placeholder);
    field.keyboardType = keyboard;
    field.autocorrectionType = UITextAutocorrectionTypeNo;
    field.autocapitalizationType = UITextAutocapitalizationTypeNone;
    field.clearButtonMode = UITextFieldViewModeWhileEditing;
    field.returnKeyType = UIReturnKeyDone;
    field.borderStyle = UITextBorderStyleNone;
    field.layer.cornerRadius = 8.0f;
    field.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
    /* a field has no inner padding of its own once its border is gone */
    UIView *pad = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, 10, 10)] autorelease];
    field.leftView = pad;
    field.leftViewMode = UITextFieldViewModeAlways;
    _textField = field;
    return field;
}

- (void)setContentView:(UIView *)view {
    if (view == _content) return;
    [_content release];
    _content = [view retain];
}

/* what SenkoDialogView reads, so the view never owns a second copy of the specs */
- (NSArray *)buttonSpecs { return _buttons; }
- (NSArray *)itemSpecs { return _items; }
- (UIView *)contentView { return _content; }

- (void)runButtonAtIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)[_buttons count]) return;
    void (^handler)(SenkoDialog *) = [[_buttons objectAtIndex:index] objectForKey:@"handler"];
    [self retain];
    [self dismiss];
    if (handler) handler(self);
    [self release];
}

- (void)runItemAtIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)[_items count]) return;
    void (^handler)(SenkoDialog *) = [[_items objectAtIndex:index] objectForKey:@"handler"];
    [self retain];
    [self dismiss];
    if (handler) handler(self);
    [self release];
}

- (void)backdropTapped {
    if (![self dismissOnBackdrop]) return;
    for (NSUInteger i = 0; i < [_buttons count]; ++i) {
        if ([[[_buttons objectAtIndex:i] objectForKey:@"style"] intValue] == SenkoDialogButtonCancel) {
            [self runButtonAtIndex:(NSInteger)i];
            return;
        }
    }
    [self dismiss];
}

/* return on the keyboard confirms: the last normal or destructive button */
- (void)primaryTriggered {
    for (NSInteger i = (NSInteger)[_buttons count] - 1; i >= 0; --i) {
        if ([[[_buttons objectAtIndex:i] objectForKey:@"style"] intValue] != SenkoDialogButtonCancel) {
            [self runButtonAtIndex:i];
            return;
        }
    }
}

static NSMutableArray *gShownDialogs = nil;

- (void)show {
    if (![NSThread isMainThread]) {
        [self performSelectorOnMainThread:@selector(show) withObject:nil waitUntilDone:NO];
        return;
    }
    if (_shown) return;
    _shown = YES;
    if (!gShownDialogs) gShownDialogs = [[NSMutableArray alloc] init];
    [gShownDialogs addObject:self];

    UIApplication *app = [UIApplication sharedApplication];
    _previousKey = [[app keyWindow] retain];
    UIViewController *host = _previousKey.rootViewController;
    _controller = [[SenkoDialogViewController alloc] initWithDialog:self host:host];
    _window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    _window.windowLevel = UIWindowLevelAlert + 1.0f;
    _window.backgroundColor = [UIColor clearColor];
    _window.rootViewController = _controller;
    [_window makeKeyAndVisible];
    [(SenkoDialogView *)_controller.view playEntrance];
    [(SenkoDialogView *)_controller.view focusField];
}

- (void)dismiss {
    if (!_shown) return;
    _shown = NO;
    [self retain];
    [_textField resignFirstResponder];
    /* dismiss runs inside the action of a button on this card, so the window, its
       controller and the view stay alive until the touch has been handled */
    [[_window retain] autorelease];
    [[_controller retain] autorelease];
    [[_controller.view retain] autorelease];
    _window.hidden = YES;
    if (_previousKey && !_previousKey.hidden) [_previousKey makeKeyWindow];
    _window.rootViewController = nil;
    [gShownDialogs removeObject:self];
    [self release];
}

@end

@implementation SenkoDialogViewController

- (id)initWithDialog:(SenkoDialog *)dialog host:(UIViewController *)host {
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _dialog = dialog;
        _host = host;
        /* ios 5 and 6 start a root view below the status bar, which would leave
           a strip above the dimmed card */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if ([self respondsToSelector:@selector(setWantsFullScreenLayout:)])
            self.wantsFullScreenLayout = YES;
#pragma clang diagnostic pop
    }
    return self;
}

- (void)loadView {
    self.view = [[[SenkoDialogView alloc] initWithDialog:_dialog] autorelease];
}

/* the card follows the orientations the app allows, as UIAlertView did: a
   window of its own would otherwise rotate to anything and stand sideways
   over a screen that is locked upright */
- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)orientation {
    if (_host && [_host respondsToSelector:@selector(shouldAutorotateToInterfaceOrientation:)])
        return [_host shouldAutorotateToInterfaceOrientation:orientation];
    return YES;
}

- (BOOL)shouldAutorotate {
    if (_host && [_host respondsToSelector:@selector(shouldAutorotate)])
        return [_host shouldAutorotate];
    return YES;
}

- (NSUInteger)supportedInterfaceOrientations {
    if (_host && [_host respondsToSelector:@selector(supportedInterfaceOrientations)])
        return [_host supportedInterfaceOrientations];
    return UIInterfaceOrientationMaskAll;
}

- (BOOL)prefersStatusBarHidden {
    /* ios 7 selectors, which the armv7 sdk does not declare */
    SEL hidden = @selector(prefersStatusBarHidden);
    if (_host && [_host respondsToSelector:hidden])
        return ((BOOL (*)(id, SEL))objc_msgSend)(_host, hidden);
    return NO;
}

- (UIStatusBarStyle)preferredStatusBarStyle {
    SEL style = @selector(preferredStatusBarStyle);
    if (_host && [_host respondsToSelector:style])
        return ((UIStatusBarStyle (*)(id, SEL))objc_msgSend)(_host, style);
    return UIStatusBarStyleDefault;
}

@end

@implementation SenkoDialogView

- (id)initWithDialog:(SenkoDialog *)dialog {
    if ((self = [super initWithFrame:[[UIScreen mainScreen] bounds]])) {
        _dialog = dialog;
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        self.backgroundColor = [UIColor clearColor];

        _backdrop = [[UIControl alloc] initWithFrame:self.bounds];
        _backdrop.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        _backdrop.backgroundColor = [UIColor colorWithWhite:0.0f alpha:0.6f];
        [_backdrop addTarget:dialog action:@selector(backdropTapped) forControlEvents:UIControlEventTouchUpInside];
        [self addSubview:_backdrop];

        _card = [[SenkoCardView alloc] initWithFrame:CGRectMake(0, 0, 300, 200)];
        [self addSubview:_card];

        _face = [[UIImageView alloc] initWithFrame:CGRectMake(0, 0, 104, 104)];
        _face.contentMode = UIViewContentModeScaleAspectFit;
        _face.image = StickerImage([dialog resolvedMood]);
        _face.userInteractionEnabled = NO;
        [self addSubview:_face];

        NSString *title = Localized(dialog.title);
        if ([title length]) {
            _titleLabel = [[UILabel alloc] initWithFrame:CGRectZero];
            _titleLabel.backgroundColor = [UIColor clearColor];
            _titleLabel.font = SenkoFontBody(19.0f, YES);
            _titleLabel.textColor = kInk;
            _titleLabel.textAlignment = NSTextAlignmentCenter;
            _titleLabel.numberOfLines = 0;
            _titleLabel.text = title;
            [_card addSubview:_titleLabel];
        }

        _scroll = [[UIScrollView alloc] initWithFrame:CGRectZero];
        _scroll.backgroundColor = [UIColor clearColor];
        _scroll.showsHorizontalScrollIndicator = NO;
        _scroll.showsVerticalScrollIndicator = NO;
        [_card addSubview:_scroll];

        NSString *message = Localized(dialog.message);
        if ([message length]) {
            _messageLabel = [[UILabel alloc] initWithFrame:CGRectZero];
            _messageLabel.backgroundColor = [UIColor clearColor];
            _messageLabel.font = SenkoFontBody(15.0f, NO);
            _messageLabel.textColor = kInkMuted;
            _messageLabel.textAlignment = NSTextAlignmentCenter;
            _messageLabel.numberOfLines = 0;
            _messageLabel.text = message;
            [_scroll addSubview:_messageLabel];
        }

        _content = [[dialog contentView] retain];
        if (_content) {
            [_content removeFromSuperview];
            [_scroll addSubview:_content];
        }
        _field = [dialog.textField retain];
        if (_field) {
            _field.delegate = self;
            [_scroll addSubview:_field];
        }

        _rows = [[NSMutableArray alloc] init];
        _rules = [[NSMutableArray alloc] init];
        NSArray *items = [dialog itemSpecs];
        for (NSUInteger i = 0; i < [items count]; ++i) {
            UIView *rule = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
            rule.backgroundColor = [kInk colorWithAlphaComponent:SenkoThemeIsLight() ? 0.14f : 0.22f];
            [_scroll addSubview:rule];
            [_rules addObject:rule];
            SenkoDialogRow *row = [SenkoDialogRow buttonWithType:UIButtonTypeCustom];
            [row setTitle:[[items objectAtIndex:i] objectForKey:@"title"] forState:UIControlStateNormal];
            [row setTitleColor:kAccentBlue forState:UIControlStateNormal];
            row.titleLabel.font = SenkoFontBody(16.0f, NO);
            row.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
            row.tag = (NSInteger)i;
            [row addTarget:self action:@selector(rowTapped:) forControlEvents:UIControlEventTouchUpInside];
            [_scroll addSubview:row];
            [_rows addObject:row];
        }

        _buttons = [[NSMutableArray alloc] init];
        NSArray *specs = [dialog buttonSpecs];
        NSInteger primary = -1;
        for (NSInteger i = (NSInteger)[specs count] - 1; i >= 0; --i) {
            if ([[[specs objectAtIndex:i] objectForKey:@"style"] intValue] != SenkoDialogButtonCancel) {
                primary = i;
                break;
            }
        }
        for (NSUInteger i = 0; i < [specs count]; ++i) {
            NSDictionary *spec = [specs objectAtIndex:i];
            SenkoDialogButtonStyle style = (SenkoDialogButtonStyle)[[spec objectForKey:@"style"] intValue];
            SenkoDialogButton *button = [[[SenkoDialogButton alloc]
                initWithTitle:[spec objectForKey:@"title"]
                        style:style
                       filled:(NSInteger)i == primary] autorelease];
            button.tag = (NSInteger)i;
            [button addTarget:self action:@selector(buttonTapped:) forControlEvents:UIControlEventTouchUpInside];
            [_card addSubview:button];
            [_buttons addObject:button];
        }

        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(keyboardChanged:)
                                                     name:UIKeyboardWillShowNotification
                                                   object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(keyboardChanged:)
                                                     name:UIKeyboardWillHideNotification
                                                   object:nil];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _field.delegate = nil;
    [_backdrop release];
    [_card release];
    [_face release];
    [_titleLabel release];
    [_messageLabel release];
    [_scroll release];
    [_rows release];
    [_rules release];
    [_buttons release];
    [_content release];
    [_field release];
    [super dealloc];
}

- (void)rowTapped:(UIButton *)row { [_dialog runItemAtIndex:row.tag]; }
- (void)buttonTapped:(UIButton *)button { [_dialog runButtonAtIndex:button.tag]; }

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    (void)field;
    [_dialog primaryTriggered];
    return NO;
}

- (void)keyboardChanged:(NSNotification *)note {
    CGFloat inset = 0.0f;
    if ([[note name] isEqualToString:UIKeyboardWillShowNotification] && self.window) {
        CGRect end = [[[note userInfo] objectForKey:UIKeyboardFrameEndUserInfoKey] CGRectValue];
        /* the frame is in screen space and the window may be turned */
        CGRect inWindow = [self.window convertRect:end fromWindow:nil];
        CGRect inSelf = [self convertRect:inWindow fromView:nil];
        inset = fmaxf(0.0f, CGRectGetMaxY(self.bounds) - inSelf.origin.y);
    }
    [self setKeyboardInset:inset];
}

- (void)setKeyboardInset:(CGFloat)inset {
    if (inset == _keyboardInset) return;
    _keyboardInset = inset;
    [self setNeedsLayout];
    SenkoAnimate(0.25, ^{ [self layoutIfNeeded]; }, NULL);
}

- (void)focusField {
    if (!_field) return;
    [self layoutIfNeeded];
    [_field becomeFirstResponder];
}

- (CGFloat)stickerSide {
    return self.bounds.size.height < 460.0f ? 64.0f : 104.0f;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect bounds = self.bounds;
    CGFloat side = [self stickerSide];
    CGFloat overlap = side * 0.75f;
    CGFloat cardW = fminf(DIALOG_CARD_MAX, bounds.size.width - 2.0f * DIALOG_MARGIN);
    CGFloat innerW = cardW - 2.0f * DIALOG_PAD;
    CGFloat topPad = side - overlap + 6.0f;

    CGFloat titleH = 0.0f;
    if (_titleLabel) {
        titleH = ceilf(SenkoTextSize(_titleLabel.text, _titleLabel.font, innerW).height);
        _titleLabel.frame = CGRectMake(DIALOG_PAD, topPad, innerW, titleH);
    }

    /* the middle of the card scrolls, so a long message keeps the buttons in reach */
    CGFloat y = 4.0f;
    if (_messageLabel) {
        CGFloat h = ceilf(SenkoTextSize(_messageLabel.text, _messageLabel.font, innerW).height);
        _messageLabel.frame = CGRectMake(0, y + 8.0f, innerW, h);
        y += 8.0f + h + 4.0f;
    }
    if (_field) {
        _field.frame = CGRectMake(0, y + 6.0f, innerW, 40.0f);
        y += 6.0f + 40.0f + 4.0f;
    }
    if (_content) {
        CGFloat h = _content.frame.size.height;
        _content.frame = CGRectMake(0, y + 6.0f, innerW, h);
        y += 6.0f + h + 4.0f;
    }
    for (NSUInteger i = 0; i < [_rows count]; ++i) {
        UIView *rule = [_rules objectAtIndex:i];
        rule.frame = CGRectMake(0, y, innerW, 0.5f);
        UIView *row = [_rows objectAtIndex:i];
        row.frame = CGRectMake(0, y + 0.5f, innerW, DIALOG_ITEM_H);
        y += 0.5f + DIALOG_ITEM_H;
    }
    CGFloat scrollContent = y;

    /* one or two buttons share a row, more stack, so no title is squeezed */
    NSUInteger count = [_buttons count];
    BOOL stacked = count > 2;
    CGFloat buttonsH = 0.0f;
    if (count) buttonsH = stacked ? count * DIALOG_BUTTON_H + (count - 1) * 8.0f : DIALOG_BUTTON_H;
    CGFloat buttonsGap = count ? 14.0f : 0.0f;

    CGFloat available = bounds.size.height - _keyboardInset - 16.0f;
    CGFloat fixed = overlap + topPad + titleH + buttonsGap + buttonsH + 14.0f;
    CGFloat cap = fminf(bounds.size.height * 0.55f, available - fixed);
    if (cap < 60.0f) cap = 60.0f;
    CGFloat scrollH = fminf(scrollContent, cap);
    _scroll.frame = CGRectMake(DIALOG_PAD, topPad + titleH, innerW, scrollH);
    _scroll.contentSize = CGSizeMake(innerW, scrollContent);
    _scroll.scrollEnabled = scrollContent > scrollH + 0.5f;

    CGFloat by = topPad + titleH + scrollH + buttonsGap;
    if (count) {
        if (stacked) {
            /* the action first, the way out last */
            NSMutableArray *order = [NSMutableArray array];
            for (SenkoDialogButton *b in [_buttons reverseObjectEnumerator]) [order addObject:b];
            for (NSUInteger i = 0; i < count; ++i) {
                UIView *b = [order objectAtIndex:i];
                b.frame = CGRectMake(DIALOG_PAD, by + i * (DIALOG_BUTTON_H + 8.0f), innerW, DIALOG_BUTTON_H);
            }
        } else {
            CGFloat bw = (innerW - (count - 1) * 8.0f) / (CGFloat)count;
            NSMutableArray *order = [NSMutableArray array];
            /* a cancel button on the left, the action on the right */
            for (SenkoDialogButton *b in _buttons)
                if ([b buttonStyle] == SenkoDialogButtonCancel) [order addObject:b];
            for (SenkoDialogButton *b in _buttons)
                if ([b buttonStyle] != SenkoDialogButtonCancel) [order addObject:b];
            for (NSUInteger i = 0; i < count; ++i) {
                UIView *b = [order objectAtIndex:i];
                b.frame = CGRectMake(DIALOG_PAD + i * (bw + 8.0f), by, bw, DIALOG_BUTTON_H);
            }
        }
    }
    CGFloat cardH = by + buttonsH + 14.0f;

    CGFloat total = overlap + cardH;
    CGFloat top = floorf((bounds.size.height - _keyboardInset - total) * 0.5f);
    if (top < 8.0f) top = 8.0f;
    CGFloat left = floorf((bounds.size.width - cardW) * 0.5f);
    _card.frame = CGRectMake(left, top + overlap, cardW, cardH);
    _face.frame = CGRectMake(floorf((bounds.size.width - side) * 0.5f), top, side, side);
}

/* the card and the sticker arrive on the curves of the android build: the card
   rises on the emphasized decelerate of material 3, and the sticker pops in over
   it a moment later on the loose spring that wobbles twice */
- (void)playEntrance {
    if (_entered) return;
    _entered = YES;
    [self layoutIfNeeded];
    SenkoMotionEnter(_backdrop, CGAffineTransformIdentity, 0.0f, SENKO_MOTION_ENTER, 0.0);
    SenkoMotionEnter(_card, CGAffineTransformMakeTranslation(0.0f, 18.0f), 0.0f,
                     SENKO_MOTION_POPUP_ENTER, 0.0);
    SenkoMotionEnter(_face, CGAffineTransformMakeScale(0.4f, 0.4f), 0.0f,
                     SENKO_MOTION_WOBBLE, 0.06);
}

@end

void SenkoDialogAlert(NSString *title, NSString *message, SenkoDialogMood mood) {
    SenkoDialog *dialog = [SenkoDialog dialogWithTitle:title message:message];
    dialog.mood = mood;
    [dialog addButtonWithTitle:@"OK" style:SenkoDialogButtonNormal handler:nil];
    [dialog show];
}

void SenkoDialogConfirm(NSString *title, NSString *message, NSString *action,
                        BOOL destructive, void (^confirm)(void)) {
    SenkoDialog *dialog = [SenkoDialog dialogWithTitle:title message:message];
    [dialog addButtonWithTitle:@"Cancel" style:SenkoDialogButtonCancel handler:nil];
    [dialog addButtonWithTitle:action
                         style:destructive ? SenkoDialogButtonDestructive : SenkoDialogButtonNormal
                       handler:^(SenkoDialog *d) {
        (void)d;
        if (confirm) confirm();
    }];
    [dialog show];
}

void SenkoDialogMenu(NSString *title, NSString *message, NSArray *labels,
                     void (^pick)(NSInteger index)) {
    SenkoDialog *dialog = [SenkoDialog dialogWithTitle:title message:message];
    for (NSUInteger i = 0; i < [labels count]; ++i) {
        NSInteger index = (NSInteger)i;
        [dialog addItemWithTitle:[labels objectAtIndex:i] handler:^(SenkoDialog *d) {
            (void)d;
            if (pick) pick(index);
        }];
    }
    [dialog addButtonWithTitle:@"Cancel" style:SenkoDialogButtonCancel handler:nil];
    [dialog show];
}

void SenkoDialogPrompt(NSString *title, NSString *message, NSString *text,
                       UIKeyboardType keyboard, NSString *action,
                       void (^done)(NSString *text)) {
    SenkoDialog *dialog = [SenkoDialog dialogWithTitle:title message:message];
    [dialog addTextFieldWithText:text placeholder:nil keyboard:keyboard];
    [dialog addButtonWithTitle:@"Cancel" style:SenkoDialogButtonCancel handler:nil];
    [dialog addButtonWithTitle:action style:SenkoDialogButtonNormal handler:^(SenkoDialog *d) {
        NSString *typed = d.textField.text ? d.textField.text : @"";
        if (done) done(typed);
    }];
    [dialog show];
}
