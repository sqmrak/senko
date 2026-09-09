#import "main_vc_priv.h"

/* the table hands a section header its real frame after the delegate builds it,
   and it hands it a new one on every reload and rotation. measuring the plate
   once from the table bounds left the header at the width the table had before
   the reload, which is how collapsing a subscription pushed its plate past the
   right edge. the header owns its geometry instead */
@interface SenkoSectionHeader : UIView {
@public
    UIView          *plate;
    CAGradientLayer *fill;
    UIButton        *collapse;
    UILabel         *title;
    UILabel         *meta;
    UIButton        *refresh;
    UIButton        *ping;
    UIButton        *more;
    BOOL             compact;
    CGSize           styledSize;
}
- (void)stylePlate;
@end

@implementation SenkoSectionHeader

/* the ios16 drop shadow and the ios26 glass are both cut for the plate bounds,
   so they have to be recut whenever the plate actually gets a size */
- (void)stylePlate {
    if (!plate) return;
    SenkoStyleSectionPlate(plate);
    if (SenkoThemeIsIos16() || SenkoThemeIsIos26()) {
/* keep the shadow outside the plate */
        plate.layer.masksToBounds = NO;
        plate.clipsToBounds = NO;
    }
/* cache section chrome, except under ios26 where the plate holds a live blur
   that would be re-rendered offscreen on every frame */
    plate.layer.shouldRasterize = !SenkoThemeIsIos26();
    plate.layer.rasterizationScale = [UIScreen mainScreen].scale;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;
    if (w < 1.0f || h < 1.0f || !plate) return;

    CGFloat side = compact ? 8.0f : 10.0f;
    CGFloat plateW = w - side * 2.0f;
    if (plateW < 40.0f) plateW = 40.0f;
    CGFloat plateH = h - 6.0f;
    if (plateH < 20.0f) plateH = 20.0f;
    plate.frame = CGRectMake(side, 4.0f, plateW, plateH);
    if (!CGSizeEqualToSize(styledSize, plate.bounds.size)) {
        styledSize = plate.bounds.size;
        [self stylePlate];
    }
    CGFloat cr = SenkoThemeCardRadius();
    plate.layer.cornerRadius = cr;
/* a sublayer does not follow its view, so the section fill kept the old width
   and left a bare strip along the plate after any resize */
    SenkoSetLayerFrame(fill, plate.bounds);
    fill.cornerRadius = cr;
    collapse.frame = plate.bounds;

    CGFloat actionW = compact ? 30.0f : 32.0f;
    CGFloat actionGap = compact ? 3.0f : 4.0f;
    CGFloat actionRight = (SenkoThemeIsIos16() ? cr * 0.55f : 8.0f) + (compact ? 2.0f : 0.0f);
    if (actionRight < 10.0f) actionRight = 10.0f;
    CGFloat actionH = plateH - 8.0f;
    if (actionH < 28.0f) actionH = 28.0f;
    CGFloat actionY = floorf((plateH - actionH) * 0.5f);

    CGFloat cursor = plateW - actionRight - actionW;
    CGFloat textRight = plateW - 12.0f;
    if (more) {
        CGFloat x = cursor < 0.0f ? 0.0f : cursor;
        more.frame = CGRectMake(x, actionY, actionW, actionH);
        cursor -= actionGap + actionW;
        textRight = x;
    }
    if (ping) {
        ping.frame = CGRectMake(cursor, actionY, actionW, actionH);
        cursor -= actionGap + actionW;
    }
    if (refresh) {
        refresh.frame = CGRectMake(cursor, actionY, actionW, actionH);
        textRight = cursor;
    }

    CGFloat textX = 12.0f;
    CGFloat labelWidth = textRight - textX;
    if (labelWidth < 42.0f) labelWidth = 42.0f;
    title.frame = CGRectMake(textX, 3.0f, labelWidth, 29.0f);
    meta.frame = CGRectMake(textX, 31.0f, labelWidth, 24.0f);
}

@end

static BOOL SenkoRegional(unichar c) {
    return c >= 0xDDE6 && c <= 0xDDFF;
}

static NSString *SenkoDecodedText(NSString *raw) {
    if (![raw length]) return @"";
    NSString *decoded = [raw stringByReplacingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
    return decoded ? decoded : raw;
}

static int SenkoHexDigit(unichar c) {
    if (c >= '0' && c <= '9') return (int)c - '0';
    if (c >= 'a' && c <= 'f') return (int)c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return (int)c - 'A' + 10;
    return -1;
}

static BOOL SenkoEscapedUnit(NSString *text, NSUInteger at, unichar *unit) {
    if (at + 5 >= [text length] || [text characterAtIndex:at] != '\\' ||
        [text characterAtIndex:at + 1] != 'u') return NO;
    int a = SenkoHexDigit([text characterAtIndex:at + 2]);
    int b = SenkoHexDigit([text characterAtIndex:at + 3]);
    int c = SenkoHexDigit([text characterAtIndex:at + 4]);
    int d = SenkoHexDigit([text characterAtIndex:at + 5]);
    if (a < 0 || b < 0 || c < 0 || d < 0) return NO;
    if (unit) *unit = (unichar)((a << 12) | (b << 8) | (c << 4) | d);
    return YES;
}

static NSString *SenkoExpandFlagEscapes(NSString *text) {
    if (![text length]) return @"";
    NSMutableString *out = [NSMutableString stringWithCapacity:[text length]];
    for (NSUInteger i = 0; i < [text length]; ++i) {
        unichar high = 0, low = 0;
        if (i + 11 < [text length] &&
            SenkoEscapedUnit(text, i, &high) &&
            SenkoEscapedUnit(text, i + 6, &low) &&
            high == 0xD83C && SenkoRegional(low)) {
            unichar regional[2] = { high, low };
            [out appendString:[NSString stringWithCharacters:regional length:2]];
            i += 11;
            continue;
        }
        [out appendString:[text substringWithRange:NSMakeRange(i, 1)]];
    }
    return out;
}

static NSRange SenkoFlagRange(NSString *text) {
    if (![text length]) return NSMakeRange(NSNotFound, 0);
    for (NSUInteger i = 0; i + 3 < [text length]; ++i) {
        unichar a = [text characterAtIndex:i];
        unichar b = [text characterAtIndex:i + 1];
        unichar c = [text characterAtIndex:i + 2];
        unichar d = [text characterAtIndex:i + 3];
        if (a == 0xD83C && c == 0xD83C && SenkoRegional(b) && SenkoRegional(d))
            return NSMakeRange(i, 4);
    }
    return NSMakeRange(NSNotFound, 0);
}

static NSString *SenkoFlagInText(NSString *text) {
    text = SenkoExpandFlagEscapes(SenkoDecodedText(text));
    NSRange range = SenkoFlagRange(text);
    if (range.location == NSNotFound) return nil;
    return [text substringWithRange:range];
}

static NSString *SenkoServerFlag(SenkoServer *server) {
    if (!server || ![server->remark length]) return nil;
    return SenkoFlagInText(SenkoDecodedText(server->remark));
}

static NSString *SenkoSubscriptionTitle(NSString *raw, NSString **flagOut) {
    if (flagOut) *flagOut = nil;
    if (![raw length]) return @"Subscription";
    raw = SenkoExpandFlagEscapes(SenkoDecodedText(raw));

    for (NSUInteger i = 0; i + 3 < [raw length]; ++i) {
        unichar a = [raw characterAtIndex:i];
        unichar b = [raw characterAtIndex:i + 1];
        unichar c = [raw characterAtIndex:i + 2];
        unichar d = [raw characterAtIndex:i + 3];
        if (a != 0xD83C || c != 0xD83C || !SenkoRegional(b) || !SenkoRegional(d))
            continue;
        if (flagOut)
            *flagOut = [raw substringWithRange:NSMakeRange(i, 4)];
        NSString *clean = [raw stringByReplacingCharactersInRange:NSMakeRange(i, 4)
                                                           withString:@""];
        clean = [clean stringByTrimmingCharactersInSet:
                 [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        while ([clean hasPrefix:@"..."] || [clean hasPrefix:@"•••"] ||
               [clean hasPrefix:@"…"]) {
            NSUInteger count = [clean hasPrefix:@"…"] ? 1 : 3;
            clean = [clean substringFromIndex:count];
            clean = [clean stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        }
        return [clean length] ? clean : @"Subscription";
    }
    while ([raw hasPrefix:@"..."] || [raw hasPrefix:@"•••"] ||
           [raw hasPrefix:@"…"]) {
        NSUInteger count = [raw hasPrefix:@"…"] ? 1 : 3;
        raw = [raw substringFromIndex:count];
        raw = [raw stringByTrimmingCharactersInSet:
               [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    }
    return [raw length] ? raw : @"Subscription";
}

static NSString *SenkoSubscriptionExpiry(unsigned long long expire) {
    if (!expire) return nil;
    NSDate *date = [NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)expire];
    if ([date timeIntervalSinceNow] <= 0.0)
        return SenkoLanguageIsRussian() ? @"истекла" : @"expired";
    NSDateFormatter *fmt = [[[NSDateFormatter alloc] init] autorelease];
    fmt.locale = [[[NSLocale alloc] initWithLocaleIdentifier:
        SenkoLanguageIsRussian() ? @"ru_RU" : @"en_US"] autorelease];
    fmt.dateFormat = SenkoLanguageIsRussian() ? @"'до' dd.MM.yy" : @"'until' MM/dd/yy";
    return [fmt stringFromDate:date];
}

static NSString *SenkoSubscriptionBytes(unsigned long long value) {
    double amount = (double)value;
    NSArray *units = SenkoLanguageIsRussian()
        ? [NSArray arrayWithObjects:@"Б", @"КБ", @"МБ", @"ГБ", @"ТБ", nil]
        : [NSArray arrayWithObjects:@"B", @"KB", @"MB", @"GB", @"TB", nil];
    NSUInteger unit = 0;
    while (amount >= 1024.0 && unit + 1 < [units count]) {
        amount /= 1024.0;
        unit++;
    }
    NSString *number = [NSString stringWithFormat:(amount >= 10.0 || unit == 0)
        ? @"%.0f" : @"%.1f", amount];
    if (SenkoLanguageIsRussian())
        number = [number stringByReplacingOccurrencesOfString:@"." withString:@","];
    return [NSString stringWithFormat:@"%@ %@", number, [units objectAtIndex:unit]];
}

static NSString *SenkoSubscriptionUsage(SenkoSub *sub) {
    if (!sub || (!sub->total && !sub->upload && !sub->download)) return nil;
    unsigned long long used = sub->upload + sub->download;
    if (sub->total) {
        unsigned long long left = sub->total > used ? sub->total - used : 0;
        return SenkoLanguageIsRussian()
            ? [NSString stringWithFormat:@"осталось %@", SenkoSubscriptionBytes(left)]
            : [NSString stringWithFormat:@"%@ left", SenkoSubscriptionBytes(left)];
    }
    return SenkoLanguageIsRussian()
        ? [NSString stringWithFormat:@"исп. %@", SenkoSubscriptionBytes(used)]
        : [NSString stringWithFormat:@"%@ used", SenkoSubscriptionBytes(used)];
}

static UIImage *SenkoSectionSnapshot(UIView *view) {
    if (!view || view.bounds.size.width < 1.0f || view.bounds.size.height < 1.0f)
        return nil;
    UIGraphicsBeginImageContextWithOptions(view.bounds.size, NO, 0.0f);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    [view.layer renderInContext:ctx];
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    [image retain];
    UIGraphicsEndImageContext();
    return [image autorelease];
}

BOOL SenkoServerIdentityEqual(SenkoServer *a, SenkoServer *b) {
    if (!a || !b) return NO;
    return a->group == b->group &&
           a->port == b->port &&
           [a->proto isEqualToString:b->proto] &&
           [a->net isEqualToString:b->net] &&
           [a->security isEqualToString:b->security] &&
           [a->host isEqualToString:b->host];
}

@implementation MainVC (List)

/* panel feeds put the same banner in front of every node name and a row label
   only has room for the first few words, so a whole section can read as one
   server. the shared opening is dropped for display, and names that are still
   equal afterwards are numbered in list order */
static NSString *SenkoSharedNameHead(NSArray *names) {
    if ([names count] < 2) return nil;
    NSString *first = [names objectAtIndex:0];
    NSUInteger common = [first length];
    for (NSString *name in names) {
        NSUInteger i = 0;
        while (i < common && i < [name length] &&
               [name characterAtIndex:i] == [first characterAtIndex:i]) ++i;
        common = i;
        if (common == 0) return nil;
    }
/* end the cut on a separator so a country or a number is never halved */
    NSCharacterSet *seps = [NSCharacterSet characterSetWithCharactersInString:@" |\u00b7-\u2014:/,"];
    NSUInteger cut = 0;
    for (NSUInteger i = 0; i < common; ++i) {
        if ([seps characterIsMember:[first characterAtIndex:i]]) cut = i + 1;
    }
    if (cut < 8) return nil;
    return [first substringToIndex:cut];
}

- (void)rebuildRowNames {
    NSMutableDictionary *names = [NSMutableDictionary dictionary];
    for (NSDictionary *sec in _sections) {
        NSArray *rows = [sec objectForKey:@"rows"];
        NSMutableArray *raw = [NSMutableArray array];
        for (SenkoServer *sv in rows) {
            NSString *name = SenkoServerDisplayName(sv->remark);
            [raw addObject:name ? name : @""];
        }
        NSString *head = SenkoSharedNameHead(raw);
        NSMutableArray *shown = [NSMutableArray array];
        for (NSString *name in raw) {
            NSString *out = name;
            if ([head length] && [name length] > [head length]) {
                out = [[name substringFromIndex:[head length]]
                       stringByTrimmingCharactersInSet:
                           [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                if (![out length]) out = name;
            }
            [shown addObject:out];
        }

        NSMutableDictionary *counts = [NSMutableDictionary dictionary];
        for (NSString *name in shown) {
            if (![name length]) continue;
            NSNumber *seen = [counts objectForKey:name];
            [counts setObject:[NSNumber numberWithInt:[seen intValue] + 1] forKey:name];
        }
        NSMutableDictionary *used = [NSMutableDictionary dictionary];
        NSUInteger i = 0;
        for (SenkoServer *sv in rows) {
            NSString *name = [shown objectAtIndex:i++];
            if (![name length]) continue;
            if ([[counts objectForKey:name] intValue] > 1) {
                int ordinal = [[used objectForKey:name] intValue] + 1;
                [used setObject:[NSNumber numberWithInt:ordinal] forKey:name];
                name = [NSString stringWithFormat:@"%@ (%d)", name, ordinal];
            }
            [names setObject:name forKey:[NSNumber numberWithInt:sv->index]];
        }
    }
    [_rowName release];
    _rowName = [names retain];
}

- (void)applyCatalog:(NSArray *)servers subs:(NSArray *)subs order:(NSArray *)order {
/* keep chrome geometry after reload */
    SenkoServer *oldSelected = nil;
    if (_selectedBackend == SenkoBackendServer && _selectedSrvIdx >= 0) {
        for (SenkoServer *sv in _servers) {
            if (sv->index == _selectedSrvIdx) {
                oldSelected = [sv retain];
                break;
            }
        }
    }
    [_servers release];
    _servers = [servers mutableCopy];
    [_subs release];
    _subs = [subs mutableCopy];
    [_sectionOrder release];
    _sectionOrder = [order mutableCopy];
    if (!_sectionOrder || ![_sectionOrder count]) {
        [_sectionOrder release];
        _sectionOrder = [[NSMutableArray alloc] init];
        [_sectionOrder addObject:[NSNumber numberWithInt:-1]];
        for (SenkoSub *sub in _subs)
            [_sectionOrder addObject:[NSNumber numberWithInt:sub->index]];
    }
    _checkGeneration++;
    [_pingingSubs removeAllObjects];
    [_serverStatus removeAllObjects];
    [self rebuildSections];
    [self reconcileSelectionAfterListKeeping:oldSelected];
    [oldSelected release];
    [_table reloadData];
    [self styleListWell];
    [self layoutMainChrome];
}

/* servers without a reading sort last: an unmeasured row is not fast, it is
   unknown, and burying it keeps the top of the list meaningful */
static int SenkoSortRank(NSNumber *ms) {
    if (!ms) return 1;
    int v = [ms intValue];
    return v >= 0 ? 0 : 2;
}

- (NSArray *)sortedRows:(NSArray *)rows {
    SenkoServerSort mode = (SenkoServerSort)
        [[NSUserDefaults standardUserDefaults] integerForKey:SENKO_SERVER_SORT_KEY];
    if (mode != SenkoSortName && mode != SenkoSortPing) return rows;
    NSMutableArray *out = [NSMutableArray arrayWithArray:rows];
    NSDictionary *status = _serverStatus;
    [out sortUsingComparator:^NSComparisonResult(SenkoServer *a, SenkoServer *b) {
        NSString *na = SenkoServerDisplayName(a->remark);
        NSString *nb = SenkoServerDisplayName(b->remark);
        if (![na length]) na = a->host ? a->host : @"";
        if (![nb length]) nb = b->host ? b->host : @"";
        if (mode == SenkoSortPing) {
            NSNumber *pa = [status objectForKey:[NSNumber numberWithInt:a->index]];
            NSNumber *pb = [status objectForKey:[NSNumber numberWithInt:b->index]];
            int ra = SenkoSortRank(pa), rb = SenkoSortRank(pb);
            if (ra != rb) return ra < rb ? NSOrderedAscending : NSOrderedDescending;
            if (ra == 0 && [pa intValue] != [pb intValue])
                return [pa intValue] < [pb intValue]
                    ? NSOrderedAscending : NSOrderedDescending;
        }
        return [na compare:nb options:NSCaseInsensitiveSearch];
    }];
    return out;
}

- (void)rebuildSections {
    [_revealedRows removeAllObjects];
    NSMutableArray *secs = [NSMutableArray array];
    NSMutableArray *manual = [NSMutableArray array];
    NSMutableDictionary *bySub = [NSMutableDictionary dictionary];
    NSMutableDictionary *flagBySub = [NSMutableDictionary dictionary];

    for (SenkoServer *sv in _servers) {
        if (sv->group >= 0) {
            NSNumber *key = [NSNumber numberWithInt:sv->group];
            if (![flagBySub objectForKey:key]) {
                NSString *flag = SenkoServerFlag(sv);
                if ([flag length]) [flagBySub setObject:flag forKey:key];
            }
        }
        if (sv->group < 0) {
            [manual addObject:sv];
            continue;
        }
        NSNumber *k = [NSNumber numberWithInt:sv->group];
        NSMutableArray *arr = [bySub objectForKey:k];
        if (!arr) {
            arr = [NSMutableArray array];
            [bySub setObject:arr forKey:k];
        }
        [arr addObject:sv];
    }

    NSMutableDictionary *subByIndex = [NSMutableDictionary dictionary];
    for (SenkoSub *sub in _subs)
        [subByIndex setObject:sub forKey:[NSNumber numberWithInt:sub->index]];

    NSMutableArray *ordered = [NSMutableArray arrayWithArray:_sectionOrder];
    NSMutableSet *seen = [NSMutableSet set];
    for (NSNumber *key in ordered) {
        int subIdx = [key intValue];
        if (subIdx == -1) {
            if ([manual count] == 0 && ![self hasAWGProfile]) continue;
            [secs addObject:[NSDictionary dictionaryWithObjectsAndKeys:
                             @"Manual", @"title",
                             key, @"subIdx",
                             [self sortedRows:manual], @"rows", nil]];
            [seen addObject:key];
            continue;
        }
        SenkoSub *sub = [subByIndex objectForKey:key];
        if (!sub) continue;
        NSArray *rows = [bySub objectForKey:key];
        if (!rows) rows = [NSArray array];
        NSString *rawTitle = [sub->name length] ? sub->name : @"Subscription";
        rawTitle = [rawTitle stringByReplacingOccurrencesOfString:@"_" withString:@" "];
        NSString *flag = nil;
        NSString *title = SenkoSubscriptionTitle(rawTitle, &flag);
        if (![flag length]) flag = [flagBySub objectForKey:key];
        NSString *expiry = SenkoSubscriptionExpiry(sub->expire);
        NSString *usage = SenkoSubscriptionUsage(sub);
        NSMutableArray *parts = [NSMutableArray array];
        if (usage) [parts addObject:usage];
        if (expiry) [parts addObject:expiry];
        NSString *meta = [parts componentsJoinedByString:@"  •  "];
        [secs addObject:[NSDictionary dictionaryWithObjectsAndKeys:
                         title, @"title", flag ? flag : @"", @"flag",
                         meta, @"meta", key, @"subIdx",
                         [self sortedRows:rows], @"rows", nil]];
        [seen addObject:key];
    }

    if ([manual count] > 0 || [self hasAWGProfile]) {
        NSNumber *manualKey = [NSNumber numberWithInt:-1];
        if (![seen containsObject:manualKey])
            [secs insertObject:[NSDictionary dictionaryWithObjectsAndKeys:
                               @"Manual", @"title", manualKey, @"subIdx",
                               [self sortedRows:manual], @"rows", nil] atIndex:0];
    }
    for (SenkoSub *sub in _subs) {
        NSNumber *key = [NSNumber numberWithInt:sub->index];
        if ([seen containsObject:key]) continue;
        NSArray *rows = [bySub objectForKey:key];
        if (!rows) rows = [NSArray array];
        NSString *rawTitle = [sub->name length] ? sub->name : @"Subscription";
        rawTitle = [rawTitle stringByReplacingOccurrencesOfString:@"_" withString:@" "];
        NSString *flag = nil;
        NSString *title = SenkoSubscriptionTitle(rawTitle, &flag);
        if (![flag length]) flag = [flagBySub objectForKey:key];
        NSString *expiry = SenkoSubscriptionExpiry(sub->expire);
        NSString *usage = SenkoSubscriptionUsage(sub);
        NSMutableArray *parts = [NSMutableArray array];
        if (usage) [parts addObject:usage];
        if (expiry) [parts addObject:expiry];
        NSString *meta = [parts componentsJoinedByString:@"  •  "];
        [secs addObject:[NSDictionary dictionaryWithObjectsAndKeys:
                         title, @"title", flag ? flag : @"", @"flag", meta, @"meta",
                         key, @"subIdx", [self sortedRows:rows], @"rows", nil]];
        [_sectionOrder addObject:key];
    }

    [_sections release];
    _sections = [secs retain];
    [self rebuildRowNames];

    NSMutableSet *valid = [NSMutableSet set];
    for (NSDictionary *sec in _sections) {
        int subIdx = [[sec objectForKey:@"subIdx"] intValue];
        if (subIdx >= 0) [valid addObject:[NSNumber numberWithInt:subIdx]];
    }
    [_collapsedSubs intersectSet:valid];
    [self syncEmptyState];
}

/* the list is empty when nothing at all is configured: not a single manual
   server, no subscription server, and no saved amneziawg profile */
- (void)syncEmptyState {
    if (!_emptyState) return;
    BOOL empty = ([_servers count] == 0) && ![self hasAWGProfile];
    _emptyState.hidden = !empty;
    if (!empty) return;

    _emptyState.frame = _table.frame;
    [self.view bringSubviewToFront:_emptyState];
    [self bringMainChromeToFront];
    [_emptyState setHWID:_deviceHWID];
    if ([_deviceHWID length]) return;
/* the daemon owns the id it actually sends, so the ui asks for it instead of
   inventing a second one */
    [_ctl deviceHWID:^(NSString *hwid) {
        if (![hwid length]) return;
        [_deviceHWID release];
        _deviceHWID = [hwid copy];
        [_emptyState setHWID:_deviceHWID];
    }];
}

- (void)emptyStatePastePressed {
    [self pasteFromClipboard];
}

- (void)emptyStateScanPressed {
    [self openScanner];
}

- (void)emptyStateCopyHWID {
    if (![_deviceHWID length]) return;
    [[UIPasteboard generalPasteboard] setString:_deviceHWID];
    [_emptyState flashCopiedNotice];
}

- (void)setListHeaderProgress:(CGFloat)progress {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
/* a whole layout pass for a sub pixel change is not worth running */
    if (fabsf((float)(_listHeaderProgress - progress)) < 0.004f) return;
    _listHeaderProgress = progress;
    [self layoutMainChromeGeometry];
}

- (SenkoServer *)serverAtIndexPath:(NSIndexPath *)ip {
    if ([self isAWGRowAtIndexPath:ip]) return nil;
    if (!_sections || ip.section < 0 || ip.section >= (NSInteger)[_sections count])
        return nil;
    NSArray *rows = [[_sections objectAtIndex:ip.section] objectForKey:@"rows"];
    NSInteger row = ip.row - [self awgRowOffsetInSection:ip.section];
    if (row < 0 || row >= (NSInteger)[rows count]) return nil;
    return [rows objectAtIndex:row];
}

- (void)refresh {
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(refresh)
                                               object:nil];
    NSInteger generation = ++_catalogGeneration;
    [_ctl listCatalog:^(NSArray *servers, NSArray *subs, NSArray *order) {
        if (generation != _catalogGeneration) return;
/* restart the daemon after a missing reply */
        if (!servers) {
            [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
                if (generation != _catalogGeneration) return;
                if (!up) {
                    if ([self isTunnelActive]) {
/* keep the current strip while connected */
                        return;
                    }
                    [self setLastErr:detail ? detail : @"daemon offline"];
                    [_state release];
                    _state = [@"error" copy];
                    [self applyState];
                    return;
                }
                [_ctl listCatalog:^(NSArray *servers2, NSArray *subs2, NSArray *order2) {
                    if (generation != _catalogGeneration) return;
                    if (!servers2) {
                        if (![self isTunnelActive]) {
                            [self setLastErr:@"daemon offline"];
                            [self applyState];
                        }
                        return;
                    }
                    [self applyCatalog:servers2 subs:subs2 order:order2];
                    [self startStatusChecks];
                }];
            }];
            return;
        }
        [self applyCatalog:servers subs:subs order:order];
        [self startStatusChecks];
    }];
    __block NSString *vlessState = nil;
    __block NSString *awgState = nil;
    __block NSInteger pending = 2;
    void (^applyBackendState)(void) = ^{
        if (generation != _catalogGeneration) {
            [vlessState release];
            [awgState release];
            vlessState = nil;
            awgState = nil;
            return;
        }
        pending--;
        if (pending != 0) return;

        NSString *awg = [awgState stringByTrimmingCharactersInSet:
                         [NSCharacterSet whitespaceAndNewlineCharacterSet]];
/* ignore a dead awg profile */
        BOOL awgUp = [awg isEqualToString:@"connecting"] ||
                     [awg isEqualToString:@"connected"];
        BOOL awgErr = [awg length] > 0 && [awg hasPrefix:@"error"];
        BOOL vlessUp = [vlessState isEqualToString:@"connecting"] ||
                       [vlessState isEqualToString:@"connected"];
        BOOL vlessErr = [vlessState isEqualToString:@"error"];
/* the daemon answers STATUS within two seconds or not at all, and a slow
   device under a live tunnel misses that window. a missing answer is not a
   disconnect: reporting idle here dropped a running tunnel back to the grey
   idle card until the next refresh happened to succeed */
        BOOL vlessAnswered = [vlessState length] > 0;
        BOOL awgAnswered = [awg length] > 0;
        if (!vlessAnswered && !awgAnswered) {
            [vlessState release];
            [awgState release];
            vlessState = nil;
            awgState = nil;
            /* nothing was learned, so the poll that drives a connecting card
               has to be kept alive by hand */
            if ([self isTunnelActive]) {
                [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                         selector:@selector(refresh)
                                                           object:nil];
                [self performSelector:@selector(refresh) withObject:nil afterDelay:2.0];
            }
            return;
        }

        if (awgUp) {
            _activeBackend = SenkoBackendAmneziaWG;
            _selectedBackend = SenkoBackendAmneziaWG;
            [[NSUserDefaults standardUserDefaults] setInteger:_selectedBackend
                                                       forKey:SENKO_SELECTED_BACKEND_KEY];
            [_state release];
            _state = [awg copy];
            [self setLastErr:nil];
        } else if (vlessUp) {
            _activeBackend = SenkoBackendServer;
            [_state release];
            _state = [vlessState copy];
            [self setLastErr:nil];
        } else if (awgErr && _selectedBackend == SenkoBackendAmneziaWG) {
            _activeBackend = SenkoBackendNone;
            [_state release];
            _state = [@"idle" copy];
            [self setLastErr:awg];
        } else if (vlessErr) {
            _activeBackend = SenkoBackendNone;
            [_state release];
            _state = [@"idle" copy];
            if (!_lastErr || ![_lastErr length])
                [self setLastErr:@"connection failed"];
        } else if (!vlessAnswered && [self isTunnelActive]) {
/* the server backend stayed silent while the card still shows a live tunnel,
   so the old state is the only trustworthy one */
        } else {
            _activeBackend = SenkoBackendNone;
            [_state release];
/* use idle for an unknown state */
            NSString *st = vlessState;
            if (!st || [st isEqualToString:@"error"] || [st isEqualToString:@"unknown"])
                st = @"idle";
            _state = [st copy];
            if (awgErr && (!_lastErr || ![_lastErr length]))
                [self setLastErr:awg];
            else if (!awgErr)
                [self setLastErr:nil];
        }
        [self applyState];
        if (awgUp && [awg isEqualToString:@"connecting"]) {
            [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                     selector:@selector(refresh)
                                                       object:nil];
            [self performSelector:@selector(refresh) withObject:nil afterDelay:2.0];
        }
        [vlessState release];
        [awgState release];
        vlessState = nil;
        awgState = nil;
    };
    [_ctl statusStateWithUptime:^(NSString *state, long uptime) {
        if (generation != _catalogGeneration) return;
        /* the daemon owns the clock, so the elapsed time survives the app being
           closed and reopened over a live tunnel. a dropped reply carries no
           clock at all and must not reset the one already on screen */
        if ([state length]) {
            _tunnelUptime = uptime;
            _tunnelUptimeAt = uptime > 0 ? CACurrentMediaTime() : 0.0;
        }
        vlessState = [state copy];
        applyBackendState();
    }];
    [_ctl awgStatus:^(NSString *status) {
        if (generation != _catalogGeneration) return;
        awgState = [status copy];
        applyBackendState();
    }];
}

- (void)setToggleBusy:(BOOL)busy {
    _busy = busy;
    _connectBtn.enabled = !busy;
    [self applyServerListLock];
}

- (void)syncSelectionFromDaemon {
    _selectedSrvIdx = -1;
    for (SenkoServer *sv in _servers) {
        if (sv->selected) {
            _selectedSrvIdx = sv->index;
            return;
        }
    }
}

- (void)reconcileSelectionAfterListKeeping:(SenkoServer *)anchor {
    if (_selectedBackend == SenkoBackendAmneziaWG) return;
    if (anchor) {
        for (SenkoServer *sv in _servers) {
            if (SenkoServerIdentityEqual(sv, anchor)) {
                _selectedSrvIdx = sv->index;
                return;
            }
        }
    }
    if (_selectedSrvIdx < 0 || [self isServerSelectionLocked]) {
        [self syncSelectionFromDaemon];
        return;
    }
    for (SenkoServer *sv in _servers) {
        if (sv->index == _selectedSrvIdx) return;
    }
    [self syncSelectionFromDaemon];
}

- (BOOL)isTunnelActive {
    return [_state isEqualToString:@"connected"] || [_state isEqualToString:@"connecting"];
}

- (BOOL)isServerSelectionLocked {
    return _busy;
}

- (BOOL)isListMutationLocked {
    return _busy || _subscriptionMutationBusy || [self isTunnelActive];
}

- (void)applyServerListLock {
    BOOL locked = _busy || _subscriptionMutationBusy;
    _table.allowsSelection = !locked;
    _table.userInteractionEnabled = !_subscriptionMutationBusy;
    _table.alpha = locked ? 0.72f : 1.0f;
}

- (void)setLastErr:(NSString *)msg {
    NSString *shown = msg ? SenkoHumanReadableError(msg) : nil;
    [_lastErr release];
    _lastErr = shown ? [shown copy] : nil;
    if (!shown || ![shown length]) {
        [_lastAlertErr release];
        _lastAlertErr = nil;
        return;
    }
    if (_lastAlertErr && [_lastAlertErr isEqualToString:shown]) return;
    [_lastAlertErr release];
    _lastAlertErr = [shown copy];
    UIAlertView *alert = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Connection failed")
              message:shown delegate:nil cancelButtonTitle:SenkoLocalizedText(@"OK")
        otherButtonTitles:nil] autorelease];
    [alert show];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return (NSInteger)[_sections count];
}

- (void)scrollViewDidScroll:(UIScrollView *)scrollView {
    if (scrollView != _table) return;
    /* the collapse is tied to the spacer above the first row, so the card
       finishes shrinking exactly as that spacer leaves the screen and the two
       never disagree about how far the list has travelled */
    CGFloat travel = _table.tableHeaderView
        ? _table.tableHeaderView.bounds.size.height : 72.0f;
    if (travel < 24.0f) travel = 24.0f;
    CGFloat offset = scrollView.contentOffset.y;
    CGFloat progress = offset <= 0.0f ? 0.0f : offset / travel;
    if (progress > 1.0f) progress = 1.0f;
    [self setListHeaderProgress:progress];
}





- (void)sectionToggleTapped:(UIButton *)button {
    int subIdx = (int)button.tag - 4000;
    if (subIdx < 0) return;
    NSNumber *key = [NSNumber numberWithInt:subIdx];
    if ([_collapsedSubs containsObject:key])
        [_collapsedSubs removeObject:key];
    else
        [_collapsedSubs addObject:key];

/* a section reload runs the row and header change inside an update block, and
   the header the table keeps from the outgoing pass stayed on screen at its old
   frame next to the new one. the collapse only changes a row count, so the
   whole table is rebuilt in place instead */
    [_table reloadData];
}

- (void)moveSectionAtIndex:(NSInteger)from toIndex:(NSInteger)to {
    if (from < 0 || to < 0 || from >= (NSInteger)[_sections count] ||
        to >= (NSInteger)[_sections count] || from == to || _sectionDragSending)
        return;
    int sectionId = [[[_sections objectAtIndex:from] objectForKey:@"subIdx"] intValue];
    int targetId = [[[_sections objectAtIndex:to] objectForKey:@"subIdx"] intValue];
    NSNumber *source = [NSNumber numberWithInt:sectionId];
    NSNumber *target = [NSNumber numberWithInt:targetId];
    NSUInteger targetPos = [_sectionOrder indexOfObject:target];
    if (targetPos == NSNotFound) return;
    [_sectionOrder removeObject:source];
    targetPos = [_sectionOrder indexOfObject:target];
    if (from < to) targetPos++;
    if (targetPos > [_sectionOrder count]) targetPos = [_sectionOrder count];
    [_sectionOrder insertObject:source atIndex:targetPos];
    [self rebuildSections];
    [_table reloadData];

    _sectionDragSending = YES;
    [_ctl moveSection:sectionId toPosition:(int)targetPos reply:^(NSString *reply) {
        _sectionDragSending = NO;
        if (!reply || [reply hasPrefix:@"ERR"]) {
            [self refresh];
            return;
        }
    SetStatusRefresh(_statusLabel, @"section moved");
    }];
}

- (void)sectionLongPressed:(UILongPressGestureRecognizer *)gesture {
    if (!_sectionDragActive && [self isListMutationLocked]) return;

    if (gesture.state == UIGestureRecognizerStateBegan) {
        if (_sectionDragActive || _sectionDragSending) return;
        NSInteger from = gesture.view.tag - 7000;
        if (from < 0 || from >= (NSInteger)[_sections count]) return;

        UIImage *image = SenkoSectionSnapshot(gesture.view);
        if (!image) return;
        CGRect frame = [_table convertRect:gesture.view.bounds fromView:gesture.view];
        UIImageView *snapshot = [[[UIImageView alloc] initWithImage:image] autorelease];
        snapshot.frame = frame;
        snapshot.alpha = 0.92f;
        snapshot.layer.cornerRadius = SenkoThemeCardRadius();
        snapshot.layer.masksToBounds = YES;
        [_table addSubview:snapshot];
        _sectionDragSnapshot = [snapshot retain];
        _sectionDragHeader = gesture.view;
        _sectionDragGrabOffset = [gesture locationInView:gesture.view].y;
        _sectionDragOrigin = (int)from;
        _dragSection = (int)from;
        _sectionDragActive = YES;
        _sectionDragHeader.alpha = 0.18f;
        _table.scrollEnabled = NO;
        return;
    }

    if (!_sectionDragActive) return;

    if (gesture.state == UIGestureRecognizerStateChanged) {
        CGPoint point = [gesture locationInView:_table];
        CGRect frame = _sectionDragSnapshot.frame;
        frame.origin.y = point.y - _sectionDragGrabOffset;
        _sectionDragSnapshot.frame = frame;

        CGFloat centerY = CGRectGetMidY(frame);
        NSInteger to = 0;
        for (NSInteger i = 0; i < (NSInteger)[_sections count]; ++i) {
            CGRect header = [_table rectForHeaderInSection:i];
            if (centerY < CGRectGetMidY(header)) {
                to = i;
                break;
            }
            to = i;
        }
        _dragSection = (int)to;
        return;
    }

    if (gesture.state != UIGestureRecognizerStateEnded &&
        gesture.state != UIGestureRecognizerStateCancelled &&
        gesture.state != UIGestureRecognizerStateFailed)
        return;

    NSInteger finalSection = _dragSection;
    NSInteger sourceSection = _sectionDragOrigin;
    BOOL moved = finalSection != sourceSection;
    UIView *sourceHeader = [_sectionDragHeader retain];
    _table.scrollEnabled = YES;

    UIView *snapshot = [_sectionDragSnapshot retain];
    [_sectionDragSnapshot release];
    _sectionDragSnapshot = nil;
    CGRect target = [_table rectForHeaderInSection:finalSection];
    [UIView animateWithDuration:0.18
                          delay:0.0
                        options:UIViewAnimationOptionCurveEaseOut
                     animations:^{
                         snapshot.frame = target;
                         snapshot.alpha = 0.0f;
                     }
                     completion:^(BOOL finished) {
                         (void)finished;
                         [snapshot removeFromSuperview];
                         [snapshot release];
                         _sectionDragActive = NO;
                         _sectionDragHeader = nil;
                         if (moved) {
                             [self moveSectionAtIndex:sourceSection toIndex:finalSection];
                         } else {
                             sourceHeader.alpha = 1.0f;
                         }
                         [sourceHeader release];
                     }];
}

- (void)rowLongPressed:(UILongPressGestureRecognizer *)gesture {
    if (gesture.state != UIGestureRecognizerStateBegan || [self isListMutationLocked]) return;
    NSIndexPath *ip = [_table indexPathForRowAtPoint:
                       [gesture locationInView:_table]];
    if (!ip || [self isAWGRowAtIndexPath:ip] || ![self isManualSection:ip.section]) return;
    [_table setEditing:YES animated:YES];
}

- (NSString *)awgProfilePath {
    NSString *path = [[NSUserDefaults standardUserDefaults] stringForKey:SENKO_AWG_PROFILE_KEY];
    if (![path length]) path = SENKO_AWG_PROFILE_PATH;
    return path;
}

- (BOOL)hasAWGProfile {
    return [[NSFileManager defaultManager] fileExistsAtPath:[self awgProfilePath]];
}

- (BOOL)isManualSection:(NSInteger)section {
    if (!_sections || section < 0 || section >= (NSInteger)[_sections count]) return NO;
    return [[[_sections objectAtIndex:section] objectForKey:@"subIdx"] intValue] == -1;
}

- (NSInteger)awgRowOffsetInSection:(NSInteger)section {
    return ([self hasAWGProfile] && [self isManualSection:section]) ? 1 : 0;
}

- (BOOL)isAWGRowAtIndexPath:(NSIndexPath *)ip {
    return [self awgRowOffsetInSection:ip.section] > 0 && ip.row == 0;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    if (!_sections || s < 0 || s >= (NSInteger)[_sections count]) return 0;
    NSDictionary *sec = [_sections objectAtIndex:s];
    int subIdx = [[sec objectForKey:@"subIdx"] intValue];
    if (subIdx >= 0 && [_collapsedSubs containsObject:[NSNumber numberWithInt:subIdx]])
        return 0;
    return [self awgRowOffsetInSection:s] + (NSInteger)[[sec objectForKey:@"rows"] count];
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad)
        return 82.0f;
    return 76.0f;
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad)
        return 68.0f;
    return 64.0f;
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    if (!_sections || s < 0 || s >= (NSInteger)[_sections count]) return nil;
    BOOL pad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
    CGFloat scrH = [UIScreen mainScreen].bounds.size.height;
    CGFloat scrW = [UIScreen mainScreen].bounds.size.width;
    if (scrW > scrH) { CGFloat t = scrH; scrH = scrW; scrW = t; } /* use the short side */
    BOOL compact = (!pad && scrH <= 568.0f);
    CGFloat hh = pad ? 68.0f : 64.0f;
    CGFloat w = CGRectGetWidth(tv.bounds);
    if (w < 1.0f) w = CGRectGetWidth(tv.frame);
    if (w < 160.0f) w = 160.0f;
    CGFloat cr = SenkoThemeCardRadius();
    CGFloat iconPx = compact ? 18.0f : 20.0f;

    NSDictionary *sec = [_sections objectAtIndex:s];
    int subIdx = [[sec objectForKey:@"subIdx"] intValue];
    NSString *title = [sec objectForKey:@"title"];
    NSString *metaText = [sec objectForKey:@"meta"];
    NSUInteger n = [[sec objectForKey:@"rows"] count];
    BOOL manualHasAwg = (subIdx < 0 && [self hasAWGProfile]);
    NSUInteger shown = n + (manualHasAwg ? 1 : 0);
    BOOL collapsed = subIdx >= 0 &&
        [_collapsedSubs containsObject:[NSNumber numberWithInt:subIdx]];
    if (subIdx >= 0)
        title = [NSString stringWithFormat:@"%@  %@", collapsed ? @">" : @"v", title];
    if (!metaText)
        metaText = [NSString stringWithFormat:@"%lu single config%@", (unsigned long)shown,
                    shown == 1 ? @"" : @"s"];

    SenkoSectionHeader *wrap = [[[SenkoSectionHeader alloc]
                                 initWithFrame:CGRectMake(0, 0, w, hh)] autorelease];
    wrap->compact = compact;
    wrap.backgroundColor = [UIColor clearColor];
    wrap.clipsToBounds = YES;
    wrap.userInteractionEnabled = YES;
    wrap.tag = 7000 + s;
    UILongPressGestureRecognizer *drag = [[[UILongPressGestureRecognizer alloc]
                                           initWithTarget:self action:@selector(sectionLongPressed:)] autorelease];
    drag.minimumPressDuration = 0.45;
    [wrap addGestureRecognizer:drag];

    UIView *plate = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    plate.layer.cornerRadius = cr;
    plate.layer.borderWidth = 0;
    plate.layer.borderColor = [UIColor clearColor].CGColor;
    plate.clipsToBounds = YES;
    CAGradientLayer *g = [CAGradientLayer layer];
    g.cornerRadius = cr;
    g.masksToBounds = YES;
    SenkoFillSectionGradient(g);
    [plate.layer insertSublayer:g atIndex:0];
    [wrap addSubview:plate];
    wrap->plate = plate;
    wrap->fill = g;

    if (subIdx >= 0) {
        UIButton *collapse = [UIButton buttonWithType:UIButtonTypeCustom];
        collapse.tag = 4000 + subIdx;
        [collapse addTarget:self action:@selector(sectionToggleTapped:)
           forControlEvents:UIControlEventTouchUpInside];
        [plate addSubview:collapse];
        wrap->collapse = collapse;
    }

    UILabel *lab = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    lab.backgroundColor = [UIColor clearColor];
    lab.font = SenkoThemeIsIos16() ? SenkoFontBody(14, YES) : [UIFont boldSystemFontOfSize:14];
    SenkoStyleSectionTitle(lab);
    lab.text = title;
    lab.numberOfLines = 2;
    lab.lineBreakMode = NSLineBreakByWordWrapping;
    [plate addSubview:lab];
    wrap->title = lab;

    UILabel *meta = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    meta.backgroundColor = [UIColor clearColor];
    meta.font = SenkoThemeIsIos16() ? SenkoFontBody(10, NO) : [UIFont systemFontOfSize:10];
    SenkoStyleSectionMeta(meta);
    meta.text = metaText;
    meta.numberOfLines = 2;
    meta.lineBreakMode = NSLineBreakByWordWrapping;
    [plate addSubview:meta];
    wrap->meta = meta;

    /* refresh and ping belong to a subscription or to the amneziawg profile */
    BOOL showActions = (subIdx >= 0) || manualHasAwg;
    /* the manual group keeps its own menu for the delete-all action */
    BOOL showMore = showActions || (subIdx < 0 && n > 0);
    BOOL subPingBusy = subIdx >= 0 &&
        [_pingingSubs containsObject:[NSNumber numberWithInt:subIdx]];

    if (showMore) {
        UIColor *iconTint = (SenkoThemeIsBoykisser() || SenkoThemeIsMiside())
            ? [UIColor colorWithRed:1.00 green:0.42 blue:0.72 alpha:1.0]
            : kAccentBlue;
        if (showActions) {
            UIButton *ref = [UIButton buttonWithType:UIButtonTypeCustom];
            ref.contentMode = UIViewContentModeCenter;
            ref.imageView.contentMode = UIViewContentModeScaleAspectFit;
            [ref setImage:SenkoIconRefresh(iconPx, iconTint)
                 forState:UIControlStateNormal];
            if (manualHasAwg) {
                [ref addTarget:self action:@selector(awgRefreshTapped:)
              forControlEvents:UIControlEventTouchUpInside];
            } else {
                ref.tag = 1000 + subIdx;
                [ref addTarget:self action:@selector(subRefreshTapped:)
              forControlEvents:UIControlEventTouchUpInside];
            }
            [plate addSubview:ref];
            wrap->refresh = ref;

            UIButton *ping = [UIButton buttonWithType:UIButtonTypeCustom];
            ping.contentMode = UIViewContentModeCenter;
            ping.imageView.contentMode = UIViewContentModeScaleAspectFit;
            ping.hidden = NO;
            ping.enabled = !subPingBusy;
            ping.alpha = subPingBusy ? 0.45f : 1.0f;
            [ping setImage:GaugeIcon(iconPx, iconTint)
                  forState:UIControlStateNormal];
            if (manualHasAwg) {
                [ping addTarget:self action:@selector(awgPingTapped:)
               forControlEvents:UIControlEventTouchUpInside];
            } else {
                ping.tag = 3000 + subIdx;
                [ping addTarget:self action:@selector(subPingTapped:)
               forControlEvents:UIControlEventTouchUpInside];
            }
            [plate addSubview:ping];
            wrap->ping = ping;
        }

        UIButton *more = [UIButton buttonWithType:UIButtonTypeCustom];
        more.contentHorizontalAlignment = UIControlContentHorizontalAlignmentCenter;
        more.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
        [more setTitle:@"•••" forState:UIControlStateNormal];
        more.titleLabel.font = [UIFont boldSystemFontOfSize:compact ? 14.0f : 16.0f];
        SenkoStyleSectionGlyph(more);
        [more setTitleColor:iconTint forState:UIControlStateNormal];
        [more setTitleColor:[iconTint colorWithAlphaComponent:0.55f]
                   forState:UIControlStateHighlighted];
        more.titleLabel.shadowColor = nil;
        more.titleLabel.shadowOffset = CGSizeZero;
        if (subIdx < 0) {
            [more addTarget:self action:@selector(showManualMenu)
           forControlEvents:UIControlEventTouchUpInside];
        } else {
            more.tag = 2000 + subIdx;
            [more addTarget:self action:@selector(subMenuTapped:)
           forControlEvents:UIControlEventTouchUpInside];
        }
        [plate addSubview:more];
        wrap->more = more;
    }
    return wrap;
}

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    if ([self isAWGRowAtIndexPath:ip]) {
        static NSString *awgCID = @"awg";
        ServerCell *cell = (ServerCell *)[tv dequeueReusableCellWithIdentifier:awgCID];
        if (!cell) {
            cell = [[[ServerCell alloc] initWithStyle:UITableViewCellStyleDefault
                                      reuseIdentifier:awgCID] autorelease];
        }
        cell.clipsToBounds = YES;
        cell.contentView.clipsToBounds = YES;
        BOOL picked = (_selectedBackend == SenkoBackendAmneziaWG);
        NSString *st = nil;
        if (picked && [_state isEqualToString:@"connected"])
            st = @"on";
        else if (picked && [_state isEqualToString:@"connecting"])
            st = SenkoLocalizedText(@"checking");
        else {
            NSNumber *ms = [_serverStatus objectForKey:[NSNumber numberWithInt:-1]];
            if (ms) st = [NSString stringWithFormat:@"%d ms", [ms intValue]];
        }
/* use the same card style */
        [cell configureWithTitle:@"AmneziaWG"
                          detail:@"awg / udp / full-device"
                          picked:picked
                          status:st];
        [cell setPingTarget:nil action:NULL serverIndex:-1];
        return cell;
    }
    static NSString *cid = @"srv";
    ServerCell *cell = (ServerCell *)[tv dequeueReusableCellWithIdentifier:cid];
    if (!cell) {
        cell = [[[ServerCell alloc] initWithStyle:UITableViewCellStyleDefault
                                  reuseIdentifier:cid] autorelease];
    }
    cell.clipsToBounds = YES;
    cell.contentView.clipsToBounds = YES;
    SenkoServer *sv = [self serverAtIndexPath:ip];
    BOOL picked = (_selectedBackend == SenkoBackendServer && sv &&
                   _selectedSrvIdx >= 0 && sv->index == _selectedSrvIdx);
    NSNumber *msVal = sv
        ? [_serverStatus objectForKey:[NSNumber numberWithInt:sv->index]] : nil;
    BOOL hideLinks = [[NSUserDefaults standardUserDefaults] boolForKey:SENKO_HIDE_LINKS_KEY];
    NSString *shown = sv ? [_rowName objectForKey:
        [NSNumber numberWithInt:sv->index]] : nil;
    [cell configureWithServer:sv picked:picked hideLinks:hideLinks pingVal:msVal
                  displayName:shown];
    [cell setPingTarget:self action:@selector(serverPingTapped:)
            serverIndex:sv ? sv->index : -1];
    return cell;
}

/* rows lift into place the first time they are shown. the set remembers what
   already appeared so scrolling back up does not replay the entrance */
- (void)tableView:(UITableView *)tv willDisplayCell:(UITableViewCell *)cell
forRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv;
    NSString *key = [NSString stringWithFormat:@"%ld.%ld",
                     (long)ip.section, (long)ip.row];
    if ([_revealedRows containsObject:key]) return;
    [_revealedRows addObject:key];
    if ([cell isKindOfClass:[ServerCell class]])
        [(ServerCell *)cell revealAtIndex:(NSUInteger)ip.row];
    else
        SenkoRevealView(cell.contentView, (NSUInteger)ip.row);
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    SenkoThemeSfxPlay(); /* play the cell tap sound */
    if ([self isAWGRowAtIndexPath:ip]) {
        if ([self isServerSelectionLocked]) {
            [tv deselectRowAtIndexPath:ip animated:YES];
            SetStatusDefault(_statusLabel, @"disconnect to switch");
            return;
        }
        if ([self isTunnelActive] && _activeBackend == SenkoBackendServer) {
            [tv deselectRowAtIndexPath:ip animated:YES];
            SetStatusDefault(_statusLabel, @"disconnect to switch backend");
            return;
        }
        _selectedBackend = SenkoBackendAmneziaWG;
        [[NSUserDefaults standardUserDefaults] setInteger:_selectedBackend forKey:SENKO_SELECTED_BACKEND_KEY];
        [[NSUserDefaults standardUserDefaults] synchronize];
        [tv deselectRowAtIndexPath:ip animated:YES];
        NSArray *vis = [tv indexPathsForVisibleRows];
        if ([vis count])
            [tv reloadRowsAtIndexPaths:vis withRowAnimation:UITableViewRowAnimationNone];
        else
            [tv reloadData];
        return;
    }
    /* the card is read-only until its own connect button is used, so it opens
       even while a tunnel is up and the selection itself is locked */
    SenkoServer *sv = [self serverAtIndexPath:ip];
    if (!sv) return;
    [tv deselectRowAtIndexPath:ip animated:YES];
    [self openSheetForServer:sv];
}

/* selection is no longer a side effect of tapping a row: the detail sheet owns
   it, so the same path serves the sheet and any future caller */
- (void)selectServerIndex:(int)index {
    int oldIdx = _selectedSrvIdx;
    _selectedBackend = SenkoBackendServer;
    [[NSUserDefaults standardUserDefaults] setInteger:_selectedBackend forKey:SENKO_SELECTED_BACKEND_KEY];
    [[NSUserDefaults standardUserDefaults] synchronize];
    _selectedSrvIdx = index;
    NSArray *vis = [_table indexPathsForVisibleRows];
    if ([vis count])
        [_table reloadRowsAtIndexPaths:vis withRowAnimation:UITableViewRowAnimationNone];
    else
        [_table reloadData];
    [self applyState];
    if ([self isTunnelActive] && _activeBackend == SenkoBackendServer && oldIdx != index)
        [self switchActiveServerIndex:index];
}

- (SenkoServer *)serverByIndex:(int)index {
    for (SenkoServer *s in _servers)
        if (s->index == index) return s;
    return nil;
}

- (void)openSheetForServer:(SenkoServer *)server {
    if (!server) return;
    [_sheet dismiss];
    [_sheet release];

    NSString *source = SenkoLocalizedText(@"Manual");
    if (server->group >= 0) {
        for (SenkoSub *sub in _subs) {
            if (sub->index != server->group) continue;
            if ([sub->name length]) source = sub->name;
            break;
        }
    }
    BOOL active = [self isTunnelActive] &&
                  _activeBackend == SenkoBackendServer &&
                  _selectedSrvIdx == server->index;
    _sheet = [[SenkoServerSheet alloc]
              initWithServer:server
                        ping:[_serverStatus objectForKey:[NSNumber numberWithInt:server->index]]
                      source:source
                      active:active
                   canMutate:![self isListMutationLocked] && server->group < 0
                    delegate:self];
    [_sheet presentInView:self.view];

    int idx = server->index;
    [_ctl serverLinkIndex:idx reply:^(NSString *link) {
        [_sheet setLink:link];
    }];
}

- (void)serverSheet:(SenkoServerSheet *)sheet
    didChooseAction:(NSString *)action
        serverIndex:(int)index {
    (void)sheet;
    SenkoServer *server = [self serverByIndex:index];
    if (!server) return;

    if ([action isEqualToString:SenkoServerSheetActionConnect]) {
        if ([self isTunnelActive] && _activeBackend == SenkoBackendAmneziaWG) {
            SetStatusDefault(_statusLabel, @"disconnect to switch backend");
            return;
        }
        if ([self isServerSelectionLocked] && _selectedSrvIdx != index) {
            SetStatusDefault(_statusLabel, @"disconnect to switch");
            return;
        }
        BOOL wasActive = [self isTunnelActive] &&
                         _activeBackend == SenkoBackendServer &&
                         _selectedSrvIdx == index;
        if (!wasActive) [self selectServerIndex:index];
        if (wasActive || ![self isTunnelActive])
            [self togglePressed];
        return;
    }
    if ([action isEqualToString:SenkoServerSheetActionPing]) {
        NSNumber *key = [NSNumber numberWithInt:index];
        [_serverStatus setObject:[NSNumber numberWithInt:-3] forKey:key];
        [self reloadServerRowForIndex:index];
        [_ctl pingIndex:index reply:^(int ms) {
            [_serverStatus setObject:[NSNumber numberWithInt:ms] forKey:key];
            [_sheet setPingResult:[NSNumber numberWithInt:ms]];
            [self reloadServerRowForIndex:index];
        }];
        return;
    }
    if ([action isEqualToString:SenkoServerSheetActionEdit]) {
        [_ctl serverLinkIndex:index reply:^(NSString *link) {
            if (![link length]) return;
            EditServerVC *editor = [[[EditServerVC alloc] initWithLink:link
                                                                  index:index
                                                               delegate:self] autorelease];
            UINavigationController *nav = [[[UINavigationController alloc]
                                            initWithRootViewController:editor] autorelease];
            StyleNavBarClassic(nav);
            nav.modalPresentationStyle = UIModalPresentationFullScreen;
            [self presentViewController:nav animated:YES completion:nil];
        }];
        return;
    }
    if ([action isEqualToString:SenkoServerSheetActionDelete]) {
        if ([self isListMutationLocked]) return;
        if (index == _selectedSrvIdx) _selectedSrvIdx = -1;
        [_ctl deleteServerIndex:index reply:^(NSString *reply) {
            (void)reply;
            [self refresh];
        }];
    }
}

/* the editor replaces a profile by identity, so the old entry has to go before
   the new link is added or the list keeps both */
- (void)editServerVC:(EditServerVC *)vc saveLink:(NSString *)link index:(int)idx {
    [_ctl deleteServerIndex:idx reply:^(NSString *reply) {
        if (!reply || [reply hasPrefix:@"ERR"]) return;
        [_ctl addServerLink:link reply:^(NSString *added) {
            if (!added || ![added hasPrefix:@"OK"]) return;
            [vc dismissViewControllerAnimated:YES completion:nil];
            [self refresh];
        }];
    }];
}

- (BOOL)tableView:(UITableView *)tv canEditRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv;
    return ![self isListMutationLocked];
}

- (UITableViewCellEditingStyle)tableView:(UITableView *)tv editingStyleForRowAtIndexPath:(NSIndexPath *)ip {
    return UITableViewCellEditingStyleDelete;
}

- (void)tableView:(UITableView *)tv commitEditingStyle:(UITableViewCellEditingStyle)style forRowAtIndexPath:(NSIndexPath *)ip {
    if (style != UITableViewCellEditingStyleDelete) return;
    if ([self isListMutationLocked]) return;
    if ([self isAWGRowAtIndexPath:ip]) {
        [self removeSavedAWGProfile];
        return;
    }
    SenkoServer *sv = [self serverAtIndexPath:ip];
    if (!sv) return;
    int targetIdx = sv->index;
    if (targetIdx == _selectedSrvIdx) _selectedSrvIdx = -1;
    [_ctl deleteServerIndex:targetIdx reply:^(NSString *reply) {
        (void)reply;
        [self refresh];
    }];
}

- (BOOL)tableView:(UITableView *)tv canMoveRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv;
    if ([self isListMutationLocked] || [self isAWGRowAtIndexPath:ip]) return NO;
    /* a drag writes a position back to the daemon, which a sorted view would
       immediately hide again */
    if ([[NSUserDefaults standardUserDefaults] integerForKey:SENKO_SERVER_SORT_KEY]
        != SenkoSortManual)
        return NO;
    return [self isManualSection:ip.section];
}

- (void)tableView:(UITableView *)tv moveRowAtIndexPath:(NSIndexPath *)from
      toIndexPath:(NSIndexPath *)to {
    if (![self tableView:tv canMoveRowAtIndexPath:from] ||
        from.section != to.section || [self isAWGRowAtIndexPath:to]) {
        [tv reloadData];
        return;
    }
    SenkoServer *server = [self serverAtIndexPath:from];
    if (!server) { [tv reloadData]; return; }
    NSInteger offset = [self awgRowOffsetInSection:from.section];
    NSInteger target = to.row - offset;
    NSArray *oldRows = [[_sections objectAtIndex:from.section] objectForKey:@"rows"];
    NSInteger maxTarget = (NSInteger)[oldRows count] - 1;
    if (target < 0) target = 0;
    if (target > maxTarget) target = maxTarget;

    NSMutableArray *rows = [oldRows mutableCopy];
    NSUInteger sourceRow = [rows indexOfObject:server];
    if (sourceRow == NSNotFound) { [rows release]; [tv reloadData]; return; }
    [rows removeObjectAtIndex:sourceRow];
    if (target > (NSInteger)[rows count]) target = (NSInteger)[rows count];
    [rows insertObject:server atIndex:(NSUInteger)target];
    NSMutableDictionary *sec = [[_sections objectAtIndex:from.section] mutableCopy];
    [sec setObject:rows forKey:@"rows"];
    [_sections replaceObjectAtIndex:from.section withObject:sec];
    [sec release];
    [rows release];

    [_ctl moveManualServerIndex:server->index toPosition:(int)target reply:^(NSString *reply) {
        [tv setEditing:NO animated:YES];
        if (!reply || [reply hasPrefix:@"ERR"]) {
            [self refresh];
            return;
        }
        SetStatusRefresh(_statusLabel, @"server moved");
        [self refresh];
    }];
}

- (void)subRefreshTapped:(UIButton *)btn {
    int subIdx = (int)btn.tag - 1000;
    if (subIdx < 0 || [self isListMutationLocked] ||
        ![self subscriptionByIndex:subIdx]) return;
    SetStatusRefresh(_statusLabel, @"refreshing subscription...");
    [_ctl refreshSubIndex:subIdx reply:^(NSString *reply) {
        if (reply && [reply hasPrefix:@"ERR"]) {
            [self setLastErr:[reply stringByTrimmingCharactersInSet:
                  [NSCharacterSet whitespaceAndNewlineCharacterSet]]];
            [self applyState];
        } else {
            SetStatusRefresh(_statusLabel, @"subscription updated");
        }
        [self refresh];
    }];
}

- (void)subPingTapped:(UIButton *)btn {
    int subIdx = (int)btn.tag - 3000;
    if (subIdx < 0 || [self isListMutationLocked] ||
        ![self subscriptionByIndex:subIdx]) return;
    [_pingMode release];
    _pingMode = [@"handshake" copy];
    [self pingServersInSub:subIdx];
}

- (void)subMenuTapped:(UIButton *)btn {
    int subIdx = (int)btn.tag - 2000;
    if (subIdx < 0 || [self isListMutationLocked] ||
        ![self subscriptionByIndex:subIdx]) return;
    [self dismissCurrentActionSheetAnimated:NO];
    _menuSubIdx = subIdx;
    UIActionSheet *as = [[UIActionSheet alloc]
                         initWithTitle:SenkoLocalizedText(@"Subscription")
                         delegate:self
                         cancelButtonTitle:SenkoLocalizedText(@"Cancel")
                         destructiveButtonTitle:SenkoLocalizedText(@"Remove")
                         otherButtonTitles:SenkoLocalizedText(@"Refresh now"),
                                           SenkoLocalizedText(@"Check ping"),
                                           SenkoLocalizedText(@"Subscription details"),
                                           SenkoLocalizedText(@"Edit details"), nil];
    as.tag = 400000 + subIdx;
    _actionSheet = as;
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad &&
        [as respondsToSelector:@selector(showFromRect:inView:animated:)])
        [as showFromRect:btn.bounds inView:btn animated:YES];
    else
        [as showInView:self.view];
}

- (void)awgRefreshTapped:(UIButton *)btn {
    (void)btn;
    if (_activeBackend != SenkoBackendAmneziaWG) {
        SetStatusRefresh(_statusLabel, @"amneziawg profile loaded");
        return;
    }
    [_ctl stopAWG:^(NSString *status) {
        NSString *clean = [status stringByTrimmingCharactersInSet:
                           [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (!clean.length || ![clean hasPrefix:@"idle"]) {
            [self setLastErr:@"could not stop amneziawg"];
            [self applyState];
            return;
        }
        _activeBackend = SenkoBackendNone;
        [self startSavedAWGProfile];
    }];
}

- (void)awgPingTapped:(UIButton *)btn {
    (void)btn;
    SetStatusRefresh(_statusLabel, @"checking amneziawg...");
    [_ctl probeAWGAtPath:[self awgProfilePath] reply:^(NSString *reply) {
        if (!reply) {
            SetStatusRefresh(_statusLabel, @"amneziawg timeout");
            return;
        }
        NSRange mark = [reply rangeOfString:@"PING "];
        if (mark.location == NSNotFound) {
            SetStatusRefresh(_statusLabel, @"amneziawg timeout");
            return;
        }
        NSInteger ms = [[reply substringFromIndex:mark.location + mark.length] integerValue];
        [_serverStatus setObject:[NSNumber numberWithInteger:ms]
                          forKey:[NSNumber numberWithInt:-1]];
        SetStatusRefresh(_statusLabel, [NSString stringWithFormat:@"%ld ms", (long)ms]);
        NSArray *vis = [_table indexPathsForVisibleRows];
        if ([vis count])
            [_table reloadRowsAtIndexPaths:vis withRowAnimation:UITableViewRowAnimationNone];
    }];
}

- (void)refreshSubscriptionIndex:(int)pos {
    NSMutableArray *idxs = [NSMutableArray array];
    for (SenkoSub *s in _subs)
        [idxs addObject:[NSNumber numberWithInt:s->index]];
    if (pos >= (int)[idxs count]) {
        [self refresh];
        SetStatusRefresh(_statusLabel, @"subscriptions refreshed");
        return;
    }
    int subIdx = [[idxs objectAtIndex:pos] intValue];
    [_ctl refreshSubIndex:subIdx reply:^(NSString *reply) {
        (void)reply;
        [self refreshSubscriptionIndex:(pos + 1)];
    }];
}

- (void)refreshPressed {
    if ([_subs count] == 0) {
        [self refresh];
        SetStatusRefresh(_statusLabel, @"list reloaded");
        return;
    }
    SetStatusRefresh(_statusLabel, @"refreshing subscriptions...");
    [self refreshSubscriptionIndex:0];
}

- (void)pingPressed {
    if ([_servers count] == 0) {
        SetStatusDefault(_statusLabel, @"no servers to ping");
        return;
    }
    if ([self isTunnelActive]) {
        SetStatusDefault(_statusLabel, @"disconnect to check profiles");
        return;
    }
    [self startPingSweep];
}

- (void)serverPingTapped:(UIButton *)button {
    int serverIndex = (int)button.tag;
    if (serverIndex < 0 || [self isTunnelActive]) {
        SetStatusDefault(_statusLabel, @"disconnect to check profiles");
        return;
    }
    NSInteger generation = ++_checkGeneration;
    NSNumber *key = [NSNumber numberWithInt:serverIndex];
    [_serverStatus setObject:[NSNumber numberWithInt:-3] forKey:key];
    [self reloadServerRowForIndex:serverIndex];
    SetStatusDefault(_statusLabel, @"checking server...");
    [_ctl checkIndex:serverIndex mode:@"handshake" reply:^(int ms, NSString *error) {
        if (generation != _checkGeneration) return;
        [_serverStatus setObject:[NSNumber numberWithInt:ms] forKey:key];
        [self reloadServerRowForIndex:serverIndex];
        SetStatusDefault(_statusLabel, ms >= 0
            ? [NSString stringWithFormat:@"%@: %d ms",
               SenkoLocalizedText(@"profile works"), ms]
            : SenkoHumanReadableError(error ?: @"profile check failed"));
    }];
}

- (void)startPingSweep {
    _checkGeneration++;
    [_pingMode release];
    _pingMode = [@"handshake" copy];
    NSSet *busySubs = [_pingingSubs copy];
    [_pingingSubs removeAllObjects];
    [_serverStatus removeAllObjects];
    [self updateSubscriptionPingButtons:busySubs];
    [busySubs release];
    SetStatusDefault(_statusLabel, @"checking profiles...");
    NSMutableArray *indexes = [NSMutableArray array];
    for (SenkoServer *server in _servers)
        [indexes addObject:[NSNumber numberWithInt:server->index]];
    [self beginBoundedPing:indexes subIndex:-1 generation:_checkGeneration];
}

- (void)updateSubscriptionPingButtons:(NSSet *)subIndexes {
    if (!_table || ![subIndexes count] || !_sections || ![_sections count]) return;
/* headerViewForSection: only exists from ios 6; on ios 5 the live header is
   reached through the section view the delegate handed back */
    BOOL canAskTable = [_table respondsToSelector:@selector(headerViewForSection:)];
    for (NSInteger section = 0; section < (NSInteger)[_sections count]; ++section) {
        if (section >= [_table numberOfSections]) break;
        int subIdx = [[[_sections objectAtIndex:section] objectForKey:@"subIdx"] intValue];
        if (subIdx >= 0 &&
            [subIndexes containsObject:[NSNumber numberWithInt:subIdx]]) {
            UIView *header = canAskTable
                ? [_table headerViewForSection:section]
                : [_table viewWithTag:(7000 + (NSInteger)section)];
            UIButton *ping = (UIButton *)[header viewWithTag:(3000 + subIdx)];
            if (![ping isKindOfClass:[UIButton class]]) continue;
            BOOL busy = [_pingingSubs containsObject:
                         [NSNumber numberWithInt:subIdx]];
            ping.hidden = NO;
            ping.enabled = !busy;
            ping.alpha = busy ? 0.45f : 1.0f;
        }
    }
}

- (void)reloadServerRowForIndex:(int)serverIndex {
    if (!_table) return;
    for (NSInteger section = 0; section < (NSInteger)[_sections count]; ++section) {
        if (section >= [_table numberOfSections]) return;
        NSDictionary *sectionInfo = [_sections objectAtIndex:section];
        int subIdx = [[sectionInfo objectForKey:@"subIdx"] intValue];
        if (subIdx >= 0 &&
            [_collapsedSubs containsObject:[NSNumber numberWithInt:subIdx]])
            continue;
        NSArray *rows = [[_sections objectAtIndex:section] objectForKey:@"rows"];
        for (NSInteger row = 0; row < (NSInteger)[rows count]; ++row) {
            SenkoServer *server = [rows objectAtIndex:row];
            if (server->index != serverIndex) continue;
            if (row >= [_table numberOfRowsInSection:section]) return;
            NSIndexPath *path = [NSIndexPath indexPathForRow:row inSection:section];
            /* offscreen rows pick up the cached ping when they appear */
            if (![[_table indexPathsForVisibleRows] containsObject:path]) return;
            [_table reloadRowsAtIndexPaths:[NSArray arrayWithObject:path]
                          withRowAnimation:UITableViewRowAnimationNone];
            return;
        }
    }
}

- (void)checkStatusOfServerAtIndex:(NSUInteger)idx generation:(NSInteger)gen {
    if (gen != _checkGeneration) return;
    if (idx >= [_servers count]) {
        SetStatusDefault(_statusLabel, @"profile check complete");
        return;
    }
    SenkoServer *sv = [_servers objectAtIndex:idx];
    NSNumber *key = [NSNumber numberWithInt:sv->index];
    [_ctl pingIndex:sv->index reply:^(int ms) {
        if (gen != _checkGeneration) return;
        [_serverStatus setObject:[NSNumber numberWithInt:ms] forKey:key];
        [self reloadServerRowForIndex:sv->index];
        [self checkStatusOfServerAtIndex:(idx + 1) generation:gen];
    }];
}

- (void)pingServersInSub:(int)subIdx {
    NSNumber *subKey = [NSNumber numberWithInt:subIdx];
    if ([_pingingSubs containsObject:subKey]) return;
    NSMutableArray *idxs = [NSMutableArray array];
    for (SenkoServer *sv in _servers) {
        if (sv->group == subIdx)
            [idxs addObject:[NSNumber numberWithInt:sv->index]];
    }
    if ([idxs count] == 0) {
        SetStatusDefault(_statusLabel, @"no servers in group");
        return;
    }
    [_pingingSubs addObject:subKey];
    [self updateSubscriptionPingButtons:[NSSet setWithObject:
                                         [NSNumber numberWithInt:subIdx]]];
    _checkGeneration++;
    NSInteger gen = _checkGeneration;
    SetStatusDefault(_statusLabel, @"checking group profiles...");
    [self beginBoundedPing:idxs subIndex:subIdx generation:gen];
}

- (void)beginBoundedPing:(NSArray *)idxs subIndex:(int)subIdx generation:(NSInteger)gen {
    [_pingQueue release];
    _pingQueue = [idxs copy];
    _pingNext = 0;
    _pingPending = 0;
    _pingCompleted = 0;
    _pingSubIndex = subIdx;
    if (!_pingMode) _pingMode = [@"tcp" copy];
    [self launchBoundedPingsForGeneration:gen];
}

- (void)launchBoundedPingsForGeneration:(NSInteger)gen {
    if (gen != _checkGeneration) return;
    NSUInteger parallelLimit = [_pingMode isEqualToString:@"handshake"] ? 1 : 3;
    while (_pingPending < parallelLimit && _pingNext < [_pingQueue count]) {
        int serverIndex = [[_pingQueue objectAtIndex:_pingNext++] intValue];
        _pingPending++;
        [_ctl checkIndex:serverIndex mode:_pingMode reply:^(int ms, NSString *error) {
            (void)error;
            if (gen != _checkGeneration) return;
            _pingPending--;
            _pingCompleted++;
            [_serverStatus setObject:[NSNumber numberWithInt:ms]
                              forKey:[NSNumber numberWithInt:serverIndex]];
            [self reloadServerRowForIndex:serverIndex];
            if (_pingCompleted >= [_pingQueue count])
                [self finishBoundedPing];
            else
                [self launchBoundedPingsForGeneration:gen];
        }];
    }
}

- (void)finishBoundedPing {
    [_servers sortUsingComparator:^NSComparisonResult(SenkoServer *a, SenkoServer *b) {
        int av = [[_serverStatus objectForKey:[NSNumber numberWithInt:a->index]] intValue];
        int bv = [[_serverStatus objectForKey:[NSNumber numberWithInt:b->index]] intValue];
        if (av < 0) av = INT_MAX;
        if (bv < 0) bv = INT_MAX;
        if (av < bv) return NSOrderedAscending;
        if (av > bv) return NSOrderedDescending;
        return NSOrderedSame;
    }];
    [self rebuildSections];
    [_table reloadData];
    if (_pingSubIndex >= 0) {
        NSNumber *key = [NSNumber numberWithInt:_pingSubIndex];
        [_pingingSubs removeObject:key];
        [self updateSubscriptionPingButtons:[NSSet setWithObject:key]];
        SetStatusDefault(_statusLabel, @"group profile check complete");
    } else {
        SetStatusDefault(_statusLabel, @"profile check complete");
    }
    [_pingQueue release];
    _pingQueue = nil;
}

- (void)pingIndexList:(NSArray *)idxs at:(NSUInteger)i generation:(NSInteger)gen {
    if (gen != _checkGeneration) return;
    if (i >= [idxs count]) {
        int subIdx = -1;
        if ([idxs count]) {
            int firstServerIdx = [[idxs objectAtIndex:0] intValue];
            for (SenkoServer *sv in _servers) {
                if (sv->index == firstServerIdx) {
                    subIdx = sv->group;
                    [_pingingSubs removeObject:[NSNumber numberWithInt:sv->group]];
                    break;
                }
            }
        }
        if (subIdx >= 0)
            [self updateSubscriptionPingButtons:[NSSet setWithObject:
                                                 [NSNumber numberWithInt:subIdx]]];
        SetStatusDefault(_statusLabel, @"group profile check complete");
        return;
    }
    int srv = [[idxs objectAtIndex:i] intValue];
    [_ctl pingIndex:srv reply:^(int ms) {
        if (gen != _checkGeneration) return;
        [_serverStatus setObject:[NSNumber numberWithInt:ms]
                          forKey:[NSNumber numberWithInt:srv]];
        [self reloadServerRowForIndex:srv];
        [self pingIndexList:idxs at:(i + 1) generation:gen];
    }];
}

- (void)startStatusChecks {
    NSSet *busySubs = [_pingingSubs copy];
    [_pingingSubs removeAllObjects];
    [self updateSubscriptionPingButtons:busySubs];
    [busySubs release];
    _checkGeneration++;
}


@end
