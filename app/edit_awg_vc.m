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

@implementation EditAWGVC {

    id<EditAWGDelegate> _delegate;
    NSString *_config;
    UITextView *_textView;
    UIView *_plate;

}

- (id)initWithConfig:(NSString *)config delegate:(id<EditAWGDelegate>)delegate {
    if ((self = [super init])) {
        _delegate = delegate;
        _config = [config copy];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_config release];
    [_textView release];
    [_plate release];
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
    self.title = SenkoLocalizedText(@"Edit details");
    self.view.backgroundColor = kBG;
    AddVGradient(self.view, kBG, kBGBot);
    self.navigationItem.leftBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                                                       target:self action:@selector(cancelPressed)] autorelease];
    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemSave
                                                       target:self action:@selector(savePressed)] autorelease];
    CGRect b = self.view.bounds;
    _plate = [[UIView alloc] initWithFrame:CGRectMake(10, 10, b.size.width - 20, b.size.height - 20)];
    _plate.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _plate.layer.cornerRadius = SenkoThemeCardRadius();
    _plate.layer.borderWidth = 0;
    _plate.layer.borderColor = [UIColor clearColor].CGColor;
    SenkoStyleTerminalPlate(_plate);
    [self.view addSubview:_plate];

    _textView = [[UITextView alloc] initWithFrame:CGRectInset(_plate.bounds, 6, 6)];
    _textView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _textView.font = [UIFont fontWithName:@"Menlo" size:12] ?: [UIFont systemFontOfSize:12];
    SenkoStyleTerminalText(_textView);
    if (SenkoThemeIsLight())
        _textView.textColor = kInk; /* config text uses body color, not log green */
    _textView.text = _config;
    _textView.autocorrectionType = UITextAutocorrectionTypeNo;
    _textView.autocapitalizationType = UITextAutocapitalizationTypeNone;

    UIToolbar *bar = [[[UIToolbar alloc] initWithFrame:CGRectMake(0, 0, b.size.width, 36)] autorelease];
    bar.barStyle = SenkoThemeIsLight() ? UIBarStyleDefault : UIBarStyleBlack;
    UIBarButtonItem *flex = [[[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil] autorelease];
    UIBarButtonItem *done = [[[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:_textView action:@selector(resignFirstResponder)] autorelease];
    bar.items = [NSArray arrayWithObjects:flex, done, nil];
    _textView.inputAccessoryView = bar;

    [_plate addSubview:_textView];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(keyboardChanged:)
                                                 name:UIKeyboardWillShowNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(keyboardChanged:)
                                                 name:UIKeyboardWillHideNotification
                                               object:nil];
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
    CGRect b = self.view.bounds;
    CGFloat h = b.size.height - 20.0f - keyboard;
    if (h < 40.0f) h = 40.0f;
    _plate.frame = CGRectMake(10, 10, b.size.width - 20, h);
    [UIView commitAnimations];
}

@end
