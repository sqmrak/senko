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

@interface FileImportVC () <UITableViewDataSource, UITableViewDelegate>
@end

@implementation FileImportVC {

    id<FileImportDelegate> _delegate;
    NSString *_path;
    UITableView *_table;
    NSMutableArray *_rows;
    NSString *_errorText;

}

- (id)initWithPath:(NSString *)path delegate:(id<FileImportDelegate>)delegate {
    if ((self = [super init])) {
        _delegate = delegate;
        _path = [path copy];
        _rows = [[NSMutableArray alloc] init];
    }
    return self;
}

- (void)dealloc {
    [_path release];
    [_table release];
    [_rows release];
    [_errorText release];
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    if (_path) {
        self.title = [_path lastPathComponent];
        if ([self.title length] == 0) self.title = _path;
    } else if (![self.title length]) {
        self.title = @"Import file";
    }
    self.view.backgroundColor = kBG;
    AddVGradient(self.view, kBG, kBGBot);
    if (!self.navigationController || [self.navigationController.viewControllers objectAtIndex:0] == self) {
        self.navigationItem.leftBarButtonItem =
            [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                                                           target:self
                                                           action:@selector(cancelPressed)] autorelease];
    }
    _table = [[UITableView alloc] initWithFrame:self.view.bounds style:UITableViewStylePlain];
    _table.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _table.dataSource = self;
    _table.delegate = self;
    _table.backgroundColor = [UIColor clearColor];
    _table.separatorStyle = UITableViewCellSeparatorStyleNone;
    _table.separatorColor = [UIColor clearColor];
    [self.view addSubview:_table];
    [self reloadRows];
}

- (NSArray *)rootPaths {
    NSMutableArray *roots = [NSMutableArray array];
    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray *candidates = [NSArray arrayWithObjects:
        @"/var/mobile/Documents",
        @"/var/mobile/Downloads",
        @"/var/mobile/Media",
        @"/tmp",
        @"/var/mobile",
        @"/var/root",
        nil];
    for (NSString *p in candidates) {
        BOOL isDir = NO;
        if ([fm fileExistsAtPath:p isDirectory:&isDir] && isDir)
            [roots addObject:p];
    }
    return roots;
}

- (NSString *)sizeLabelForBytes:(unsigned long long)n {
    if (n < 1024ULL) return [NSString stringWithFormat:@"%llu B", n];
    if (n < 1024ULL * 1024ULL) return [NSString stringWithFormat:@"%llu KB", (n + 1023ULL) / 1024ULL];
    return [NSString stringWithFormat:@"%.1f MB", (double)n / (1024.0 * 1024.0)];
}

- (void)addPath:(NSString *)p isRoot:(BOOL)isRoot {
    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:p isDirectory:&isDir]) return;
    NSDictionary *attrs = [fm attributesOfItemAtPath:p error:nil];
    NSString *name = isRoot ? p : [p lastPathComponent];
    NSString *detail = isDir ? @"folder" : [self sizeLabelForBytes:[[attrs objectForKey:NSFileSize] unsignedLongLongValue]];
    NSMutableDictionary *row = [NSMutableDictionary dictionary];
    [row setObject:name forKey:@"name"];
    [row setObject:p forKey:@"path"];
    [row setObject:[NSNumber numberWithBool:isDir] forKey:@"dir"];
    [row setObject:detail forKey:@"detail"];
    [_rows addObject:row];
}

- (void)reloadRows {
    [_rows removeAllObjects];
    [_errorText release];
    _errorText = nil;
    NSFileManager *fm = [NSFileManager defaultManager];
    if (!_path) {
        for (NSString *p in [self rootPaths])
            [self addPath:p isRoot:YES];
    } else {
        NSError *err = nil;
        NSArray *names = [fm contentsOfDirectoryAtPath:_path error:&err];
        if (!names) {
            _errorText = [[err localizedDescription] copy];
        } else {
            NSMutableArray *dirs = [NSMutableArray array];
            NSMutableArray *files = [NSMutableArray array];
            for (NSString *name in names) {
                if ([name hasPrefix:@"."]) continue;
                NSString *p = [_path stringByAppendingPathComponent:name];
                BOOL isDir = NO;
                if (![fm fileExistsAtPath:p isDirectory:&isDir]) continue;
                if (isDir) [dirs addObject:name];
                else [files addObject:name];
            }
            SEL cmp = @selector(localizedCaseInsensitiveCompare:);
            [dirs sortUsingSelector:cmp];
            [files sortUsingSelector:cmp];
            for (NSString *name in dirs)
                [self addPath:[_path stringByAppendingPathComponent:name] isRoot:NO];
            for (NSString *name in files)
                [self addPath:[_path stringByAppendingPathComponent:name] isRoot:NO];
        }
    }
    [_table reloadData];
}

- (void)cancelPressed {
    [_delegate fileImportVCDidCancel:self];
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)section {
    (void)tv; (void)section;
    if (_errorText) return 1;
    return [_rows count] ? (NSInteger)[_rows count] : 1;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    static NSString *cid = @"file";
    UITableViewCell *cell = [tv dequeueReusableCellWithIdentifier:cid];
    if (!cell) {
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                       reuseIdentifier:cid] autorelease];
        cell.textLabel.font = [UIFont boldSystemFontOfSize:15];
        cell.detailTextLabel.font = [UIFont systemFontOfSize:11];
    }
    cell.backgroundColor = kCellHi;
    SenkoStyleInkLabel(cell.textLabel);
    SenkoStyleMutedLabel(cell.detailTextLabel);
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.selectionStyle = UITableViewCellSelectionStyleBlue;
    if (_errorText) {
        cell.textLabel.text = @"cannot open folder";
        cell.detailTextLabel.text = _errorText;
        cell.accessoryType = UITableViewCellAccessoryNone;
        return cell;
    }
    if ([_rows count] == 0) {
        cell.textLabel.text = @"empty folder";
        cell.detailTextLabel.text = _path ? _path : @"no readable folders";
        cell.accessoryType = UITableViewCellAccessoryNone;
        return cell;
    }
    NSDictionary *row = [_rows objectAtIndex:ip.row];
    BOOL isDir = [[row objectForKey:@"dir"] boolValue];
    cell.textLabel.text = [row objectForKey:@"name"];
    cell.detailTextLabel.text = isDir ? [row objectForKey:@"path"] : [row objectForKey:@"detail"];
    cell.accessoryType = isDir ? UITableViewCellAccessoryDisclosureIndicator : UITableViewCellAccessoryNone;
    return cell;
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    [tv deselectRowAtIndexPath:ip animated:YES];
    if (_errorText || [_rows count] == 0) return;
    NSDictionary *row = [_rows objectAtIndex:ip.row];
    NSString *p = [row objectForKey:@"path"];
    if ([[row objectForKey:@"dir"] boolValue]) {
        FileImportVC *next = [[[FileImportVC alloc] initWithPath:p delegate:_delegate] autorelease];
        [self.navigationController pushViewController:next animated:YES];
    } else {
        [_delegate fileImportVC:self didPickPath:p];
    }
}

@end

