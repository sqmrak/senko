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

@implementation EditAWGVC {

    id<EditAWGDelegate> _delegate;
    NSString *_config;
    UITextView *_textView;

}

- (id)initWithConfig:(NSString *)config delegate:(id<EditAWGDelegate>)delegate {
    if ((self = [super init])) {
        _delegate = delegate;
        _config = [config copy];
    }
    return self;
}

- (void)dealloc {
    [_config release];
    [_textView release];
    [super dealloc];
}

- (void)cancelPressed {
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)savePressed {
    NSString *text = [[_textView text] stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![text length]) return;
    [_delegate editAWGVC:self saveConfig:[text stringByAppendingString:@"\n"]];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Edit details";
    self.view.backgroundColor = kBG;
    AddVGradient(self.view, kBG, kBGBot);
    self.navigationItem.leftBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                                                       target:self action:@selector(cancelPressed)] autorelease];
    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemSave
                                                       target:self action:@selector(savePressed)] autorelease];
    CGRect b = self.view.bounds;
    UIView *plate = [[[UIView alloc] initWithFrame:CGRectInset(b, 10, 10)] autorelease];
    plate.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    plate.layer.cornerRadius = 8;
    plate.layer.borderWidth = 0;
    plate.layer.borderColor = [UIColor clearColor].CGColor;
    SenkoStyleTerminalPlate(plate);
    [self.view addSubview:plate];
    _textView = [[UITextView alloc] initWithFrame:CGRectInset(plate.bounds, 6, 6)];
    _textView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _textView.font = [UIFont fontWithName:@"Menlo" size:12] ?: [UIFont systemFontOfSize:12];
    SenkoStyleTerminalText(_textView);
    if (SenkoThemeIsLight())
        _textView.textColor = kInk; /* config text uses body color, not log green */
    _textView.text = _config;
    _textView.autocorrectionType = UITextAutocorrectionTypeNo;
    _textView.autocapitalizationType = UITextAutocapitalizationTypeNone;
    [plate addSubview:_textView];
}

@end

