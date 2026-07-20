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

@interface EditServerVC () <UITextFieldDelegate>
@end

@implementation EditServerVC {

    id<EditServerDelegate> _delegate;
    int _index;
    NSString *_link;
    UIScrollView *_scroll;
    UIView *_body;
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
    UILabel *_lblAddress;
    UILabel *_lblPort;
    UILabel *_lblUuid;
    UILabel *_lblFlow;
    UILabel *_lblPath;
    UILabel *_lblSni;
    UILabel *_lblFp;
    UILabel *_lblRemark;

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
    [_link release];
    [_scroll release];
    [_body release];
    [_address release]; [_port release]; [_uuid release]; [_flow release];
    [_sni release]; [_fingerprint release]; [_path release]; [_remark release];
    [_security release]; [_transport release];
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

- (void)cancelPressed {
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)savePressed {
    NSString *address = [_address.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *port = [_port.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *uuid = [_uuid.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![address length] || ![port length] || ![uuid length]) return;
    NSString *security = [_security titleForSegmentAtIndex:_security.selectedSegmentIndex];
    NSString *transport = [_transport titleForSegmentAtIndex:_transport.selectedSegmentIndex];
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

    _lblAddress.frame = CGRectMake(side, y, fieldW, 16);
    _address.frame = CGRectMake(side, y + 16, fieldW, 30);
    y += rowH;

    _lblPort.frame = CGRectMake(side, y, fieldW, 16);
    _port.frame = CGRectMake(side, y + 16, fieldW, 30);
    y += rowH;

    _lblUuid.frame = CGRectMake(side, y, fieldW, 16);
    _uuid.frame = CGRectMake(side, y + 16, fieldW, 30);
    y += rowH;

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
    self.title = @"Edit server";
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
    _scroll.alwaysBounceVertical = YES;
    _scroll.showsHorizontalScrollIndicator = NO;
    _scroll.backgroundColor = [UIColor clearColor];
    [self.view addSubview:_scroll];

    _body = [[UIView alloc] initWithFrame:CGRectMake(0, 0, b.size.width > 1 ? b.size.width : 320, 700)];
    _body.backgroundColor = [UIColor clearColor];
    [_scroll addSubview:_body];

    NSURL *url = [NSURL URLWithString:_link];
    NSString *query = [url query] ? [url query] : @"";

    _lblAddress = [self makeLabel:@"ADDRESS"];
    _address = [self makeField];
    _lblPort = [self makeLabel:@"PORT"];
    _port = [self makeField];
    _lblUuid = [self makeLabel:@"UUID"];
    _uuid = [self makeField];
    _lblFlow = [self makeLabel:@"FLOW"];
    _flow = [self makeField];
    _lblPath = [self makeLabel:@"PATH"];
    _path = [self makeField];
    _lblSni = [self makeLabel:@"SNI"];
    _sni = [self makeField];
    _lblFp = [self makeLabel:@"FINGERPRINT"];
    _fingerprint = [self makeField];
    _lblRemark = [self makeLabel:@"NAME"];
    _remark = [self makeField];

    _transport = [[UISegmentedControl alloc] initWithItems:
                  [NSArray arrayWithObjects:@"TCP", @"WS", @"XHTTP", nil]];
    SenkoStyleGlassSegmented(_transport);
    NSString *type = [self queryValue:@"type" query:query];
    _transport.selectedSegmentIndex = [type isEqualToString:@"ws"] ? 1 :
        ([type isEqualToString:@"xhttp"] ? 2 : 0);

    _security = [[UISegmentedControl alloc] initWithItems:
                 [NSArray arrayWithObjects:@"NONE", @"TLS", @"REALITY", nil]];
    SenkoStyleGlassSegmented(_security);
    NSString *sec = [self queryValue:@"security" query:query];
    _security.selectedSegmentIndex = [sec isEqualToString:@"tls"] ? 1 :
        ([sec isEqualToString:@"reality"] ? 2 : 0);

    UIView *subs[] = {
        _lblAddress, _address, _lblPort, _port, _lblUuid, _uuid,
        _transport, _lblFlow, _flow, _lblPath, _path,
        _security, _lblSni, _sni, _lblFp, _fingerprint, _lblRemark, _remark
    };
    for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); ++i)
        [_body addSubview:subs[i]];

    _address.text = [url host];
    _port.text = [[url port] stringValue];
    _uuid.text = [url user];
    _flow.text = [self queryValue:@"flow" query:query];
    _path.text = [self queryValue:@"path" query:query];
    _sni.text = [self queryValue:@"sni" query:query];
    _fingerprint.text = [self queryValue:@"fp" query:query];
    _remark.text = [url fragment];

    [self layoutEditForm];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    SenkoApplyScreenChrome(self.view);
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

@end
