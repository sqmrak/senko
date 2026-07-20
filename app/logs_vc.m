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

@implementation LogsVC {

    UITextView *_textView;

}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"System Logs";
    SenkoApplyScreenChrome(self.view);

    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemRefresh
                                                       target:self
                                                       action:@selector(loadLogs)] autorelease];

    UIView *plate = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    plate.tag = 7701;
    plate.layer.cornerRadius = 10;
    plate.layer.borderWidth = 0;
    plate.layer.borderColor = [UIColor clearColor].CGColor;
    plate.opaque = NO;
    SenkoStyleTerminalPlate(plate);
    [self.view addSubview:plate];

    _textView = [[UITextView alloc] initWithFrame:CGRectZero];
    _textView.editable = NO;
    _textView.font = [UIFont fontWithName:@"Courier" size:11.0f];
    SenkoStyleTerminalText(_textView);
    [plate addSubview:_textView];

    [self layoutLogs];
    [self loadLogs];
}

- (void)layoutLogs {
    CGRect b = SenkoViewBounds(self.view);
    if (b.size.width < 2.0f || b.size.height < 2.0f) return;
    UIView *bg = [self.view viewWithTag:9111];
    if (bg) bg.frame = b;
    UIView *plate = [self.view viewWithTag:7701];
    if (!plate) return;
    CGFloat pad = 8.0f;
    plate.frame = CGRectMake(pad, pad, b.size.width - pad * 2.0f, b.size.height - pad * 2.0f);
    _textView.frame = CGRectInset(plate.bounds, 6, 6);
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    SenkoApplyScreenChrome(self.view);
    [self layoutLogs];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutLogs];
}

- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)io
                                         duration:(NSTimeInterval)dur {
    (void)io; (void)dur;
    [self layoutLogs];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)io {
    (void)io;
    [self layoutLogs];
}

- (void)dealloc {
    [_textView release];
    [super dealloc];
}

- (void)loadLogs {
    NSString *vless = [NSString stringWithContentsOfFile:@"/var/log/senkod.log"
                                                  encoding:NSUTF8StringEncoding error:nil];
    NSString *awg = [NSString stringWithContentsOfFile:@"/var/log/senkoawgd.log"
                                                encoding:NSUTF8StringEncoding error:nil];
    if (![vless length] && ![awg length]) {
        _textView.text = @"No daemon logs available";
    } else {
        NSString *content = [NSString stringWithFormat:@"[senkod]\n%@\n[senkoawgd]\n%@",
                             vless ? vless : @"(no log)", awg ? awg : @"(no log)"];
        if (content.length > 20000) {
            content = [content substringFromIndex:content.length - 20000];
        }
        _textView.text = content;
        if (_textView.text.length > 0) {
            NSRange range = NSMakeRange(_textView.text.length - 1, 1);
            [_textView scrollRangeToVisible:range];
        }
    }
}

@end

