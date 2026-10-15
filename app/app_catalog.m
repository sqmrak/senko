#import "app_catalog.h"

#import "ui_theme.h"

#define APP_LIMIT 1500
#define ICON_CACHE_LIMIT 300

@implementation SenkoInstalledApp
- (void)dealloc {
    [bundleID release];
    [name release];
    [bundlePath release];
    [super dealloc];
}
@end

/* where an app bundle can sit: the system volume, a rootless jailbreak's own
   applications, the per app folders of ios 7 and before, and the bundle
   containers that replaced them */
static NSArray *SenkoAppRoots(void) {
    return [NSArray arrayWithObjects:
            @"/Applications",
            @"/var/jb/Applications",
            @"/var/mobile/Applications",
            @"/var/containers/Bundle/Application",
            @"/var/mobile/Containers/Bundle/Application", nil];
}

static void SenkoCollectBundles(NSString *dir, NSMutableArray *out, BOOL oneLevelDeeper) {
    NSArray *names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:NULL];
    for (NSString *entry in names) {
        NSString *path = [dir stringByAppendingPathComponent:entry];
        if ([[entry pathExtension] isEqualToString:@"app"]) {
            [out addObject:path];
        } else if (oneLevelDeeper && [entry length] >= 32) {
            /* a container folder is named by a uuid and holds the .app */
            SenkoCollectBundles(path, out, NO);
        }
    }
}

static BOOL SenkoPlistListHas(id list, NSString *word) {
    if (![list isKindOfClass:[NSArray class]]) return NO;
    for (id item in (NSArray *)list)
        if ([item isKindOfClass:[NSString class]] && [item isEqualToString:word]) return YES;
    return NO;
}

static NSArray *gScannedApps;

@implementation SenkoAppCatalog

+ (NSArray *)cachedApps {
    @synchronized ([SenkoAppCatalog class]) {
        return [[gScannedApps retain] autorelease];
    }
}

+ (NSArray *)installedApps {
    NSMutableArray *bundles = [NSMutableArray array];
    for (NSString *root in SenkoAppRoots())
        SenkoCollectBundles(root, bundles, ![root isEqualToString:@"/Applications"] &&
                                           ![root isEqualToString:@"/var/jb/Applications"]);
    NSMutableDictionary *seen = [NSMutableDictionary dictionary];
    NSMutableArray *apps = [NSMutableArray array];
    NSString *own = [[NSBundle mainBundle] bundleIdentifier];
    for (NSString *path in bundles) {
        if ([apps count] >= APP_LIMIT) break;
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        NSDictionary *info = [NSDictionary dictionaryWithContentsOfFile:
                              [path stringByAppendingPathComponent:@"Info.plist"]];
        NSString *ident = [info objectForKey:@"CFBundleIdentifier"];
        BOOL usable = [ident isKindOfClass:[NSString class]] && [ident length] &&
                      [info objectForKey:@"CFBundleExecutable"] &&
                      ![seen objectForKey:ident] && ![ident isEqualToString:own] &&
                      !SenkoPlistListHas([info objectForKey:@"SBAppTags"], @"hidden") &&
                      ![[info objectForKey:@"LSApplicationLaunchProhibited"] boolValue];
        if (usable) {
            NSBundle *bundle = [NSBundle bundleWithPath:path];
            NSString *title = [bundle objectForInfoDictionaryKey:@"CFBundleDisplayName"];
            if (![title isKindOfClass:[NSString class]] || ![title length])
                title = [bundle objectForInfoDictionaryKey:@"CFBundleName"];
            if (![title isKindOfClass:[NSString class]] || ![title length])
                title = [[path lastPathComponent] stringByDeletingPathExtension];
            SenkoInstalledApp *app = [[[SenkoInstalledApp alloc] init] autorelease];
            app->bundleID = [ident copy];
            app->name = [title copy];
            app->bundlePath = [path copy];
            [seen setObject:app forKey:ident];
            [apps addObject:app];
        }
        [pool drain];
    }
    [apps sortUsingComparator:^NSComparisonResult(id a, id b) {
        return [((SenkoInstalledApp *)a)->name localizedCaseInsensitiveCompare:
                ((SenkoInstalledApp *)b)->name];
    }];
    @synchronized ([SenkoAppCatalog class]) {
        [gScannedApps release];
        gScannedApps = [apps copy];
    }
    return apps;
}

/* the point size in an icon file name such as AppIcon60x60 or Icon-72, 0 when it
   names none */
static int SenkoIconPoints(NSString *name) {
    int best = 0;
    NSScanner *scan = [NSScanner scannerWithString:name];
    NSCharacterSet *digits = [NSCharacterSet decimalDigitCharacterSet];
    while (![scan isAtEnd]) {
        [scan scanUpToCharactersFromSet:digits intoString:NULL];
        int n = 0;
        if ([scan scanInt:&n] && n >= 16 && n <= 1024 && best == 0) best = n;
    }
    return best;
}

static NSArray *SenkoIconNames(NSDictionary *info) {
    NSMutableArray *names = [NSMutableArray array];
    NSDictionary *icons = [info objectForKey:@"CFBundleIcons"];
    NSDictionary *primary = [icons isKindOfClass:[NSDictionary class]]
        ? [icons objectForKey:@"CFBundlePrimaryIcon"] : nil;
    id files = [primary isKindOfClass:[NSDictionary class]]
        ? [primary objectForKey:@"CFBundleIconFiles"] : nil;
    if ([files isKindOfClass:[NSArray class]]) [names addObjectsFromArray:files];
    files = [info objectForKey:@"CFBundleIconFiles"];
    if ([files isKindOfClass:[NSArray class]]) [names addObjectsFromArray:files];
    files = [info objectForKey:@"CFBundleIconFile"];
    if ([files isKindOfClass:[NSString class]]) [names addObject:files];
    [names addObjectsFromArray:[NSArray arrayWithObjects:@"AppIcon60x60", @"Icon", @"icon", nil]];
    return names;
}

/* the file for an icon name at this screen's scale, trying the suffixes an old
   bundle and a new one use */
static NSString *SenkoIconFile(NSString *dir, NSString *name) {
    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *stem = [[name pathExtension] length] ? [name stringByDeletingPathExtension] : name;
    CGFloat scale = [UIScreen mainScreen].scale;
    NSMutableArray *suffixes = [NSMutableArray array];
    if (scale >= 3.0f) [suffixes addObject:@"@3x"];
    if (scale >= 2.0f) [suffixes addObject:@"@2x"];
    [suffixes addObject:@""];
    [suffixes addObject:@"@2x"];
    for (NSString *suffix in suffixes) {
        for (NSString *device in [NSArray arrayWithObjects:@"", @"~iphone", nil]) {
            NSString *file = [dir stringByAppendingPathComponent:
                              [NSString stringWithFormat:@"%@%@%@.png", stem, suffix, device]];
            if ([fm fileExistsAtPath:file]) return file;
        }
    }
    return nil;
}

/* the smallest icon that still fills the list's tile, else the biggest there is */
static NSString *SenkoBestIconFile(SenkoInstalledApp *app, CGFloat side) {
    NSDictionary *info = [NSDictionary dictionaryWithContentsOfFile:
                          [app->bundlePath stringByAppendingPathComponent:@"Info.plist"]];
    NSMutableDictionary *byPoints = [NSMutableDictionary dictionary];
    NSString *unsized = nil;
    for (NSString *name in SenkoIconNames(info)) {
        if (![name isKindOfClass:[NSString class]]) continue;
        NSString *file = SenkoIconFile(app->bundlePath, name);
        if (!file) continue;
        int points = SenkoIconPoints(name);
        if (points) [byPoints setObject:file forKey:[NSNumber numberWithInt:points]];
        else if (!unsized) unsized = file;
    }
    NSArray *sizes = [[byPoints allKeys] sortedArrayUsingSelector:@selector(compare:)];
    for (NSNumber *size in sizes)
        if ([size intValue] >= (int)side) return [byPoints objectForKey:size];
    if ([sizes count]) return [byPoints objectForKey:[sizes lastObject]];
    return unsized;
}

static UIImage *SenkoRoundedTile(UIImage *source, NSString *letter, CGFloat side) {
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(side, side), NO, 0.0f);
    CGRect rect = CGRectMake(0, 0, side, side);
    UIBezierPath *shape = [UIBezierPath bezierPathWithRoundedRect:rect cornerRadius:side * 0.225f];
    [shape addClip];
    if (source) {
        [source drawInRect:rect];
    } else {
        [SenkoShadeColor(kAccentBlue, -0.1f) setFill];
        UIRectFill(rect);
        /* a label draws the letter, so no deprecated string drawing is needed */
        UILabel *glyph = [[[UILabel alloc] initWithFrame:rect] autorelease];
        glyph.backgroundColor = [UIColor clearColor];
        glyph.textColor = [UIColor whiteColor];
        glyph.textAlignment = NSTextAlignmentCenter;
        glyph.font = [UIFont boldSystemFontOfSize:side * 0.5f];
        glyph.text = letter;
        [glyph.layer renderInContext:UIGraphicsGetCurrentContext()];
    }
    UIImage *tile = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return tile;
}

+ (UIImage *)iconForApp:(SenkoInstalledApp *)app side:(CGFloat)side {
    static NSMutableDictionary *cache;
    if (!app) return nil;
    if (!cache) cache = [[NSMutableDictionary alloc] init];
    NSString *key = [NSString stringWithFormat:@"%@@%d", app->bundleID, (int)side];
    UIImage *hit = [cache objectForKey:key];
    if (hit) return hit;
    NSString *file = SenkoBestIconFile(app, side);
    UIImage *source = file ? [UIImage imageWithContentsOfFile:file] : nil;
    NSString *letter = [app->name length]
        ? [[app->name substringToIndex:1] uppercaseString] : @"?";
    UIImage *tile = SenkoRoundedTile(source, letter, side);
    /* a bounded cache: a device with hundreds of apps would otherwise keep every
       tile it ever scrolled past */
    if ([cache count] >= ICON_CACHE_LIMIT) [cache removeAllObjects];
    if (tile) [cache setObject:tile forKey:key];
    return tile;
}

@end
