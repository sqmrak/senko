#import "rules_vc.h"

#import "control_client.h"
#import "ui_theme.h"
#import "app_common.h"
#import "crash_report.h"

#include <objc/message.h>

#define RULES_ALERT_VALUE   4301
#define RULES_ALERT_MESSAGE 4302
#define RULES_SHEET_ACTION  4311
#define RULES_SHEET_TYPE    4312

/* the daemon's own vocabulary, kept in one place so a renamed button cannot
   start sending a word the rule parser does not know */
static NSString *const kRuleActions[] = { @"proxy", @"direct", @"block" };
static NSString *const kRuleTypes[] = { @"domain-suffix", @"domain-keyword", @"ip-cidr" };

static NSString *RuleActionTitle(NSString *action) {
    if ([action isEqualToString:@"direct"]) return SenkoLocalizedText(@"Direct");
    if ([action isEqualToString:@"block"]) return SenkoLocalizedText(@"Block");
    return SenkoLocalizedText(@"Through the tunnel");
}

static NSString *RuleTypeTitle(NSString *type) {
    if ([type isEqualToString:@"domain-keyword"]) return SenkoLocalizedText(@"Keyword");
    if ([type isEqualToString:@"ip-cidr"]) return SenkoLocalizedText(@"IP range");
    return SenkoLocalizedText(@"Domain and subdomains");
}

@implementation RulesVC

- (id)initWithControl:(SenkoControl *)control {
    if ((self = [super init])) {
        _ctl = [control retain];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    [_ctl release];
    [_rules release];
    [_pendingAction release];
    [_pendingType release];
    [super dealloc];
}

/* ios 5 releases the view of an offscreen controller, so the retained table
   must go with it instead of pointing into a freed hierarchy */
- (void)viewDidUnload {
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:SenkoThemeDidChangeNotification
                                                  object:nil];
    _tv.dataSource = nil;
    _tv.delegate = nil;
    [_tv release];
    _tv = nil;
    [super viewDidUnload];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"Routing rules");
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);

    self.view.backgroundColor = kBG;
    _tv = [[UITableView alloc] initWithFrame:self.view.bounds
                                       style:UITableViewStyleGrouped];
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _tv.backgroundColor = kBG;
    _tv.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.18]
        : [UIColor colorWithWhite:1 alpha:0.14];
    if ([_tv respondsToSelector:@selector(setBackgroundView:)])
        _tv.backgroundView = nil;
    [self.view addSubview:_tv];

    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAdd
                                                       target:self
                                                       action:@selector(addTapped)] autorelease];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("rules");
    [super viewWillAppear:animated];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [self reloadRules];
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    self.view.backgroundColor = kBG;
    _tv.backgroundColor = kBG;
    _tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.18]
        : [UIColor colorWithWhite:1 alpha:0.14];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

- (void)reloadRules {
    [_ctl listRules:^(NSArray *rules) {
        [_rules release];
        _rules = [rules retain];
        _loaded = YES;
        [_tv reloadData];
    }];
}

- (void)showMessage:(NSString *)body {
    UIAlertView *a = [[UIAlertView alloc] initWithTitle:SenkoLocalizedText(@"Routing rules")
                                                message:SenkoHumanReadableError(body)
                                               delegate:nil
                                      cancelButtonTitle:@"OK"
                                      otherButtonTitles:nil];
    a.tag = RULES_ALERT_MESSAGE;
    [a show];
    [a release];
}

- (void)addTapped {
    UIActionSheet *sheet = [[UIActionSheet alloc]
             initWithTitle:SenkoLocalizedText(@"What should happen to the traffic?")
                  delegate:self
         cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    destructiveButtonTitle:nil
         otherButtonTitles:RuleActionTitle(kRuleActions[0]),
                           RuleActionTitle(kRuleActions[1]),
                           RuleActionTitle(kRuleActions[2]), nil];
    sheet.tag = RULES_SHEET_ACTION;
    [sheet showInView:self.view];
    [sheet release];
}

- (void)askForType {
    UIActionSheet *sheet = [[UIActionSheet alloc]
             initWithTitle:SenkoLocalizedText(@"What should it match?")
                  delegate:self
         cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    destructiveButtonTitle:nil
         otherButtonTitles:RuleTypeTitle(kRuleTypes[0]),
                           RuleTypeTitle(kRuleTypes[1]),
                           RuleTypeTitle(kRuleTypes[2]), nil];
    sheet.tag = RULES_SHEET_TYPE;
    [sheet showInView:self.view];
    [sheet release];
}

/* a plain text alert is the only input control that behaves the same from
   ios 5 to ios 15 */
- (void)askForValue {
    NSString *hint = [_pendingType isEqualToString:@"ip-cidr"]
        ? SenkoLocalizedText(@"For example 10.0.0.0/8")
        : [_pendingType isEqualToString:@"domain-keyword"]
            ? SenkoLocalizedText(@"For example googlevideo")
            : SenkoLocalizedText(@"For example example.com");
    UIAlertView *a = [[UIAlertView alloc]
        initWithTitle:RuleActionTitle(_pendingAction)
              message:hint
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:SenkoLocalizedText(@"Add"), nil];
    a.tag = RULES_ALERT_VALUE;
    if ([a respondsToSelector:@selector(setAlertViewStyle:)]) {
        a.alertViewStyle = UIAlertViewStylePlainTextInput;
        UITextField *field = [a textFieldAtIndex:0];
        field.autocapitalizationType = UITextAutocapitalizationTypeNone;
        field.autocorrectionType = UITextAutocorrectionTypeNo;
        field.keyboardType = [_pendingType isEqualToString:@"ip-cidr"]
            ? UIKeyboardTypeNumbersAndPunctuation : UIKeyboardTypeURL;
        field.placeholder = SenkoLocalizedText(@"Value");
    }
    [a show];
    [a release];
}

- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)index {
    if (index == sheet.cancelButtonIndex) return;
    if (index < 0 || index > 2) return;
    if (sheet.tag == RULES_SHEET_ACTION) {
        [_pendingAction release];
        _pendingAction = [kRuleActions[index] copy];
        [self askForType];
        return;
    }
    if (sheet.tag == RULES_SHEET_TYPE) {
        [_pendingType release];
        _pendingType = [kRuleTypes[index] copy];
        [self askForValue];
    }
}

- (void)alertView:(UIAlertView *)alert clickedButtonAtIndex:(NSInteger)index {
    if (alert.tag != RULES_ALERT_VALUE || index == alert.cancelButtonIndex) return;
    NSString *value = [[[alert textFieldAtIndex:0] text]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![value length]) return;
/* the daemon is the one that validates: it owns the rule grammar, and a second
   copy of it here would drift from the parser that actually decides */
    [_ctl addRuleAction:_pendingAction type:_pendingType value:value
                  reply:^(NSString *reply) {
        if (![reply hasPrefix:@"OK "])
            [self showMessage:reply ? reply : SenkoLocalizedText(@"Daemon is unreachable")];
        [self reloadRules];
    }];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return 1;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)section {
    (void)tv; (void)section;
    return (NSInteger)[_rules count];
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return 58.0f;
}

- (NSString *)footerText {
    if (!_loaded) return SenkoLocalizedText(@"Reading the rules from the daemon...");
    if (![_rules count])
        return SenkoLocalizedText(@"No rules: everything goes through the tunnel. Add one with the plus button.");
    return SenkoLocalizedText(@"Block wins over direct, direct wins over the tunnel, whatever the order. On iOS 12 and later the tunnel core reads the real domain from the connection; below that the rule is matched when the name is resolved, so an address shared by several sites follows the first name that asked for it.");
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)section {
    (void)section;
    NSString *text = [self footerText];
    CGFloat width = tv.bounds.size.width - 40.0f;
    if (width < 120.0f) width = 120.0f;
    CGSize size = SenkoTextSize(text, [UIFont systemFontOfSize:12.0f], width);
    return MAX(50.0f, size.height + 24.0f);
}

- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)section {
    CGFloat height = [self tableView:tv heightForFooterInSection:section];
    UIView *wrap = [[[UIView alloc] initWithFrame:
                     CGRectMake(0, 0, tv.bounds.size.width, height)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    wrap.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    UILabel *label = [[[UILabel alloc] initWithFrame:
                       CGRectMake(20.0f, 4.0f, tv.bounds.size.width - 40.0f,
                                  height - 8.0f)] autorelease];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                             UIViewAutoresizingFlexibleHeight;
    label.backgroundColor = [UIColor clearColor];
    label.font = [UIFont systemFontOfSize:12.0f];
    label.numberOfLines = 0;
    label.lineBreakMode = NSLineBreakByWordWrapping;
    label.text = [self footerText];
    SenkoStyleMutedLabel(label);
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
    [wrap addSubview:label];
    return wrap;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"rule";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                       reuseIdentifier:cid] autorelease];
    if (ip.row >= (NSInteger)[_rules count]) return cell;
    SenkoRule *rule = [_rules objectAtIndex:ip.row];

    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.backgroundColor = kCellHi;
    cell.contentView.backgroundColor = [UIColor clearColor];
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];
    cell.textLabel.font = [UIFont systemFontOfSize:16.0f];
    cell.textLabel.textColor = kInk;
    cell.textLabel.shadowColor = nil;
/* a domain clipped in the middle is a domain nobody can read back */
    cell.textLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    cell.textLabel.adjustsFontSizeToFitWidth = YES;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    cell.textLabel.minimumFontSize = 11.0f;
#pragma clang diagnostic pop
    cell.textLabel.text = rule->value;
    cell.detailTextLabel.font = [UIFont systemFontOfSize:13.0f];
    cell.detailTextLabel.textColor = kInkMuted;
    cell.detailTextLabel.shadowColor = nil;
/* the hit counter is the only way to tell a rule that works from one that never
   matches, and it resets whenever the daemon restarts */
    cell.detailTextLabel.text = [NSString stringWithFormat:@"%@ · %@ · %@ %llu",
                                 RuleActionTitle(rule->action),
                                 RuleTypeTitle(rule->type),
                                 SenkoLocalizedText(@"hits"), rule->hits];
    return cell;
}

- (BOOL)tableView:(UITableView *)tv canEditRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return YES;
}

- (void)tableView:(UITableView *)tv
    commitEditingStyle:(UITableViewCellEditingStyle)style
     forRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv;
    if (style != UITableViewCellEditingStyleDelete) return;
    if (ip.row >= (NSInteger)[_rules count]) return;
    SenkoRule *rule = [_rules objectAtIndex:ip.row];
/* removing a rule renumbers everything after it, so the list is read back
   rather than patched in place */
    [_ctl deleteRuleIndex:rule->index reply:^(NSString *reply) {
        if (![reply hasPrefix:@"OK "])
            [self showMessage:reply ? reply : SenkoLocalizedText(@"Daemon is unreachable")];
        [self reloadRules];
    }];
}

@end
