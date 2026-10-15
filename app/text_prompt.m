#import "text_prompt.h"
#import "ui_theme.h"
#import "app_common.h"

@implementation SenkoTextPromptVC {
    NSString *_message;
    NSString *_text;
    NSString *_confirm;
    UIKeyboardType _keyboard;
    void (^_handler)(NSString *text);
    UITextField *_field;
}

+ (void)presentFrom:(UIViewController *)host
              title:(NSString *)title
            message:(NSString *)message
               text:(NSString *)text
           keyboard:(UIKeyboardType)keyboard
            confirm:(NSString *)confirm
            handler:(void (^)(NSString *text))handler {
    if (!host) return;
    SenkoTextPromptVC *prompt = [[[SenkoTextPromptVC alloc] init] autorelease];
    prompt.title = title;
    prompt->_message = [message copy];
    prompt->_text = [text copy];
    prompt->_confirm = [confirm copy];
    prompt->_keyboard = keyboard;
    prompt->_handler = [handler copy];
    UINavigationController *nav = [[[UINavigationController alloc]
                                    initWithRootViewController:prompt] autorelease];
    StyleNavBarClassic(nav);
    SenkoPresentModal(host, nav);
}

- (void)dealloc {
    _field.delegate = nil;
    [_field release];
    [_handler release];
    [_confirm release];
    [_text release];
    [_message release];
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    SenkoApplyScreenChrome(self.view);
    AddVGradient(self.view, kBG, kBGBot);

    self.navigationItem.leftBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                                                       target:self
                                                       action:@selector(cancelPressed)] autorelease];
    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithTitle:_confirm
                                          style:UIBarButtonItemStyleDone
                                         target:self
                                         action:@selector(confirmPressed)] autorelease];

    CGFloat w = self.view.bounds.size.width;
    UILabel *hint = [[[UILabel alloc] initWithFrame:CGRectMake(18, 20, w - 36, 40)] autorelease];
    hint.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    hint.backgroundColor = [UIColor clearColor];
    hint.textColor = kInkMuted;
    hint.font = [UIFont systemFontOfSize:15];
    hint.numberOfLines = 2;
    hint.text = _message;
    [self.view addSubview:hint];

    _field = [[UITextField alloc] initWithFrame:CGRectMake(18, 70, w - 36, 42)];
    _field.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    _field.backgroundColor = [UIColor clearColor];
    _field.textColor = kInk;
    _field.font = [UIFont systemFontOfSize:17];
    _field.text = _text;
    _field.delegate = self;
    _field.keyboardType = _keyboard;
    _field.autocorrectionType = UITextAutocorrectionTypeNo;
    _field.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _field.clearButtonMode = UITextFieldViewModeWhileEditing;
    _field.returnKeyType = UIReturnKeyDone;
    [self.view addSubview:_field];

    UIView *line = [[[UIView alloc] initWithFrame:CGRectMake(0, 120, w, 1)] autorelease];
    line.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    line.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14]
        : [UIColor colorWithWhite:1 alpha:0.28];
    [self.view addSubview:line];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [_field becomeFirstResponder];
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    (void)field;
    [self confirmPressed];
    return NO;
}

- (void)cancelPressed {
    [self.view endEditing:YES];
    SenkoDismissModal(self, nil);
}

- (void)confirmPressed {
    /* ios 4.3 dismisses at once and releases this screen inside the call */
    [[self retain] autorelease];
    [self.view endEditing:YES];
    NSString *text = _field.text ? [[_field.text copy] autorelease] : @"";
    void (^handler)(NSString *) = [[_handler retain] autorelease];
    SenkoDismissModal(self, ^{
        if (handler) handler(text);
    });
}

@end
