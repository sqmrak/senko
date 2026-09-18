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
#import "home_layout.h"
#import "update_install.h"
#import "meow.h"
#import "app_common.h"
#include "../daemon/core/b64.h"

@interface EditServerVC () <UITextFieldDelegate>
@end

@implementation EditServerVC {

    id<EditServerDelegate> _delegate;
    int _index;
    NSString *_link;
    UIScrollView *_scroll;
    UIView *_body;
    UISegmentedControl *_proto;
    UITextField *_address;
    UITextField *_port;
    UITextField *_uuid;
    UITextField *_flow;
    UITextField *_sni;
    UITextField *_fingerprint;
    UITextField *_path;
    UITextField *_remark;
    UISegmentedControl *_security;
    UISegmentedControl *_transport;
    UILabel *_lblProto;
    UILabel *_lblAddress;
    UILabel *_lblPort;
    UILabel *_lblUuid;
    UILabel *_lblFlow;
    UILabel *_lblPath;
    UILabel *_lblSni;
    UILabel *_lblFp;
    UILabel *_lblRemark;
    UITextField *_activeField;

}

- (id)initWithLink:(NSString *)link index:(int)idx delegate:(id<EditServerDelegate>)delegate {
    if ((self = [super init])) {
        _delegate = delegate;
        _index = idx;
        _link = [link copy];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_link release];
    [_scroll release];
    [_body release];
    [_proto release];
    [_address release]; [_port release]; [_uuid release]; [_flow release];
    [_sni release]; [_fingerprint release]; [_path release]; [_remark release];
    [_security release]; [_transport release];
    [_lblProto release];
    [_lblAddress release]; [_lblPort release]; [_lblUuid release];
    [_lblFlow release]; [_lblPath release]; [_lblSni release];
    [_lblFp release]; [_lblRemark release];
    [super dealloc];
}

- (NSString *)queryValue:(NSString *)key query:(NSString *)query {
    for (NSString *part in [query componentsSeparatedByString:@"&"]) {
        NSArray *pair = [part componentsSeparatedByString:@"="];
        if ([pair count] < 2) continue;
        if ([[pair objectAtIndex:0] isEqualToString:key])
            return [[pair objectAtIndex:1] stringByReplacingOccurrencesOfString:@"%2F" withString:@"/"];
    }
    return @"";
}

- (UILabel *)makeLabel:(NSString *)title {
    UILabel *label = [[UILabel alloc] initWithFrame:CGRectZero];
    label.backgroundColor = [UIColor clearColor];
    label.font = [UIFont boldSystemFontOfSize:11];
    SenkoStyleMutedLabel(label);
    label.text = title;
    return label;
}

- (UITextField *)makeField {
    UITextField *field = [[UITextField alloc] initWithFrame:CGRectZero];
    field.font = [UIFont systemFontOfSize:15];
    field.autocorrectionType = UITextAutocorrectionTypeNo;
    field.autocapitalizationType = UITextAutocapitalizationTypeNone;
    field.clearButtonMode = UITextFieldViewModeWhileEditing;
    field.delegate = self;
    SenkoStyleGlassField(field);
    return field;
}

- (void)protoChanged {
    NSInteger p = _proto.selectedSegmentIndex;
    if (p == 0) {
        _lblUuid.text = SenkoLocalizedText(@"UUID");
        _lblFlow.text = SenkoLocalizedText(@"FLOW");
    } else if (p == 1) {
        _lblUuid.text = SenkoLocalizedText(@"PASSWORD");
        _lblFlow.text = SenkoLocalizedText(@"FLOW");
    } else if (p == 2) {
        _lblUuid.text = SenkoLocalizedText(@"PASSWORD");
        _lblFlow.text = SenkoLocalizedText(@"CIPHER");
        if (![_flow.text length]) _flow.text = @"aes-256-gcm";
    }
    [self layoutEditForm];
}

- (void)cancelPressed {
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)savePressed {
    NSString *address = [_address.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *port = [_port.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *uuid = [_uuid.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![address length] || ![port length] || ![uuid length]) return;

    if (_proto.selectedSegmentIndex == 1) {
        NSArray *transportValues = [NSArray arrayWithObjects:@"tcp", @"ws", @"xhttp", @"grpc", nil];
        NSString *transport = [transportValues objectAtIndex:_transport.selectedSegmentIndex];
        NSMutableString *uri = [NSMutableString stringWithFormat:@"trojan://%@@%@:%@?security=tls&type=%@",
                                uuid, address, port, [transport lowercaseString]];
        if ([_sni.text length]) [uri appendFormat:@"&sni=%@", _sni.text];
        if ([_path.text length]) [uri appendFormat:@"&path=%@", _path.text];
        if ([_remark.text length]) [uri appendFormat:@"#%@", _remark.text];
        [_delegate editServerVC:self saveLink:uri index:_index];
        return;
    }

    if (_proto.selectedSegmentIndex == 2) {
        NSString *cipher = [_flow.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (![cipher length]) cipher = @"aes-256-gcm";
        NSString *userinfo = [NSString stringWithFormat:@"%@:%@@%@:%@", cipher, uuid, address, port];
        const char *u_raw = [userinfo UTF8String];
        char b64[512];
        size_t b64_len = 0;
        if (b64_encode((const unsigned char *)u_raw, strlen(u_raw), b64, sizeof(b64), &b64_len) == 0) {
            NSMutableString *uri = [NSMutableString stringWithFormat:@"ss://%s", b64];
            if ([_remark.text length]) [uri appendFormat:@"#%@", _remark.text];
            [_delegate editServerVC:self saveLink:uri index:_index];
        }
        return;
    }

    NSArray *securityValues = [NSArray arrayWithObjects:@"none", @"tls", @"reality", nil];
    NSArray *transportValues = [NSArray arrayWithObjects:@"tcp", @"ws", @"xhttp", @"grpc", nil];
    NSString *security = [securityValues objectAtIndex:_security.selectedSegmentIndex];
    NSString *transport = [transportValues objectAtIndex:_transport.selectedSegmentIndex];
    NSMutableString *uri = [NSMutableString stringWithFormat:@"vless://%@@%@:%@?security=%@&type=%@",
                            uuid, address, port, [security lowercaseString], [transport lowercaseString]];
    NSArray *keys = [NSArray arrayWithObjects:@"flow", @"sni", @"fp", @"path", nil];
    NSArray *values = [NSArray arrayWithObjects:_flow.text, _sni.text, _fingerprint.text, _path.text, nil];
    for (NSUInteger i = 0; i < [keys count]; ++i) {
        NSString *value = [values objectAtIndex:i];
        if ([value length])
            [uri appendFormat:@"&%@=%@", [keys objectAtIndex:i], value];
    }
    if ([_remark.text length]) [uri appendFormat:@"#%@", _remark.text];
    [_delegate editServerVC:self saveLink:uri index:_index];
}

- (void)layoutEditForm {
    CGRect b = SenkoViewBounds(self.view);
    if (b.size.width < 2.0f || b.size.height < 2.0f) return;

    UIView *bg = [self.view viewWithTag:9111];
    if (bg) bg.frame = b;
    _scroll.frame = CGRectMake(0, 0, b.size.width, b.size.height);

    CGFloat contentW = b.size.width;
    if (contentW > 700.0f) contentW = 700.0f;
    CGFloat x = floorf((b.size.width - contentW) * 0.5f);
    CGFloat side = 18.0f;
    CGFloat fieldW = contentW - side * 2.0f;
    if (fieldW < 80.0f) fieldW = 80.0f;

    CGFloat y = 14.0f;
    CGFloat rowH = 56.0f; /* label 18 + gap + field 28 + pad */

    _lblProto.frame = CGRectMake(side, y, fieldW, 16);
    _proto.frame = CGRectMake(side, y + 16, fieldW, 32);
    y += 56.0f;

    _lblAddress.frame = CGRectMake(side, y, fieldW, 16);
    _address.frame = CGRectMake(side, y + 16, fieldW, 30);
    y += rowH;

    _lblPort.frame = CGRectMake(side, y, fieldW, 16);
    _port.frame = CGRectMake(side, y + 16, fieldW, 30);
    y += rowH;

    _lblUuid.frame = CGRectMake(side, y, fieldW, 16);
    _uuid.frame = CGRectMake(side, y + 16, fieldW, 30);
    y += rowH;

    NSInteger p = _proto.selectedSegmentIndex;
    if (p == 0) {
        _transport.hidden = NO;
        _lblFlow.hidden = NO; _flow.hidden = NO;
        _lblPath.hidden = NO; _path.hidden = NO;
        _security.hidden = NO;
        _lblSni.hidden = NO; _sni.hidden = NO;
        _lblFp.hidden = NO; _fingerprint.hidden = NO;

        _transport.frame = CGRectMake(side, y, fieldW, 32);
        y += 40.0f;

        _lblFlow.frame = CGRectMake(side, y, fieldW, 16);
        _flow.frame = CGRectMake(side, y + 16, fieldW, 30);
        y += rowH;

        _lblPath.frame = CGRectMake(side, y, fieldW, 16);
        _path.frame = CGRectMake(side, y + 16, fieldW, 30);
        y += rowH;

        _security.frame = CGRectMake(side, y, fieldW, 32);
        y += 40.0f;

        _lblSni.frame = CGRectMake(side, y, fieldW, 16);
        _sni.frame = CGRectMake(side, y + 16, fieldW, 30);
        y += rowH;

        _lblFp.frame = CGRectMake(side, y, fieldW, 16);
        _fingerprint.frame = CGRectMake(side, y + 16, fieldW, 30);
        y += rowH;
    } else if (p == 1) {
        _transport.hidden = NO;
        _lblFlow.hidden = YES; _flow.hidden = YES;
        _lblPath.hidden = NO; _path.hidden = NO;
        _security.hidden = YES;
        _lblSni.hidden = NO; _sni.hidden = NO;
        _lblFp.hidden = YES; _fingerprint.hidden = YES;

        _transport.frame = CGRectMake(side, y, fieldW, 32);
        y += 40.0f;

        _lblPath.frame = CGRectMake(side, y, fieldW, 16);
        _path.frame = CGRectMake(side, y + 16, fieldW, 30);
        y += rowH;

        _lblSni.frame = CGRectMake(side, y, fieldW, 16);
        _sni.frame = CGRectMake(side, y + 16, fieldW, 30);
        y += rowH;
    } else {
        _transport.hidden = YES;
        _lblFlow.hidden = NO; _flow.hidden = NO;
        _lblPath.hidden = YES; _path.hidden = YES;
        _security.hidden = YES;
        _lblSni.hidden = YES; _sni.hidden = YES;
        _lblFp.hidden = YES; _fingerprint.hidden = YES;

        _lblFlow.frame = CGRectMake(side, y, fieldW, 16);
        _flow.frame = CGRectMake(side, y + 16, fieldW, 30);
        y += rowH;
    }

    _lblRemark.frame = CGRectMake(side, y, fieldW, 16);
    _remark.frame = CGRectMake(side, y + 16, fieldW, 30);
    y += rowH + 20.0f;

    _body.frame = CGRectMake(x, 0, contentW, y);
    _scroll.contentSize = CGSizeMake(b.size.width, y + 24.0f);
/* never leave a leftover horizontal offset after rotation */
    if (_scroll.contentOffset.x != 0)
        _scroll.contentOffset = CGPointMake(0, _scroll.contentOffset.y);
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"Edit server");
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    SenkoApplyScreenChrome(self.view);
    self.navigationItem.leftBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                                                       target:self action:@selector(cancelPressed)] autorelease];
    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemSave
                                                       target:self action:@selector(savePressed)] autorelease];

    CGRect b = SenkoViewBounds(self.view);
    _scroll = [[UIScrollView alloc] initWithFrame:b];
    SenkoScrollViewUseManualInsets(_scroll);
    _scroll.alwaysBounceVertical = YES;
    _scroll.showsHorizontalScrollIndicator = NO;
    _scroll.backgroundColor = [UIColor clearColor];
    UITapGestureRecognizer *tap = [[[UITapGestureRecognizer alloc]
        initWithTarget:self action:@selector(dismissKeyboard)] autorelease];
    tap.cancelsTouchesInView = NO;
    [_scroll addGestureRecognizer:tap];
    [self.view addSubview:_scroll];

    _body = [[UIView alloc] initWithFrame:CGRectMake(0, 0, b.size.width > 1 ? b.size.width : 320, 700)];
    _body.backgroundColor = [UIColor clearColor];
    [_scroll addSubview:_body];

    NSURL *url = [NSURL URLWithString:_link];
    NSString *query = [url query] ? [url query] : @"";

    _lblProto = [self makeLabel:SenkoLocalizedText(@"PROTOCOL")];
    _proto = [[UISegmentedControl alloc] initWithItems:
              [NSArray arrayWithObjects:@"VLESS", @"Trojan", @"Shadowsocks", nil]];
    SenkoStyleGlassSegmented(_proto);
    [_proto addTarget:self action:@selector(protoChanged) forControlEvents:UIControlEventValueChanged];

    _lblAddress = [self makeLabel:SenkoLocalizedText(@"ADDRESS")];
    _address = [self makeField];
    _address.returnKeyType = UIReturnKeyNext;
    _lblPort = [self makeLabel:SenkoLocalizedText(@"PORT")];
    _port = [self makeField];
    _port.returnKeyType = UIReturnKeyNext;
    _lblUuid = [self makeLabel:SenkoLocalizedText(@"UUID")];
    _uuid = [self makeField];
    _uuid.returnKeyType = UIReturnKeyNext;
    _lblFlow = [self makeLabel:SenkoLocalizedText(@"FLOW")];
    _flow = [self makeField];
    _flow.returnKeyType = UIReturnKeyNext;
    _lblPath = [self makeLabel:SenkoLocalizedText(@"PATH")];
    _path = [self makeField];
    _path.returnKeyType = UIReturnKeyNext;
    _lblSni = [self makeLabel:SenkoLocalizedText(@"SNI")];
    _sni = [self makeField];
    _sni.returnKeyType = UIReturnKeyNext;
    _lblFp = [self makeLabel:SenkoLocalizedText(@"FINGERPRINT")];
    _fingerprint = [self makeField];
    _fingerprint.returnKeyType = UIReturnKeyNext;
    _lblRemark = [self makeLabel:SenkoLocalizedText(@"NAME")];
    _remark = [self makeField];
    _remark.returnKeyType = UIReturnKeyDone;

    _transport = [[UISegmentedControl alloc] initWithItems:
                  [NSArray arrayWithObjects:@"TCP", @"WS", @"XHTTP", @"gRPC", nil]];
    SenkoStyleGlassSegmented(_transport);

    _security = [[UISegmentedControl alloc] initWithItems:
                 [NSArray arrayWithObjects:SenkoLocalizedText(@"None"), @"TLS", @"REALITY", nil]];
    SenkoStyleGlassSegmented(_security);

    UIView *subs[] = {
        _lblProto, _proto,
        _lblAddress, _address, _lblPort, _port, _lblUuid, _uuid,
        _transport, _lblFlow, _flow, _lblPath, _path,
        _security, _lblSni, _sni, _lblFp, _fingerprint, _lblRemark, _remark
    };
    for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); ++i)
        [_body addSubview:subs[i]];

    if ([_link hasPrefix:@"ss://"]) {
        _proto.selectedSegmentIndex = 2;
        _lblUuid.text = SenkoLocalizedText(@"PASSWORD");
        _lblFlow.text = SenkoLocalizedText(@"CIPHER");
        NSString *ssPart = [_link substringFromIndex:5];
        NSRange hashR = [ssPart rangeOfString:@"#"];
        NSString *remark = @"";
        if (hashR.location != NSNotFound) {
            remark = [ssPart substringFromIndex:hashR.location + 1];
            ssPart = [ssPart substringToIndex:hashR.location];
        }
        _remark.text = [remark stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
        const char *b64in = [ssPart UTF8String];
        unsigned char dec[512];
        size_t dec_len = 0;
        if (b64_decode(b64in, strlen(b64in), dec, sizeof(dec) - 1, &dec_len) == 0) {
            dec[dec_len] = '\0';
            NSString *decoded = [NSString stringWithUTF8String:(char *)dec];
            NSRange atR = [decoded rangeOfString:@"@"];
            if (atR.location != NSNotFound) {
                NSString *cred = [decoded substringToIndex:atR.location];
                NSString *hp = [decoded substringFromIndex:atR.location + 1];
                NSRange colonC = [cred rangeOfString:@":"];
                if (colonC.location != NSNotFound) {
                    _flow.text = [cred substringToIndex:colonC.location];
                    _uuid.text = [cred substringFromIndex:colonC.location + 1];
                }
                NSRange colonHP = [hp rangeOfString:@":" options:NSBackwardsSearch];
                if (colonHP.location != NSNotFound) {
                    _address.text = [hp substringToIndex:colonHP.location];
                    _port.text = [hp substringFromIndex:colonHP.location + 1];
                }
            }
        }
        if (![_flow.text length]) _flow.text = @"aes-256-gcm";
    } else if ([_link hasPrefix:@"trojan://"]) {
        _proto.selectedSegmentIndex = 1;
        _lblUuid.text = SenkoLocalizedText(@"PASSWORD");
        NSURL *url = [NSURL URLWithString:_link];
        NSString *query = [url query] ? [url query] : @"";
        _address.text = [url host];
        _port.text = [[url port] stringValue];
        _uuid.text = [url user];
        _sni.text = [self queryValue:@"sni" query:query];
        _path.text = [self queryValue:@"path" query:query];
        NSString *type = [self queryValue:@"type" query:query];
        _transport.selectedSegmentIndex = [type isEqualToString:@"ws"] ? 1 :
            ([type isEqualToString:@"xhttp"] ? 2 :
             ([type isEqualToString:@"grpc"] ? 3 : 0));
        _remark.text = [url fragment];
    } else {
        _proto.selectedSegmentIndex = 0;
        _lblUuid.text = SenkoLocalizedText(@"UUID");
        _lblFlow.text = SenkoLocalizedText(@"FLOW");
        NSURL *url = [NSURL URLWithString:_link];
        NSString *query = [url query] ? [url query] : @"";
        _address.text = [url host];
        _port.text = [[url port] stringValue];
        _uuid.text = [url user];
        _flow.text = [self queryValue:@"flow" query:query];
        _path.text = [self queryValue:@"path" query:query];
        NSString *type = [self queryValue:@"type" query:query];
        if ([type isEqualToString:@"grpc"] && ![_path.text length])
            _path.text = [self queryValue:@"serviceName" query:query];
        _sni.text = [self queryValue:@"sni" query:query];
        _fingerprint.text = [self queryValue:@"fp" query:query];
        _remark.text = [url fragment];
        _transport.selectedSegmentIndex = [type isEqualToString:@"ws"] ? 1 :
            ([type isEqualToString:@"xhttp"] ? 2 :
             ([type isEqualToString:@"grpc"] ? 3 : 0));
        NSString *sec = [self queryValue:@"security" query:query];
        _security.selectedSegmentIndex = [sec isEqualToString:@"tls"] ? 1 :
            ([sec isEqualToString:@"reality"] ? 2 : 0);
    }

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(keyboardChanged:)
                                                 name:UIKeyboardWillShowNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(keyboardChanged:)
                                                 name:UIKeyboardWillHideNotification
                                               object:nil];

    [self layoutEditForm];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    SenkoApplyScreenChrome(self.view);
    SenkoStyleGlassSegmented(_proto);
    SenkoStyleGlassSegmented(_transport);
    SenkoStyleGlassSegmented(_security);
    SenkoStyleGlassField(_address);
    SenkoStyleGlassField(_port);
    SenkoStyleGlassField(_uuid);
    SenkoStyleGlassField(_flow);
    SenkoStyleGlassField(_path);
    SenkoStyleGlassField(_sni);
    SenkoStyleGlassField(_fingerprint);
    SenkoStyleGlassField(_remark);
    [self layoutEditForm];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutEditForm];
}

- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)io
                                         duration:(NSTimeInterval)dur {
    (void)io; (void)dur;
    [self layoutEditForm];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io {
    (void)io;
    [self layoutEditForm];
}

- (void)dismissKeyboard {
    [self.view endEditing:YES];
}

- (void)keyboardChanged:(NSNotification *)note {
    CGFloat keyboard = 0.0f;
    if ([note.name isEqualToString:UIKeyboardWillShowNotification]) {
        NSValue *value = [note.userInfo objectForKey:UIKeyboardFrameEndUserInfoKey];
        CGRect frame = [self.view convertRect:[value CGRectValue] fromView:nil];
        CGFloat overlap = CGRectGetHeight(self.view.bounds) - CGRectGetMinY(frame);
        if (overlap > 0.0f) keyboard = overlap;
    }
    NSTimeInterval duration = 0.25;
    UIViewAnimationCurve curve = UIViewAnimationCurveEaseInOut;
    NSNumber *durNum = [note.userInfo objectForKey:UIKeyboardAnimationDurationUserInfoKey];
    if (durNum) duration = [durNum doubleValue];
    NSNumber *curvNum = [note.userInfo objectForKey:UIKeyboardAnimationCurveUserInfoKey];
    if (curvNum) curve = (UIViewAnimationCurve)[curvNum integerValue];

    [UIView beginAnimations:@"keyboard" context:NULL];
    [UIView setAnimationDuration:duration];
    [UIView setAnimationCurve:curve];
    _scroll.contentInset = UIEdgeInsetsMake(0, 0, keyboard, 0);
    _scroll.scrollIndicatorInsets = UIEdgeInsetsMake(0, 0, keyboard, 0);
    [UIView commitAnimations];

    if (keyboard > 0.0f && _activeField) {
        CGRect r = [_scroll convertRect:_activeField.bounds fromView:_activeField];
        [_scroll scrollRectToVisible:CGRectInset(r, 0, -20.0f) animated:YES];
    }
}

- (void)textFieldDidBeginEditing:(UITextField *)tf {
    _activeField = tf;
    CGRect r = [_scroll convertRect:tf.bounds fromView:tf];
    [_scroll scrollRectToVisible:CGRectInset(r, 0, -20.0f) animated:YES];
}

- (void)textFieldDidEndEditing:(UITextField *)tf {
    if (_activeField == tf) _activeField = nil;
}

- (BOOL)textFieldShouldReturn:(UITextField *)tf {
    if (tf == _address) [_port becomeFirstResponder];
    else if (tf == _port) [_uuid becomeFirstResponder];
    else if (tf == _uuid) [_flow becomeFirstResponder];
    else if (tf == _flow) [_path becomeFirstResponder];
    else if (tf == _path) [_sni becomeFirstResponder];
    else if (tf == _sni) [_fingerprint becomeFirstResponder];
    else if (tf == _fingerprint) [_remark becomeFirstResponder];
    else [tf resignFirstResponder];
    return YES;
}

@end
