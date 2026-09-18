#import "dev_console_vc.h"

#import "control_client.h"
#import "ui_theme.h"
#import "app_common.h"
#import "crash_report.h"

#include <objc/message.h>

/* the verbs that change nothing, offered as a starting point. anything else can
   still be typed: the daemon validates, not this screen */
static NSString *const kConsoleHints =
    @"status  list  diag  settings  rules  fwconf  logs\n"
     "set <key> <value>   check <mode> <idx>   flush <what>\n";

@implementation DevConsoleVC

- (id)initWithControl:(SenkoControl *)control {
    if ((self = [super init])) {
        _ctl = [control retain];
        _history = [[NSMutableArray alloc] init];
        _historyAt = -1;
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _input.delegate = nil;
    [_out release];
    [_input release];
    [_history release];
    [_ctl release];
    [super dealloc];
}

- (void)viewDidUnload {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _input.delegate = nil;
    [_out release];
    _out = nil;
    [_input release];
    _input = nil;
    [super viewDidUnload];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"Console");
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    self.view.backgroundColor = kBG;

    _out = [[UITextView alloc] initWithFrame:CGRectZero];
    _out.editable = NO;
    _out.text = kConsoleHints;
    _out.font = [UIFont fontWithName:@"Courier-Bold" size:13.0f]
        ?: [UIFont systemFontOfSize:13.0f];
    SEL insetSel = NSSelectorFromString(@"setTextContainerInset:");
    if ([_out respondsToSelector:insetSel])
        ((void (*)(id, SEL, UIEdgeInsets))objc_msgSend)(
            _out, insetSel, UIEdgeInsetsMake(12.0f, 10.0f, 12.0f, 10.0f));
    _out.layer.cornerRadius = 10.0f;
    _out.layer.borderWidth = 1.0f;
    _out.layer.borderColor = [UIColor colorWithWhite:1.0f alpha:0.18f].CGColor;
    _out.clipsToBounds = YES;
    SenkoStyleTerminalText(_out);
    [self.view addSubview:_out];

    _input = [[UITextField alloc] initWithFrame:CGRectZero];
    _input.delegate = self;
    _input.autocorrectionType = UITextAutocorrectionTypeNo;
    _input.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _input.spellCheckingType = UITextSpellCheckingTypeNo;
    _input.clearButtonMode = UITextFieldViewModeWhileEditing;
    _input.returnKeyType = UIReturnKeySend;
    _input.placeholder = @"status";
    _input.font = [UIFont fontWithName:@"Courier" size:14.0f]
        ?: [UIFont systemFontOfSize:14.0f];
    SenkoStyleGlassField(_input);
    [self.view addSubview:_input];

    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithTitle:SenkoLocalizedText(@"Clear")
                                          style:UIBarButtonItemStylePlain
                                         target:self
                                         action:@selector(clearOutput)] autorelease];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
/* the field sits at the bottom, so it has to climb above the keyboard or the
   line being typed is the one thing on the screen that cannot be seen */
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(keyboardChanged:)
                                                 name:UIKeyboardWillShowNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(keyboardChanged:)
                                                 name:UIKeyboardWillHideNotification
                                               object:nil];
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("devconsole");
    [super viewWillAppear:animated];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [self layoutConsoleWithKeyboard:0.0f];
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    self.view.backgroundColor = kBG;
    SenkoStyleTerminalText(_out);
    SenkoStyleGlassField(_input);
    _out.layer.borderColor = [UIColor colorWithWhite:1.0f alpha:0.18f].CGColor;
}

- (void)layoutConsoleWithKeyboard:(CGFloat)keyboard {
    CGRect b = SenkoViewBounds(self.view);
    CGFloat pad = 8.0f;
    CGFloat fieldH = 34.0f;
    CGFloat available = b.size.height - keyboard;
    if (available < fieldH + pad * 2.0f) available = fieldH + pad * 2.0f;
    _input.frame = CGRectMake(pad, available - fieldH - pad,
                              b.size.width - pad * 2.0f, fieldH);
    _out.frame = CGRectMake(pad, pad, b.size.width - pad * 2.0f,
                            available - fieldH - pad * 3.0f);
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutConsoleWithKeyboard:0.0f];
}

- (void)keyboardChanged:(NSNotification *)note {
    CGFloat keyboard = 0.0f;
    if ([note.name isEqualToString:UIKeyboardWillShowNotification]) {
        NSValue *value = [note.userInfo objectForKey:UIKeyboardFrameEndUserInfoKey];
/* the notification carries a window rectangle that ios 5 and 6 do not rotate
   with the interface, so it is converted rather than measured by its sides */
        CGRect frame = [self.view convertRect:[value CGRectValue] fromView:nil];
        CGFloat overlap = CGRectGetHeight(self.view.bounds) - CGRectGetMinY(frame);
        if (overlap > 0.0f) keyboard = overlap;
    }
    [self layoutConsoleWithKeyboard:keyboard];
}

- (void)appendLine:(NSString *)line {
    NSString *text = _out.text ?: @"";
    _out.text = [text stringByAppendingFormat:@"%@\n", line ?: @""];
    if ([_out.text length] > 0) {
        NSRange tail = NSMakeRange([_out.text length] - 1, 1);
        [_out scrollRangeToVisible:tail];
    }
}

- (void)clearOutput {
    _out.text = kConsoleHints;
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    [self send];
    return NO;
}

- (void)send {
    NSString *line = [_input.text stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![line length] || _busy) return;
    _busy = YES;
    _input.text = nil;
    [_history addObject:line];
    _historyAt = -1;
    [self appendLine:[NSString stringWithFormat:@"> %@", line]];
/* the reply is shown exactly as it came off the socket. this screen exists to
   see the protocol, so nothing here rewrites it into friendlier words */
    [_ctl sendCommand:line timeoutMs:15000 reply:^(NSString *reply) {
        _busy = NO;
        NSString *text = [reply stringByTrimmingCharactersInSet:
                          [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        [self appendLine:[text length] ? SenkoRedactSecrets(text)
                                       : SenkoLocalizedText(@"no answer")];
    }];
}

@end
