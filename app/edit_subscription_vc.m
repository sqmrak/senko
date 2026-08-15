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

static NSString *SenkoSubscriptionHWID(void) {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSString *value = [defaults stringForKey:@"SenkoSubscriptionHWID"];
    if ([value length]) return value;

    CFUUIDRef uuid = CFUUIDCreate(NULL);
    NSString *created = [(NSString *)CFUUIDCreateString(NULL, uuid) autorelease];
    CFRelease(uuid);
    [defaults setObject:created forKey:@"SenkoSubscriptionHWID"];
    [defaults synchronize];
    return created;
}

static BOOL SenkoCookieHeader(NSString *header) {
    NSRange colon = [header rangeOfString:@":"];
    if (colon.location == NSNotFound) return [header length] == 0;
    NSString *name = [[header substringToIndex:colon.location]
                       stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return [name caseInsensitiveCompare:@"Cookie"] == NSOrderedSame;
}

static BOOL SenkoCookieTokenHWID(NSString *token) {
    NSRange equal = [token rangeOfString:@"="];
    NSString *name = equal.location == NSNotFound ? token :
        [token substringToIndex:equal.location];
    name = [name stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return [name caseInsensitiveCompare:@"HWID"] == NSOrderedSame;
}

static NSString *SenkoHeaderWithHWID(NSString *header, BOOL enabled, BOOL *compatible) {
    if (compatible) *compatible = YES;
    if (!enabled && !SenkoCookieHeader(header)) return header ? header : @"";
    if (enabled && !SenkoCookieHeader(header)) {
        if (compatible) *compatible = NO;
        return header ? header : @"";
    }

    NSString *prefix = @"Cookie: ";
    NSString *value = @"";
    if ([header length]) {
        NSRange colon = [header rangeOfString:@":"];
        value = [[header substringFromIndex:colon.location + 1]
                 stringByTrimmingCharactersInSet:
                 [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    }

    NSMutableArray *tokens = [NSMutableArray array];
    for (NSString *raw in [value componentsSeparatedByString:@";"]) {
        NSString *token = [raw stringByTrimmingCharactersInSet:
                           [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (![token length] || SenkoCookieTokenHWID(token)) continue;
        [tokens addObject:token];
    }
    if (enabled)
        [tokens addObject:[NSString stringWithFormat:@"HWID=%@", SenkoSubscriptionHWID()]];
    if (![tokens count]) return @"";
    return [prefix stringByAppendingString:[tokens componentsJoinedByString:@"; "]];
}

@interface EditSubscriptionVC () <UITextFieldDelegate>
@end

@implementation EditSubscriptionVC {

    id<EditSubscriptionDelegate> _delegate;
    int _subIdx;
    NSString *_name;
    NSString *_url;
    NSString *_header;
    UIScrollView *_scroll;
    UIView *_plate;
    UITextField *_nameField;
    UITextField *_urlField;
    UISwitch *_hwidSwitch;
    UILabel *_sectionLbl;
    UIView *_nameLine;
    UIView *_urlLine;
    UITextField *_headerField;
    UIView *_headerLine;

}

- (id)initWithSub:(SenkoSub *)sub delegate:(id<EditSubscriptionDelegate>)delegate {
    if ((self = [super init])) {
        _delegate = delegate;
        _subIdx = sub ? sub->index : -1;
        _name = [(sub && sub->name) ? sub->name : @"" copy];
        _url = [(sub && sub->url) ? sub->url : @"" copy];
        _header = [(sub && sub->header) ? sub->header : @"" copy];
    }
    return self;
}

- (void)dealloc {
    [_name release];
    [_url release];
    [_header release];
    [_scroll release];
    [_plate release];
    [_nameField release];
    [_urlField release];
    [_hwidSwitch release];
    [_sectionLbl release];
    [_nameLine release];
    [_urlLine release];
    [_headerField release];
    [_headerLine release];
    [super dealloc];
}

- (void)cancelPressed {
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)savePressed {
    [_nameField resignFirstResponder];
    [_urlField resignFirstResponder];
    [_headerField resignFirstResponder];
    NSString *name = [[_nameField text] stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *url = [[_urlField text] stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *header = [[_headerField text] stringByTrimmingCharactersInSet:
                        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![name length] || ![url length]) {
        UIAlertView *alert = [[[UIAlertView alloc]
            initWithTitle:SenkoLocalizedText(@"Error")
                  message:SenkoLocalizedText(@"name and url required")
                 delegate:nil cancelButtonTitle:SenkoLocalizedText(@"OK")
            otherButtonTitles:nil] autorelease];
        [alert show];
        return;
    }
    BOOL compatible = YES;
    header = SenkoHeaderWithHWID(header, _hwidSwitch.on, &compatible);
    if (!compatible) {
        UIAlertView *alert = [[[UIAlertView alloc]
            initWithTitle:SenkoLocalizedText(@"Error")
                  message:SenkoLocalizedText(@"HWID requires a Cookie request header")
                 delegate:nil cancelButtonTitle:SenkoLocalizedText(@"OK")
            otherButtonTitles:nil] autorelease];
        [alert show];
        return;
    }
    if (_delegate)
        [_delegate editSubscriptionVC:self saveSubWithIndex:_subIdx name:name url:url header:header];
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

/* keep the only switch connected to the stored request header */
- (void)addHwidSwitchTo:(UIView *)parent y:(CGFloat)y w:(CGFloat)w {
    UILabel *label = [self labelWithFrame:CGRectMake(18, y + 18, w - 112, 28)
                                     text:SenkoLocalizedText(@"Send HWID in Cookie")
                                    color:kInk size:18 bold:YES];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [parent addSubview:label];

    _hwidSwitch = [[UISwitch alloc] initWithFrame:CGRectZero];
    CGRect f = _hwidSwitch.frame;
    f.origin.x = w - f.size.width - 20;
    f.origin.y = y + 14;
    _hwidSwitch.frame = f;
    _hwidSwitch.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
    _hwidSwitch.on = SenkoCookieTokenHWID(_header) ||
                     [[_header lowercaseString] rangeOfString:@"hwid="].location != NSNotFound;
    [_hwidSwitch addTarget:self action:@selector(hwidChanged:)
          forControlEvents:UIControlEventValueChanged];
    [parent addSubview:_hwidSwitch];

    UIView *line = [[[UIView alloc] initWithFrame:CGRectMake(0, y + 63, w, 1)] autorelease];
    line.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.12]
        : [UIColor colorWithWhite:1 alpha:0.18];
}

- (void)layoutEditForm {
    CGRect b = self.view.bounds;
    if (b.size.width < 1.0f || b.size.height < 1.0f) return;

    _scroll.frame = b;

    CGFloat contentW = b.size.width;
    if (contentW > 620.0f) contentW = 620.0f;
    CGFloat x = floorf((b.size.width - contentW) * 0.5f);
    const CGFloat plateH = 410.0f;
    _plate.frame = CGRectMake(x, 0, contentW, plateH);
    _scroll.contentSize = CGSizeMake(b.size.width, plateH + 24.0f);

    CGFloat fieldW = contentW - 36.0f;
    _sectionLbl.frame = CGRectMake(18, 80, fieldW, 28);
    _nameField.frame = CGRectMake(18, 125, fieldW, 42);
    _nameLine.frame = CGRectMake(0, 179, contentW, 1);
    _urlField.frame = CGRectMake(18, 197, fieldW, 42);
    _urlLine.frame = CGRectMake(0, 251, contentW, 1);
    _headerField.frame = CGRectMake(18, 267, fieldW, 48);
    _headerLine.frame = CGRectMake(0, 337, contentW, 1);
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

    _plate = [[UIView alloc] initWithFrame:CGRectMake(0, 0, contentW, 410)];
    _plate.backgroundColor = [UIColor clearColor];
    _plate.autoresizingMask = UIViewAutoresizingNone;
    [_scroll addSubview:_plate];

    [self addHwidSwitchTo:_plate y:0 w:contentW];

    _sectionLbl = [[self labelWithFrame:CGRectMake(18, 80, contentW - 36, 28)
                                   text:SenkoLocalizedText(@"Title and URL")
                                  color:kAccentBlue
                                   size:18
                                   bold:YES] retain];
    [_plate addSubview:_sectionLbl];

    _nameField = [[UITextField alloc] initWithFrame:CGRectMake(18, 125, contentW - 36, 42)];
    _nameField.backgroundColor = [UIColor clearColor];
    _nameField.textColor = kInk;
    _nameField.font = [UIFont boldSystemFontOfSize:21];
    _nameField.placeholder = @"Name";
    _nameField.text = _name;
    _nameField.delegate = self;
    _nameField.clearButtonMode = UITextFieldViewModeWhileEditing;
    _nameField.returnKeyType = UIReturnKeyNext;
    [_plate addSubview:_nameField];

    _nameLine = [[UIView alloc] initWithFrame:CGRectMake(0, 179, contentW, 1)];
    _nameLine.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14]
        : [UIColor colorWithWhite:1 alpha:0.28];
    [_plate addSubview:_nameLine];

    _urlField = [[UITextField alloc] initWithFrame:CGRectMake(18, 197, contentW - 36, 42)];
    _urlField.backgroundColor = [UIColor clearColor];
    _urlField.textColor = kInk;
    _urlField.font = [UIFont systemFontOfSize:15];
    _urlField.placeholder = SenkoLocalizedText(@"Subscription URL");
    _urlField.text = _url;
    _urlField.delegate = self;
    _urlField.clearButtonMode = UITextFieldViewModeWhileEditing;
    _urlField.keyboardType = UIKeyboardTypeURL;
    _urlField.autocorrectionType = UITextAutocorrectionTypeNo;
    _urlField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _urlField.returnKeyType = UIReturnKeyDone;
    [_plate addSubview:_urlField];

    _urlLine = [[UIView alloc] initWithFrame:CGRectMake(0, 251, contentW, 1)];
    _urlLine.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14]
        : [UIColor colorWithWhite:1 alpha:0.28];
    [_plate addSubview:_urlLine];

    _headerField = [[UITextField alloc] initWithFrame:CGRectMake(18, 267, contentW - 36, 48)];
    _headerField.backgroundColor = [UIColor clearColor];
    _headerField.textColor = kInk;
    _headerField.font = [UIFont systemFontOfSize:15];
    _headerField.placeholder = SenkoLocalizedText(@"Header: value");
    _headerField.text = _header;
    _headerField.delegate = self;
    _headerField.clearButtonMode = UITextFieldViewModeWhileEditing;
    _headerField.autocorrectionType = UITextAutocorrectionTypeNo;
    _headerField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _headerField.returnKeyType = UIReturnKeyDone;
    [_plate addSubview:_headerField];

    _headerLine = [[UIView alloc] initWithFrame:CGRectMake(0, 337, contentW, 1)];
    _headerLine.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14]
        : [UIColor colorWithWhite:1 alpha:0.28];
    [_plate addSubview:_headerLine];

    [self layoutEditForm];
}

- (void)hwidChanged:(UISwitch *)sender {
    BOOL compatible = YES;
    NSString *header = SenkoHeaderWithHWID(_headerField.text, sender.on, &compatible);
    if (!compatible) {
        sender.on = NO;
        UIAlertView *alert = [[[UIAlertView alloc]
            initWithTitle:SenkoLocalizedText(@"Error")
                  message:SenkoLocalizedText(@"HWID requires a Cookie request header")
                 delegate:nil cancelButtonTitle:SenkoLocalizedText(@"OK")
            otherButtonTitles:nil] autorelease];
        [alert show];
        return;
    }
    _headerField.text = header;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutEditForm];
}

- (BOOL)textFieldShouldReturn:(UITextField *)tf {
    if (tf == _nameField) {
        [_urlField becomeFirstResponder];
    } else if (tf == _urlField) {
        [_headerField becomeFirstResponder];
    } else {
        [tf resignFirstResponder];
    }
    return YES;
}

@end
