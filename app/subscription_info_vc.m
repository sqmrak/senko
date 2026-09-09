#import "app_common.h"
#import "ui_theme.h"
#import "crash_report.h"
#include "../daemon/core/b64.h"
#include <math.h>
#include <string.h>

static NSString *SenkoMetadataText(NSString *text) {
    if (![text hasPrefix:@"base64:"]) return text;
    const char *encoded = [[text substringFromIndex:7] UTF8String];
    unsigned char decoded[1024];
    size_t n = 0;
    if (!encoded || b64_decode(encoded, strlen(encoded), decoded,
                               sizeof decoded, &n) != 0 || n == 0)
        return SenkoLocalizedText(@"Not provided");
    NSString *value = [[[NSString alloc] initWithBytes:decoded length:n
                                               encoding:NSUTF8StringEncoding] autorelease];
    return [value length] ? value : SenkoLocalizedText(@"Not provided");
}

static NSString *SenkoBytes(unsigned long long value) {
    double n = (double)value;
    NSArray *units = SenkoLanguageIsRussian()
        ? [NSArray arrayWithObjects:@"Б", @"КБ", @"МБ", @"ГБ", @"ТБ", nil]
        : [NSArray arrayWithObjects:@"B", @"KB", @"MB", @"GB", @"TB", nil];
    NSUInteger unit = 0;
    while (n >= 1024.0 && unit + 1 < [units count]) { n /= 1024.0; unit++; }
    NSString *number = [NSString stringWithFormat:n >= 10.0 || unit == 0 ? @"%.0f" : @"%.1f", n];
    if (SenkoLanguageIsRussian())
        number = [number stringByReplacingOccurrencesOfString:@"." withString:@","];
    return [NSString stringWithFormat:@"%@ %@", number, [units objectAtIndex:unit]];
}

@interface SubscriptionInfoVC ()
- (void)applyChrome;
- (void)themeDidChange:(NSNotification *)n;
@end

@implementation SubscriptionInfoVC {
    SenkoSub *_sub;
}

- (id)initWithSubscription:(SenkoSub *)sub {
    if ((self = [super initWithStyle:UITableViewStyleGrouped])) _sub = [sub retain];
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_sub release];
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = SenkoLocalizedText(@"Subscription details");
    self.navigationItem.leftBarButtonItem = [[[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self
        action:@selector(donePressed)] autorelease];
    [self applyChrome];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(themeDidChange:)
                                                 name:SenkoThemeDidChangeNotification
                                               object:nil];
}

/* this screen set a background colour and nothing else, so it kept the system
   chrome: a grouped table paints its own backgroundView over whatever colour
   sits underneath, and an unstyled navigation bar stays light with a blue
   button. the dark cells against all of that is what looked broken on ipad,
   where the sheet is large enough to show most of it */
- (void)applyChrome {
    UITableView *tv = self.tableView;
    if ([tv respondsToSelector:@selector(setBackgroundView:)])
        tv.backgroundView = nil;
    tv.backgroundColor = kBG;
    tv.separatorColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.14f]
        : [UIColor colorWithWhite:1 alpha:0.16f];
    if (self.navigationController)
        StyleNavBarClassic(self.navigationController);
}

- (void)themeDidChange:(NSNotification *)n {
    (void)n;
    [self applyChrome];
    [self.tableView reloadData];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    SenkoCrashScreen("subscription details");
    [self applyChrome];
}

- (void)donePressed {
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    (void)tableView;
    return 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView;
    return section == 0 ? 6 : ([_sub->supportURL length] ? 2 : 1);
}

- (NSString *)expiryText {
    if (!_sub->expire) return SenkoLocalizedText(@"Not provided");
    NSDate *date = [NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)_sub->expire];
    if ([date timeIntervalSinceNow] <= 0.0) return SenkoLocalizedText(@"Expired");
    NSDateFormatter *fmt = [[[NSDateFormatter alloc] init] autorelease];
    fmt.locale = [[[NSLocale alloc] initWithLocaleIdentifier:
        SenkoLanguageIsRussian() ? @"ru_RU" : @"en_US"] autorelease];
    fmt.dateStyle = NSDateFormatterMediumStyle;
    fmt.timeStyle = NSDateFormatterShortStyle;
    return [fmt stringFromDate:date];
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)ip {
    if (ip.section == 0) return 54.0f;
    NSString *value = nil;
    if (ip.row == 0)
        value = [_sub->description length] ? SenkoMetadataText(_sub->description)
                                           : SenkoLocalizedText(@"Not provided");
    else
        value = _sub->supportURL;
    CGFloat width = tableView.bounds.size.width - 48.0f;
    if (width < 120.0f) width = 120.0f;
    CGSize size = SenkoTextSize(value, [UIFont systemFontOfSize:13.0f], width);
    return MAX(ip.row == 0 ? 70.0f : 58.0f, 39.0f + size.height);
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)ip {
    UITableViewCell *cell = [[[UITableViewCell alloc]
        initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil] autorelease];
    cell.backgroundColor = kCellHi;
    cell.textLabel.textColor = kInk;
    cell.detailTextLabel.textColor = kInkMuted;
    cell.textLabel.font = [UIFont boldSystemFontOfSize:14.0f];
    cell.detailTextLabel.font = [UIFont systemFontOfSize:13.0f];
    cell.textLabel.lineBreakMode = NSLineBreakByWordWrapping;
    cell.detailTextLabel.lineBreakMode = NSLineBreakByWordWrapping;
    cell.detailTextLabel.numberOfLines = 0;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    if (ip.section == 0) {
        unsigned long long used = _sub->upload + _sub->download;
        unsigned long long left = _sub->total > used ? _sub->total - used : 0;
        NSArray *names = [NSArray arrayWithObjects:@"Used", @"Remaining", @"Limit",
                          @"Uploaded", @"Downloaded", @"Expires", nil];
        NSArray *values = [NSArray arrayWithObjects:SenkoBytes(used),
            _sub->total ? SenkoBytes(left) : SenkoLocalizedText(@"Not provided"),
            _sub->total ? SenkoBytes(_sub->total) : SenkoLocalizedText(@"Not provided"),
            SenkoBytes(_sub->upload), SenkoBytes(_sub->download), [self expiryText], nil];
        cell.textLabel.text = SenkoLocalizedText([names objectAtIndex:ip.row]);
        cell.detailTextLabel.text = [values objectAtIndex:ip.row];
    } else if (ip.row == 0) {
        cell.textLabel.text = SenkoLocalizedText(@"Description");
        cell.detailTextLabel.text = [_sub->description length]
            ? SenkoMetadataText(_sub->description) : SenkoLocalizedText(@"Not provided");
    } else {
        cell.textLabel.text = SenkoLocalizedText(@"Contact support");
        cell.detailTextLabel.text = _sub->supportURL;
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        SenkoStyleSelectableCell(cell);
    }
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tableView deselectRowAtIndexPath:ip animated:YES];
    if (ip.section != 1 || ip.row != 1 || ![_sub->supportURL length]) return;
    NSURL *url = [NSURL URLWithString:_sub->supportURL];
    if (url && ([[url scheme] isEqualToString:@"https"] || [[url scheme] isEqualToString:@"http"]))
        [[UIApplication sharedApplication] openURL:url];
}

@end
