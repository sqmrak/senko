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

@interface EditSubscriptionVC () <UITextFieldDelegate>
@end

@implementation EditSubscriptionVC {

    id<EditSubscriptionDelegate> _delegate;
    int _subIdx;
    NSString *_name;
    NSString *_url;
    UIScrollView *_scroll;
    UIView *_plate;
    UITextField *_nameField;
    UITextField *_urlField;
    UIButton *_saveBtn;
    UILabel *_sectionLbl;
    UIView *_nameLine;
    UIView *_urlLine;

}

- (id)initWithSub:(SenkoSub *)sub delegate:(id<EditSubscriptionDelegate>)delegate {
    if ((self = [super init])) {
        _delegate = delegate;
        _subIdx = sub ? sub->index : -1;
        _name = [(sub && sub->name) ? sub->name : @"" copy];
        _url = [(sub && sub->url) ? sub->url : @"" copy];
    }
    return self;
}

- (void)dealloc {
    [_name release];
    [_url release];
    [_scroll release];
    [_plate release];
    [_nameField release];
    [_urlField release];
    [_saveBtn release];
    [_sectionLbl release];
    [_nameLine release];
    [_urlLine release];
    [super dealloc];
}

- (void)cancelPressed {
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)savePressed {
    [_nameField resignFirstResponder];
    [_urlField resignFirstResponder];
    NSString *name = [[_nameField text] stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *url = [[_urlField text] stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![name length] || ![url length]) return;
    if (_delegate)
        [_delegate editSubscriptionVC:self saveSubWithIndex:_subIdx name:name url:url];
}

- (UILabel *)labelWithFrame:(CGRect)frame text:(NSString *)text color:(UIColor *)color size:(CGFloat)size bold:(BOOL)bold {
    UILabel *l = [[[UILabel alloc] initWithFrame:frame] autorelease];
    l.backgroundColor = [UIColor clearColor];
    l.font = bold ? [UIFont boldSystemFontOfSize:size] : [UIFont systemFontOfSize:size];
    if (color == kInkMuted) SenkoStyleMutedLabel(l);
    else if (color == kAccentBlue) SenkoStyleAccentLabel(l);
    else { l.textColor = color; SenkoStyleInkLabel(l); l.textColor = color; }
    l.text = text;
    return l;
}

/* fixed layout; no tag math */
- (void)addSwitchRowTo:(UIView *)parent y:(CGFloat)y w:(CGFloat)w title:(NSString *)title {
    UILabel *label = [self labelWithFrame:CGRectMake(18, y + 18, w - 112, 28)
                                     text:title color:kInk size:18 bold:YES];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [parent addSubview:label];

    UISwitch *sw = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
    CGRect f = sw.frame;
    f.origin.x = w - f.size.width - 20;
    f.origin.y = y + 14;
    sw.frame = f;
    sw.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
    sw.on = NO;
    [parent addSubview:sw];

    UIView *line = [[[UIView alloc] initWithFrame:CGRectMake(0, y + 63, w, 1)] autorelease];
    line.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.12]
        : [UIColor colorWithWhite:1 alpha:0.18];
    line.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [parent addSubview:line];
}

- (void)layoutSaveGlossy {
    if (!_saveBtn) return;
    CGFloat w = _plate.bounds.size.width;
    if (w < 1.0f) return;
    CGFloat fieldW = w - 36.0f;
    const CGFloat btnH = 50.0f;
    _saveBtn.transform = CGAffineTransformIdentity;
    _saveBtn.frame = CGRectMake(18, 418, fieldW, btnH);
/* capsule; not styleglossy on wrong size */
    StyleGlossyCapsule(_saveBtn, kAccentBlue, kAccentBlueLo);
    StyleGlossyCapsuleLayout(_saveBtn);
    [_saveBtn setTitle:@"Save" forState:UIControlStateNormal];
    SenkoStyleChromeTitle(_saveBtn);
    _saveBtn.titleLabel.font = [UIFont boldSystemFontOfSize:18];
    [_saveBtn bringSubviewToFront:_saveBtn.titleLabel];
}

- (void)layoutEditForm {
    CGRect b = self.view.bounds;
    if (b.size.width < 1.0f || b.size.height < 1.0f) return;

    _scroll.frame = b;

    CGFloat contentW = b.size.width;
    if (contentW > 620.0f) contentW = 620.0f;
    CGFloat x = floorf((b.size.width - contentW) * 0.5f);
    const CGFloat plateH = 500.0f;
    _plate.frame = CGRectMake(x, 0, contentW, plateH);
    _scroll.contentSize = CGSizeMake(b.size.width, plateH + 24.0f);

    CGFloat fieldW = contentW - 36.0f;
    _sectionLbl.frame = CGRectMake(18, 210, fieldW, 28);
    _nameField.frame = CGRectMake(18, 260, fieldW, 42);
    _nameLine.frame = CGRectMake(0, 314, contentW, 1);
    _urlField.frame = CGRectMake(18, 332, fieldW, 42);
    _urlLine.frame = CGRectMake(0, 386, contentW, 1);
    [self layoutSaveGlossy];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Edit";
    self.view.backgroundColor = kBG;
    AddVGradient(self.view, kBG, kBGBot);

    self.navigationItem.leftBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                                                       target:self
                                                       action:@selector(cancelPressed)] autorelease];
    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemSave
                                                       target:self
                                                       action:@selector(savePressed)] autorelease];

    CGRect b = self.view.bounds;
    CGFloat contentW = b.size.width > 1.0f ? b.size.width : 320.0f;
    if (contentW > 620.0f) contentW = 620.0f;

    _scroll = [[UIScrollView alloc] initWithFrame:b];
    _scroll.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _scroll.backgroundColor = [UIColor clearColor];
    _scroll.alwaysBounceVertical = YES;
    [self.view addSubview:_scroll];

    _plate = [[UIView alloc] initWithFrame:CGRectMake(0, 0, contentW, 500)];
    _plate.backgroundColor = [UIColor clearColor];
    _plate.autoresizingMask = UIViewAutoresizingNone;
    [_scroll addSubview:_plate];

    [self addSwitchRowTo:_plate y:0 w:contentW title:@"Encrypted subscription"];
    [self addSwitchRowTo:_plate y:64 w:contentW title:@"Allow insecure"];
    [self addSwitchRowTo:_plate y:128 w:contentW title:@"Send HWID in Cookie"];

    _sectionLbl = [[self labelWithFrame:CGRectMake(18, 210, contentW - 36, 28)
                                   text:@"Title and URL"
                                  color:kAccentBlue
                                   size:18
                                   bold:YES] retain];
    [_plate addSubview:_sectionLbl];

    _nameField = [[UITextField alloc] initWithFrame:CGRectMake(18, 260, contentW - 36, 42)];
    _nameField.backgroundColor = [UIColor clearColor];
    _nameField.textColor = kInk;
    _nameField.font = [UIFont boldSystemFontOfSize:21];
    _nameField.placeholder = @"Name";
    _nameField.text = _name;
    _nameField.delegate = self;
    _nameField.clearButtonMode = UITextFieldViewModeWhileEditing;
    _nameField.returnKeyType = UIReturnKeyNext;
    [_plate addSubview:_nameField];

    _nameLine = [[UIView alloc] initWithFrame:CGRectMake(0, 314, contentW, 1)];
    _nameLine.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14]
        : [UIColor colorWithWhite:1 alpha:0.28];
    [_plate addSubview:_nameLine];

    _urlField = [[UITextField alloc] initWithFrame:CGRectMake(18, 332, contentW - 36, 42)];
    _urlField.backgroundColor = [UIColor clearColor];
    _urlField.textColor = kInk;
    _urlField.font = [UIFont systemFontOfSize:15];
    _urlField.placeholder = @"Subscription URL";
    _urlField.text = _url;
    _urlField.delegate = self;
    _urlField.clearButtonMode = UITextFieldViewModeWhileEditing;
    _urlField.keyboardType = UIKeyboardTypeURL;
    _urlField.autocorrectionType = UITextAutocorrectionTypeNo;
    _urlField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _urlField.returnKeyType = UIReturnKeyDone;
    [_plate addSubview:_urlField];

    _urlLine = [[UIView alloc] initWithFrame:CGRectMake(0, 386, contentW, 1)];
    _urlLine.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14]
        : [UIColor colorWithWhite:1 alpha:0.28];
    [_plate addSubview:_urlLine];

    _saveBtn = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _saveBtn.autoresizingMask = UIViewAutoresizingNone;
    [_saveBtn addTarget:self action:@selector(savePressed) forControlEvents:UIControlEventTouchUpInside];
    [_plate addSubview:_saveBtn];

    [self layoutEditForm];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutEditForm];
}

- (BOOL)textFieldShouldReturn:(UITextField *)tf {
    if (tf == _nameField) {
        [_urlField becomeFirstResponder];
    } else {
        [tf resignFirstResponder];
    }
    return YES;
}

@end

