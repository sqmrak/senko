#import "dev_table_vc.h"

#import "control_client.h"
#import "ui_theme.h"
#import "app_common.h"
#import "crash_report.h"

#include <objc/message.h>

@interface DevTableVC ()
- (void)layoutDevTable;
@end

@implementation DevTableVC

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

- (NSString *)devTitle {
    return @"";
}

- (const char *)devScreenName {
    return "devmenu";
}

- (NSString *)devFooterForSection:(NSInteger)section {
    (void)section;
    return nil;
}

- (NSString *)devHeaderForSection:(NSInteger)section {
    (void)section;
    return nil;
}

- (void)devConfirmed:(NSInteger)tag {
    (void)tag;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = [self devTitle];
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    if ([self respondsToSelector:@selector(setExtendedLayoutIncludesOpaqueBars:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self, @selector(setExtendedLayoutIncludesOpaqueBars:), NO);
    if ([self respondsToSelector:@selector(setAutomaticallyAdjustsScrollViewInsets:)])
        ((void (*)(id, SEL, BOOL))objc_msgSend)(self,
            @selector(setAutomaticallyAdjustsScrollViewInsets:), NO);

    self.view.backgroundColor = kBG;
    _tv = [[UITableView alloc] initWithFrame:self.view.bounds
                                       style:UITableViewStyleGrouped];
    _tv.dataSource = self;
    _tv.delegate = self;
    _tv.alwaysBounceVertical = YES;
    SenkoScrollViewUseManualInsets(_tv);
    _tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _tv.backgroundColor = kBG;
    /* the cell background owns the engraved groove, so UIKit must not draw a
       second separator over it */
    _tv.separatorStyle = UITableViewCellSeparatorStyleNone;
    if ([_tv respondsToSelector:@selector(setBackgroundView:)])
        _tv.backgroundView = nil;
    [self.view addSubview:_tv];
    [self layoutDevTable];

    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(devThemeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
}

- (void)layoutDevTable {
    CGRect bounds = self.view.bounds;
    _tv.frame = bounds;
    UIEdgeInsets safe = SenkoSafeAreaInsets(self.view);
    _tv.contentInset = UIEdgeInsetsMake(8.0f, 0.0f, safe.bottom + 16.0f, 0.0f);
    _tv.scrollIndicatorInsets = _tv.contentInset;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutDevTable];
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen([self devScreenName]);
    [super viewWillAppear:animated];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
}

- (void)devThemeDidChange:(NSNotification *)n {
    (void)n;
    self.view.backgroundColor = kBG;
    _tv.backgroundColor = kBG;
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [_tv reloadData];
}

/* the row count comes from the caller, not from the table: asking the table for
   it while it is building a cell re-enters a table that has not finished
   loading */
- (UITableViewCell *)devCellForTable:(UITableView *)tv
                           indexPath:(NSIndexPath *)ip
                                rows:(NSInteger)rows {
    static NSString *cid = @"dev";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                       reuseIdentifier:cid] autorelease];
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.accessoryView = nil;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.selectedBackgroundView = nil;
    cell.textLabel.font = [UIFont boldSystemFontOfSize:15.0f];
    cell.textLabel.textColor = kInk;
    cell.textLabel.shadowColor = nil;
    cell.textLabel.shadowOffset = CGSizeZero;
    cell.textLabel.numberOfLines = 1;
    cell.textLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    cell.textLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.font = [UIFont systemFontOfSize:13.0f];
    cell.detailTextLabel.textColor = kInkMuted;
    cell.detailTextLabel.shadowColor = nil;
    cell.detailTextLabel.shadowOffset = CGSizeZero;
    cell.detailTextLabel.numberOfLines = 2;
    cell.detailTextLabel.lineBreakMode = NSLineBreakByWordWrapping;
    cell.detailTextLabel.backgroundColor = [UIColor clearColor];
    cell.detailTextLabel.text = nil;

    SenkoStyleGroupCell(cell, ip, rows);
    return cell;
}

- (UIView *)devTextViewWithText:(NSString *)text
                           font:(UIFont *)font
                         height:(CGFloat)height
                          width:(CGFloat)width
                       centered:(BOOL)centered {
    if (![text length]) return nil;
    UIView *wrap = [[[UIView alloc] initWithFrame:
                     CGRectMake(0, 0, width, height)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    wrap.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    UILabel *label = [[[UILabel alloc] initWithFrame:
                       CGRectMake(20.0f, 4.0f, width - 40.0f, height - 8.0f)] autorelease];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                             UIViewAutoresizingFlexibleHeight;
    label.backgroundColor = [UIColor clearColor];
    label.font = font;
    label.numberOfLines = 0;
    label.lineBreakMode = NSLineBreakByWordWrapping;
    label.text = text;
    if (centered) {
        label.textAlignment = NSTextAlignmentCenter;
        SenkoStyleAccentLabel(label);
    } else {
        SenkoStyleMutedLabel(label);
    }
    label.shadowColor = nil;
    label.shadowOffset = CGSizeZero;
    [wrap addSubview:label];
    return wrap;
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv;
    return [[self devHeaderForSection:s] length] ? 28.0f : 12.0f;
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    return [self devTextViewWithText:[self devHeaderForSection:s]
                                font:[UIFont boldSystemFontOfSize:13.0f]
                              height:28.0f
                               width:tv.bounds.size.width
                            centered:YES];
}

- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s {
    NSString *text = [self devFooterForSection:s];
    if (![text length]) return 18.0f;
    CGFloat width = tv.bounds.size.width - 40.0f;
    if (width < 120.0f) width = 120.0f;
    CGSize size = SenkoTextSize(text, [UIFont systemFontOfSize:12.0f], width);
    return MAX(44.0f, size.height + 24.0f);
}

- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)s {
    return [self devTextViewWithText:[self devFooterForSection:s]
                                font:[UIFont systemFontOfSize:12.0f]
                              height:[self tableView:tv heightForFooterInSection:s]
                               width:tv.bounds.size.width
                            centered:YES];
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    return 58.0f;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 0;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    return [self devCellForTable:tv indexPath:ip
                            rows:[self tableView:tv numberOfRowsInSection:ip.section]];
}

- (void)devSay:(NSString *)title message:(NSString *)message {
    UIAlertView *a = [[[UIAlertView alloc]
        initWithTitle:title
              message:message
             delegate:nil
    cancelButtonTitle:@"OK"
    otherButtonTitles:nil] autorelease];
    [a show];
}

/* nothing on these screens is undone by tapping again, so each one asks before
   it runs and names what it will remove */
- (void)devConfirm:(NSString *)title message:(NSString *)message
            button:(NSString *)button tag:(NSInteger)tag {
    UIAlertView *a = [[[UIAlertView alloc]
        initWithTitle:title
              message:message
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:button, nil] autorelease];
    a.tag = tag;
    [a show];
}

- (void)alertView:(UIAlertView *)alert clickedButtonAtIndex:(NSInteger)index {
    if (index == alert.cancelButtonIndex) return;
    [self devConfirmed:alert.tag];
}

@end

@implementation DevTextVC

- (id)initWithTitle:(NSString *)title body:(NSString *)body {
    if ((self = [super init])) {
        _pageTitle = [title copy];
        _body = [body copy];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_text release];
    [_body release];
    [_pageTitle release];
    [super dealloc];
}

- (void)viewDidUnload {
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:SenkoThemeDidChangeNotification
                                                  object:nil];
    [_text release];
    _text = nil;
    [super viewDidUnload];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = _pageTitle;
    if ([self respondsToSelector:@selector(setEdgesForExtendedLayout:)])
        ((void (*)(id, SEL, NSUInteger))objc_msgSend)(self, @selector(setEdgesForExtendedLayout:), 0);
    self.view.backgroundColor = kBG;
    _text = [[UITextView alloc] initWithFrame:self.view.bounds];
    _text.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _text.editable = NO;
    _text.text = _body;
    SenkoStyleTerminalText(_text);
    [self.view addSubview:_text];

    self.navigationItem.rightBarButtonItem =
        [[[UIBarButtonItem alloc] initWithTitle:SenkoLocalizedText(@"Copy")
                                          style:UIBarButtonItemStylePlain
                                         target:self
                                         action:@selector(copyBody)] autorelease];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
}

- (void)viewWillAppear:(BOOL)animated {
    SenkoCrashScreen("devtext");
    [super viewWillAppear:animated];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    self.view.backgroundColor = kBG;
    SenkoStyleTerminalText(_text);
}

- (void)copyBody {
    [[UIPasteboard generalPasteboard] setString:_body ?: @""];
    UIAlertView *done = [[[UIAlertView alloc]
        initWithTitle:_pageTitle
              message:SenkoLocalizedText(@"Copied to the clipboard.")
             delegate:nil
    cancelButtonTitle:@"OK"
    otherButtonTitles:nil] autorelease];
    [done show];
}

@end
