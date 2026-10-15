#import "main_vc_priv.h"

/* the table hands a section header its real frame after the delegate builds it,
   and it hands it a new one on every reload and rotation. measuring the plate
   once from the table bounds left the header at the width the table had before
   the reload, which is how collapsing a subscription pushed its plate past the
   right edge. the header owns its geometry instead */
@interface SenkoSectionHeader : UIView {
@public
    UIView          *plate;
    UIButton        *collapse;
    UILabel         *title;
    UILabel         *meta;
    UIButton        *refresh;
    UIButton        *ping;
    UIButton        *more;
    UIView          *backdrop;
    BOOL             compact;
    CGSize           styledSize;
}
- (void)stylePlate;
- (void)setPinned:(BOOL)pinned;
@end

@implementation SenkoSectionHeader

/* the ios16 drop shadow and the ios26 glass are both cut for the plate bounds,
   so they have to be recut whenever the plate actually gets a size */
/* a group heading is text over the screen, not a second card: two stacked
   plates per group is what made the list read as loose pieces */
- (void)stylePlate {
    if (!plate) return;
    SenkoRemoveFrost(plate);
    plate.backgroundColor = [UIColor clearColor];
    plate.layer.borderWidth = 0.0f;
    plate.layer.shadowOpacity = 0.0f;
    plate.layer.shouldRasterize = NO;
}

/* a plain table pins the heading over its rows; clear, its text overlaps theirs */
- (void)setPinned:(BOOL)pinned {
    if (pinned && !backdrop) {
        backdrop = [[UIView alloc] initWithFrame:self.bounds];
        backdrop.userInteractionEnabled = NO;
        backdrop.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                    UIViewAutoresizingFlexibleHeight;
        [self insertSubview:backdrop atIndex:0];
    }
    if (!backdrop) return;
    backdrop.backgroundColor = [kBG colorWithAlphaComponent:0.86f];
    backdrop.hidden = !pinned;
}

- (void)dealloc {
    [backdrop release];
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;
    if (w < 1.0f || h < 1.0f || !plate) return;

    CGFloat plateW = w;
    CGFloat plateH = h - 6.0f;
    if (plateH < 20.0f) plateH = 20.0f;
    plate.frame = CGRectMake(0.0f, 6.0f, plateW, plateH);
    if (!CGSizeEqualToSize(styledSize, plate.bounds.size)) {
        styledSize = plate.bounds.size;
        [self stylePlate];
    }
    CGFloat cr = SenkoThemeCardRadius();
    plate.layer.cornerRadius = cr;
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

    CGFloat textX = 4.0f;
    CGFloat labelWidth = textRight - textX;
    if (labelWidth < 42.0f) labelWidth = 42.0f;
    title.frame = CGRectMake(textX, 4.0f, labelWidth, 22.0f);
    meta.frame = CGRectMake(textX, 26.0f, labelWidth, 18.0f);
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
        return SenkoLanguageIsChinese() ? @"已过期"
             : SenkoLanguageIsRussian() ? @"истекла" : @"expired";
    NSDateFormatter *fmt = [[[NSDateFormatter alloc] init] autorelease];
    fmt.locale = [[[NSLocale alloc] initWithLocaleIdentifier:
        SenkoLanguageIsChinese() ? @"zh_CN"
        : SenkoLanguageIsRussian() ? @"ru_RU" : @"en_US"] autorelease];
    fmt.dateFormat = SenkoLanguageIsChinese() ? @"至 yyyy-MM-dd"
        : SenkoLanguageIsRussian() ? @"'до' dd.MM.yy" : @"'until' MM/dd/yy";
    return [fmt stringFromDate:date];
}

static NSString * const kSenkoSubscriptionRefreshKey = @"senkoSubscriptionRefresh";
static NSString * const kSenkoPingButtonKey = @"senkoPingButton";

/* core animation keeps the icon moving without asking uikit to redraw a whole
   header for every frame. the operation remains serial, so one icon per active
   group tells the truth about what the daemon is doing. */
static void SenkoSetButtonSpinning(UIButton *button, NSString *key, BOOL spinning) {
    CALayer *layer;
    CABasicAnimation *turn;
    if (!button || ![key length]) return;
    layer = button.imageView ? button.imageView.layer : button.layer;
    if (!spinning) {
        [layer removeAnimationForKey:key];
        return;
    }
    if ([layer animationForKey:key]) return;
    turn = [CABasicAnimation animationWithKeyPath:@"transform.rotation.z"];
    turn.fromValue = [NSNumber numberWithFloat:0.0f];
    turn.toValue = [NSNumber numberWithFloat:(float)(M_PI * 2.0)];
    turn.duration = 0.78;
    turn.repeatCount = HUGE_VALF;
    turn.timingFunction = [CAMediaTimingFunction
        functionWithName:kCAMediaTimingFunctionLinear];
    [layer addAnimation:turn forKey:key];
}

/* the gauge glyph is a half-circle dial with a needle pivoting off its own
   center, drawn to sit still, not to turn: spinning it in place made it wobble
   around a point that is not its own middle instead of spinning cleanly. the
   refresh glyph is drawn for exactly this job, so it stands in as the icon
   while a ping is in flight. a button with no image at all (the classic
   check button, which shows text instead) has nothing to swap */
static void SenkoSetGaugeSpinning(UIButton *button, BOOL spinning, UIColor *tint) {
    UIImage *current = [button imageForState:UIControlStateNormal];
    if (button && current) {
        CGFloat side = current.size.width > 0 ? current.size.width : 20.0f;
        [button setImage:(spinning ? SenkoIconRefresh(side, tint) : GaugeIcon(side, tint))
                forState:UIControlStateNormal];
    }
    SenkoSetButtonSpinning(button, kSenkoPingButtonKey, spinning);
}


static NSString *SenkoSubscriptionUsage(SenkoSub *sub) {
    if (!sub || (!sub->total && !sub->upload && !sub->download)) return nil;
    unsigned long long used = sub->upload + sub->download;
    if (sub->total) {
        unsigned long long left = sub->total > used ? sub->total - used : 0;
        return SenkoLanguageIsChinese()
            ? [NSString stringWithFormat:@"剩余 %@", SenkoFormatBytes(left)]
            : SenkoLanguageIsRussian()
            ? [NSString stringWithFormat:@"осталось %@", SenkoFormatBytes(left)]
            : [NSString stringWithFormat:@"%@ left", SenkoFormatBytes(left)];
    }
    return SenkoLanguageIsChinese()
        ? [NSString stringWithFormat:@"已用 %@", SenkoFormatBytes(used)]
        : SenkoLanguageIsRussian()
        ? [NSString stringWithFormat:@"исп. %@", SenkoFormatBytes(used)]
        : [NSString stringWithFormat:@"%@ used", SenkoFormatBytes(used)];
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

/* the drag carries a copy of the header under the finger. a layer holding a
   live UIVisualEffectView cannot be copied with renderInContext:: the backdrop
   samples the render server's surface, a bitmap context has none, and core
   image faults reaching for it. snapshotViewAfterScreenUpdates: is the
   supported copy and it landed in ios 7, so it is asked for by selector; the
   bitmap above stays for older systems, which have no backdrop views to trip
   over in the first place */
static UIView *SenkoSectionDragProxy(UIView *view) {
    if (!view || view.bounds.size.width < 1.0f || view.bounds.size.height < 1.0f)
        return nil;
    if ([view respondsToSelector:@selector(snapshotViewAfterScreenUpdates:)]) {
        UIView *snap = ((UIView *(*)(id, SEL, BOOL))objc_msgSend)(
            view, @selector(snapshotViewAfterScreenUpdates:), NO);
        if (snap) return snap;
    }
    UIImage *image = SenkoSectionSnapshot(view);
    if (!image) return nil;
    return [[[UIImageView alloc] initWithImage:image] autorelease];
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

/* the display name each row would show after stripping the head every row in
   the section shares (see SenkoSharedNameHead), one entry per row in order */
static NSArray *SenkoShownNames(NSArray *rows) {
    NSMutableArray *raw = [NSMutableArray array];
    for (SenkoServer *sv in rows) {
        NSString *name = SenkoServerDisplayName(sv->remark);
        [raw addObject:name ? name : @""];
    }
    NSString *head = SenkoSharedNameHead(raw);
    if (![head length]) return raw;
    NSMutableArray *shown = [NSMutableArray array];
    for (NSString *name in raw) {
        NSString *out = name;
        if ([name length] > [head length]) {
            out = [[name substringFromIndex:[head length]]
                   stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (![out length]) out = name;
        }
        [shown addObject:out];
    }
    return shown;
}

/* display names are user text, so case and repeated whitespace must not split
   one country into several rows after the visible spelling has become equal */
static NSString *SenkoCollapseNameKey(NSString *name) {
    if (![name length]) return @"";
    NSArray *words = [name componentsSeparatedByCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSMutableArray *kept = [NSMutableArray array];
    for (NSString *word in words)
        if ([word length]) [kept addObject:word];
    return [[kept componentsJoinedByString:@" "] lowercaseString];
}

/* rows that reduce to the same name (same country, different transport or
   host) collapse into one: the first one keeps the row and remembers the
   rest in dupIndexes, so connecting to it can try them in turn instead of the
   list showing several identical-looking entries for what the user reads as
   one server. */
- (NSArray *)collapsedRows:(NSArray *)rows names:(NSMutableDictionary *)rowNames {
    NSArray *shown = SenkoShownNames(rows);
    NSMutableArray *out = [NSMutableArray array];
    NSMutableDictionary *repByName = [NSMutableDictionary dictionary];
    NSUInteger i = 0;
    for (SenkoServer *sv in rows) {
        [sv->dupIndexes release];
        sv->dupIndexes = nil; /* drop whatever an earlier rebuild left here */
        NSString *name = [shown objectAtIndex:i++];
        NSNumber *indexKey = [NSNumber numberWithInt:sv->index];
        if ([name length]) [rowNames setObject:name forKey:indexKey];
        if (![name length]) { [out addObject:sv]; continue; }
        NSString *key = SenkoCollapseNameKey(name);
/* a red placeholder folded under a working row would hide it and hand connect
   a member it can only fail on */
        if (!sv->supported) key = [key stringByAppendingString:@"\n-unsupported"];
        SenkoServer *rep = [repByName objectForKey:key];
        if (!rep) {
            [repByName setObject:sv forKey:key];
            [out addObject:sv];
            continue;
        }
        BOOL svCurrent = (_selectedBackend == SenkoBackendServer &&
                          sv->index == _selectedSrvIdx) || sv->selected;
        BOOL repCurrent = (_selectedBackend == SenkoBackendServer &&
                           rep->index == _selectedSrvIdx) || rep->selected;
        if (svCurrent && !repCurrent) {
/* the ui selection can be newer than the last catalog answer. whichever side
   owns that selection has to keep the row, the ping label and the connect
   button together */
            NSMutableArray *members = rep->dupIndexes
                ? [rep->dupIndexes mutableCopy]
                : [[NSMutableArray alloc] initWithObjects:
                   [NSNumber numberWithInt:rep->index], nil];
            NSNumber *selectedIndex = [NSNumber numberWithInt:sv->index];
            if (![members containsObject:selectedIndex])
                [members addObject:selectedIndex];
            NSArray *old = sv->dupIndexes;
            sv->dupIndexes = [members copy];
            [old release];
            [members release];
            NSUInteger repPos = [out indexOfObjectIdenticalTo:rep];
            if (repPos != NSNotFound) [out replaceObjectAtIndex:repPos withObject:sv];
            [rowNames removeObjectForKey:[NSNumber numberWithInt:rep->index]];
            [repByName setObject:sv forKey:key];
            continue;
        }
        NSMutableArray *members = rep->dupIndexes
            ? [rep->dupIndexes mutableCopy]
            : [[NSMutableArray alloc] initWithObjects:
               [NSNumber numberWithInt:rep->index], nil];
        [members addObject:[NSNumber numberWithInt:sv->index]];
        NSArray *old = rep->dupIndexes;
        rep->dupIndexes = [members copy];
        [old release];
        [members release];
    }
    return out;
}

static BOOL SenkoSameMeasuredCatalog(NSArray *before, NSArray *after) {
    if ([before count] != [after count]) return NO;
    for (NSUInteger i = 0; i < [before count]; ++i) {
        SenkoServer *a = [before objectAtIndex:i];
        SenkoServer *b = [after objectAtIndex:i];
        if (a->index != b->index || a->group != b->group || a->port != b->port ||
            ![a->host isEqualToString:b->host] ||
            ![a->proto isEqualToString:b->proto] ||
            ![a->net isEqualToString:b->net] ||
            ![a->security isEqualToString:b->security] ||
            ![a->remark isEqualToString:b->remark])
            return NO;
    }
    return YES;
}

- (void)applyCatalog:(NSArray *)servers subs:(NSArray *)subs order:(NSArray *)order {
    _catalogLoaded = YES;
    [self hideBusyOverlay];
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
    BOOL sameMeasuredCatalog = SenkoSameMeasuredCatalog(_servers, servers);
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
    if (!sameMeasuredCatalog) {
        [_serverStatus removeAllObjects];
    } else {
        /* an interrupted check has no result to carry into the new catalog */
        for (NSNumber *key in [_serverStatus allKeys]) {
            if ([[_serverStatus objectForKey:key] intValue] == -3)
                [_serverStatus removeObjectForKey:key];
        }
    }
    [self rebuildSections];
    [self reconcileSelectionAfterListKeeping:oldSelected];
    [oldSelected release];
    [self layoutMainChrome];
    [_table reloadData];
    [_table layoutIfNeeded];
    /* reloadData discards the old rows only after the catalog has been
       replaced. placing the empty panel before that used the old contentSize,
       concluded that the screen was full, and kept it hidden until relaunch. */
    [self syncEmptyState];
    [self applyState];
}

/* servers without a reading sort last: an unmeasured row is not fast, it is
   unknown, and burying it keeps the top of the list meaningful */
static int SenkoSortRank(NSNumber *ms) {
    if (!ms) return 1;
    int v = [ms intValue];
    return v >= 0 ? 0 : 2;
}

/* a collapsed row's own measurement may be missing or worse than a hidden
   sibling's, so both the ping label and the sort order read the best of the
   whole group rather than just the representative's own entry */
- (NSNumber *)bestPingForServer:(SenkoServer *)sv {
    NSArray *idxs = [sv->dupIndexes count]
        ? sv->dupIndexes
        : [NSArray arrayWithObject:[NSNumber numberWithInt:sv->index]];
    NSNumber *best = nil;
    BOOL anyChecked = NO;
    for (NSNumber *idxNum in idxs) {
        NSNumber *ms = [_serverStatus objectForKey:idxNum];
        if (!ms) continue;
        anyChecked = YES;
        if ([ms intValue] == -3) return ms; /* a member is mid-check: show that */
        if ([ms intValue] >= 0 && (!best || [ms intValue] < [best intValue]))
            best = ms;
    }
    if (best) return best;
    return anyChecked ? [NSNumber numberWithInt:-1] : nil;
}

- (NSArray *)sortedRows:(NSArray *)rows {
    SenkoServerSort mode = (SenkoServerSort)
        [[NSUserDefaults standardUserDefaults] integerForKey:SENKO_SERVER_SORT_KEY];
    if (mode != SenkoSortName && mode != SenkoSortPing) return rows;
    NSMutableArray *out = [NSMutableArray arrayWithArray:rows];
    [out sortUsingComparator:^NSComparisonResult(SenkoServer *a, SenkoServer *b) {
        NSString *na = SenkoServerDisplayName(a->remark);
        NSString *nb = SenkoServerDisplayName(b->remark);
        if (![na length]) na = a->host ? a->host : @"";
        if (![nb length]) nb = b->host ? b->host : @"";
        if (mode == SenkoSortPing) {
            NSNumber *pa = [self bestPingForServer:a];
            NSNumber *pb = [self bestPingForServer:b];
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
    NSMutableArray *secs = [NSMutableArray array];
    NSMutableArray *manual = [NSMutableArray array];
    NSMutableDictionary *bySub = [NSMutableDictionary dictionary];
    NSMutableDictionary *flagBySub = [NSMutableDictionary dictionary];
    NSMutableDictionary *rowNames = [NSMutableDictionary dictionary];

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
                             [self sortedRows:[self collapsedRows:manual names:rowNames]], @"rows", nil]];
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
                         [self sortedRows:[self collapsedRows:rows names:rowNames]], @"rows", nil]];
        [seen addObject:key];
    }

    if ([manual count] > 0 || [self hasAWGProfile]) {
        NSNumber *manualKey = [NSNumber numberWithInt:-1];
        if (![seen containsObject:manualKey])
            [secs insertObject:[NSDictionary dictionaryWithObjectsAndKeys:
                               @"Manual", @"title", manualKey, @"subIdx",
                               [self sortedRows:[self collapsedRows:manual names:rowNames]], @"rows", nil] atIndex:0];
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
                         key, @"subIdx", [self sortedRows:[self collapsedRows:rows names:rowNames]], @"rows", nil]];
        [_sectionOrder addObject:key];
    }

    [_rowName release];
    _rowName = [rowNames copy];
    [self applyPickerFilterTo:secs];

    /* the fold state belongs to every group, not only to the ones the filter
       lets through right now */
    NSMutableSet *valid = [NSMutableSet set];
    for (NSDictionary *sec in secs) {
        int subIdx = [[sec objectForKey:@"subIdx"] intValue];
        if (subIdx >= 0) [valid addObject:[NSNumber numberWithInt:subIdx]];
    }
    [_collapsedSubs intersectSet:valid];
}

/* a search reads the name the row shows, the remark behind it and the host,
   because a panel feed often puts the city in only one of them */
static BOOL SenkoServerMatchesQuery(SenkoServer *sv, NSString *shown, NSString *query) {
    NSString *remark = SenkoServerDisplayName(sv->remark);
    NSString *code = SenkoServerFlagCode(sv->remark);
    NSArray *fields = [NSArray arrayWithObjects:shown ? shown : @"",
                       remark ? remark : @"", sv->host ? sv->host : @"",
                       code ? code : @"", nil];
    for (NSString *field in fields) {
        if ([field length] &&
            [field rangeOfString:query options:NSCaseInsensitiveSearch].location != NSNotFound)
            return YES;
    }
    return NO;
}

- (void)applyPickerFilterTo:(NSMutableArray *)secs {
    NSMutableArray *titles = [NSMutableArray arrayWithObject:SenkoLocalizedText(@"All")];
    NSMutableArray *keys = [NSMutableArray arrayWithObject:[NSNull null]];
    for (NSDictionary *sec in secs) {
        NSNumber *key = [sec objectForKey:@"subIdx"];
        NSString *flag = [sec objectForKey:@"flag"];
        NSString *title = [key intValue] < 0 ? SenkoLocalizedText(@"Manual")
                                             : [sec objectForKey:@"title"];
        if ([flag length]) title = [NSString stringWithFormat:@"%@ %@", flag, title];
        [titles addObject:title];
        [keys addObject:key];
    }
    BOOL hasAWG = [self hasAWGProfile];
    NSNumber *awgKey = [NSNumber numberWithInt:-2];
    if (hasAWG) {
        [titles addObject:@"AmneziaWG"];
        [keys addObject:awgKey];
    }
    if (_pickerFilter && ![keys containsObject:_pickerFilter]) {
        [_pickerFilter release];
        _pickerFilter = nil;
    }

    NSString *query = [_pickerQuery stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    BOOL awgOnly = _pickerFilter && [_pickerFilter isEqualToNumber:awgKey];
    _awgRowShown = hasAWG && (![query length] ||
        [@"AmneziaWG" rangeOfString:query options:NSCaseInsensitiveSearch].location != NSNotFound);

    NSMutableArray *shown = [NSMutableArray array];
    for (NSDictionary *sec in secs) {
        int subIdx = [[sec objectForKey:@"subIdx"] intValue];
        if (awgOnly) {
            if (subIdx != -1 || !_awgRowShown) continue;
            NSMutableDictionary *only = [[sec mutableCopy] autorelease];
            [only setObject:[NSArray array] forKey:@"rows"];
            [shown addObject:only];
            continue;
        }
        if (_pickerFilter && subIdx != [_pickerFilter intValue]) continue;
        if ([query length]) {
            NSMutableArray *rows = [NSMutableArray array];
            for (SenkoServer *sv in [sec objectForKey:@"rows"]) {
                NSString *name = [_rowName objectForKey:[NSNumber numberWithInt:sv->index]];
                if (SenkoServerMatchesQuery(sv, name, query)) [rows addObject:sv];
            }
            if (![rows count] && !(subIdx == -1 && _awgRowShown)) continue;
            NSMutableDictionary *narrowed = [[sec mutableCopy] autorelease];
            [narrowed setObject:rows forKey:@"rows"];
            [shown addObject:narrowed];
            continue;
        }
        [shown addObject:sec];
    }
    [_sections release];
    _sections = [shown retain];
    [_chipKeys release];
    _chipKeys = [keys copy];
    NSUInteger selected = _pickerFilter ? [keys indexOfObject:_pickerFilter] : 0;
    [_picker setChipTitles:titles
                  selected:selected == NSNotFound ? 0 : (NSInteger)selected];
    [self syncPickerChrome];
}

- (void)syncPickerChrome {
    BOOL autoOn = SenkoAutoServerEnabled() && _selectedBackend == SenkoBackendServer;
    NSString *subtitle = SenkoLocalizedText(@"Fastest server");
    SenkoServer *picked = [self serverByIndex:_selectedSrvIdx];
    if (autoOn && picked && [self isTunnelActive] && _activeBackend == SenkoBackendServer)
        subtitle = [self nameForServer:picked];
    BOOL awgOnly = _pickerFilter && [_pickerFilter intValue] == -2;
    [_picker setAutoVisible:[_servers count] > 0 && ![_pickerQuery length] && !awgOnly
                     picked:autoOn
                   subtitle:subtitle];
}

- (void)reloadPickerList {
    [self rebuildSections];
    [_table reloadData];
    [self syncEmptyState];
}

- (void)serverPickerClose {
    [self hideServerPicker];
}

- (void)serverPickerAdd {
    [self addPressed];
}

- (void)serverPickerManageSubscriptions {
    if (_subscriptionMutationBusy) return;
    if (![_subs count]) {
        [self addPressed];
        return;
    }
    if ([_subs count] == 1 || ([_pickerFilter intValue] >= 0 && _pickerFilter)) {
        int index = [_subs count] == 1 ? ((SenkoSub *)[_subs objectAtIndex:0])->index
                                       : [_pickerFilter intValue];
        UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
        button.tag = 2000 + index;
        [self subMenuTapped:button];
        return;
    }
    [self dismissCurrentActionSheetAnimated:NO];
    NSMutableArray *choices = [NSMutableArray arrayWithCapacity:[_subs count]];
    UIActionSheet *sheet = [[UIActionSheet alloc]
        initWithTitle:SenkoLocalizedText(@"Subscription") delegate:self
        cancelButtonTitle:SenkoLocalizedText(@"Cancel") destructiveButtonTitle:nil
        otherButtonTitles:nil];
    for (SenkoSub *sub in _subs) {
        [choices addObject:[NSNumber numberWithInt:sub->index]];
        [sheet addButtonWithTitle:[sub->name length] ? sub->name :
                                  SenkoLocalizedText(@"Subscription")];
    }
    [_menuSubChoices release];
    _menuSubChoices = [choices copy];
    sheet.tag = 44;
    _actionSheet = sheet;
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad &&
        [sheet respondsToSelector:@selector(showFromRect:inView:animated:)])
        [sheet showFromRect:_picker->subscriptionButton.bounds
                    inView:_picker->subscriptionButton animated:YES];
    else
        [sheet showInView:self.view];
}

- (void)serverPickerSort {
    [self showSortMenu];
}

- (void)serverPickerPing {
    [self pingPressed];
}

- (void)serverPickerChooseAuto {
    if ([self isTunnelActive] && _activeBackend == SenkoBackendAmneziaWG) {
        SetStatusDefault(_statusLabel, @"disconnect to switch backend");
        return;
    }
    if (_busy) {
        SetStatusDefault(_statusLabel, @"disconnect to switch");
        return;
    }
    SenkoSetAutoServerEnabled(YES);
    _selectedBackend = SenkoBackendServer;
    [[NSUserDefaults standardUserDefaults] setInteger:_selectedBackend
                                               forKey:SENKO_SELECTED_BACKEND_KEY];
    [[NSUserDefaults standardUserDefaults] synchronize];
    [self syncPickerChrome];
    NSArray *vis = [_table indexPathsForVisibleRows];
    if ([vis count])
        [_table reloadRowsAtIndexPaths:vis withRowAnimation:UITableViewRowAnimationNone];
    [self applyState];
    [self hideServerPicker];
}

- (void)serverPickerChooseChip:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)[_chipKeys count]) return;
    id key = [_chipKeys objectAtIndex:index];
    [_pickerFilter release];
    _pickerFilter = [key isKindOfClass:[NSNumber class]] ? [key retain] : nil;
    if (_table.editing) [_table setEditing:NO animated:NO];
    [self reloadPickerList];
    [_table setContentOffset:CGPointZero animated:NO];
}

- (void)serverPickerQueryChanged:(NSString *)query {
    NSString *clean = [query length] ? query : nil;
    if ((!clean && !_pickerQuery) || [clean isEqualToString:_pickerQuery]) return;
    [_pickerQuery release];
    _pickerQuery = [clean copy];
    [self reloadPickerList];
}

/* sorting belongs to the list it sorts, and reordering the manual group is the
   same kind of choice, so both live in one menu there */
- (void)showSortMenu {
    [self dismissCurrentActionSheetAnimated:NO];
    SenkoServerSort mode = (SenkoServerSort)
        [[NSUserDefaults standardUserDefaults] integerForKey:SENKO_SERVER_SORT_KEY];
    UIActionSheet *as = [[UIActionSheet alloc] initWithTitle:SenkoLocalizedText(@"Sort servers")
                                                    delegate:self
                                           cancelButtonTitle:nil
                                      destructiveButtonTitle:nil
                                           otherButtonTitles:nil];
    NSString *titles[3] = { SenkoLocalizedText(@"Stored order"),
                            SenkoLocalizedText(@"By name"),
                            SenkoLocalizedText(@"By latency") };
    for (int i = 0; i < 3; ++i)
        [as addButtonWithTitle:(int)mode == i
            ? [NSString stringWithFormat:@"\u2713 %@", titles[i]] : titles[i]];
    if (mode == SenkoSortManual && [self hasManualServers] && ![self isListMutationLocked])
        [as addButtonWithTitle:_table.editing ? SenkoLocalizedText(@"Done reordering")
                                              : SenkoLocalizedText(@"Reorder manual servers")];
    as.cancelButtonIndex = [as addButtonWithTitle:SenkoLocalizedText(@"Cancel")];
    as.tag = 43;
    _actionSheet = as;
    [as showInView:self.view];
}

- (void)sortMenuPicked:(NSInteger)index {
    if (index >= 0 && index <= 2) {
        NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
        [d setInteger:index forKey:SENKO_SERVER_SORT_KEY];
        [d synchronize];
        if (_table.editing) [_table setEditing:NO animated:NO];
        [self reloadPickerList];
        return;
    }
    if (index == 3) {
        /* only the manual group moves, so the view narrows to it */
        if (!_table.editing && (!_pickerFilter || [_pickerFilter intValue] != -1)) {
            [_pickerFilter release];
            _pickerFilter = [[NSNumber numberWithInt:-1] retain];
            [self reloadPickerList];
        }
        [_table setEditing:!_table.editing animated:YES];
    }
}

/* the list is empty when nothing at all is configured: not a single manual
   server, no subscription server, and no saved amneziawg profile */
/* the panel starts below whatever the list has actually drawn: the spacer header
   that holds the open status card off the first row, plus any section header a
   subscription with no servers still draws. the list refresh and the layout pass
   both place it, and the layout pass used to win by resetting it to the plain
   table frame, which is what put the card over the first line */
- (CGRect)emptyStateFrame {
    CGRect area = _table.frame;
    /* the open status card covers the table header spacer. empty content must
       start below it too, otherwise its title sits under the card. short
       landscape screens keep a small visible panel instead of losing it. */
    if ([_servers count] == 0 && ![self hasAWGProfile]) {
        UIView *spacer = _table.tableHeaderView;
        CGFloat spacerH = spacer ? spacer.bounds.size.height : 0.0f;
        CGFloat used = spacerH;
        /* an empty subscription still draws its header. start below it instead
           of covering the controls that explain why its server list is empty */
        if ([_subs count] > 0) {
            CGFloat content = _table.contentSize.height - _table.contentOffset.y;
            if (content > used) used = content;
        }
        if (used < 0.0f) used = 0.0f;
        if (used > area.size.height - 72.0f)
            used = MAX(0.0f, area.size.height - 72.0f);
        area.origin.y += used;
        area.size.height -= used;
        return area;
    }
    CGFloat used = _table.contentSize.height - _table.contentOffset.y;
    UIView *spacer = _table.tableHeaderView;
/* contentSize is not written until the table lays itself out, and the spacer is
   set synchronously by the layout pass, so the header is the reliable floor */
    CGFloat spacerH = spacer ? spacer.bounds.size.height : 0.0f;
    if (used < spacerH) used = spacerH;
    if (used < 0.0f) used = 0.0f;
    if (used > 0.0f) {
        CGFloat room = area.size.height - used;
        /* under a list that already fills the screen there is nowhere to put
           the panel that would not cover it */
        if (room < 180.0f) return CGRectZero;
        area.origin.y += used;
        area.size.height = room;
    }
    return area;
}

- (void)syncEmptyState {
    if (!_emptyState) return;
    BOOL empty = ([_servers count] == 0) && ![self hasAWGProfile];
    if (!empty) {
        if (!_emptyState.hidden) _emptyState.hidden = YES;
        return;
    }

    /* layout can run with no room while the table header is settling. load the
       id before that early return, or the panel can stay empty until relaunch */
    [_emptyState setHWID:_deviceHWID];
    if (![_deviceHWID length]) {
        NSString *shared = SenkoSharedDeviceHWID();
        if ([shared length]) {
            [_deviceHWID release];
            _deviceHWID = [shared copy];
            [_emptyState setHWID:_deviceHWID];
        }
    }

    CGRect area = [self emptyStateFrame];
    BOOL hide = CGRectIsEmpty(area);
    if (_emptyState.hidden != hide) _emptyState.hidden = hide;
    if (hide)
        return;
    if (!CGRectEqualToRect(_emptyState.frame, area)) _emptyState.frame = area;
}

/* the shared file is written the first time the daemon is asked for the id, so
   before that the socket is the only source. a busy daemon misses the reply
   window, and treating that silence as an answer is what left the plate
   reading "not available yet" for the rest of the session */
- (void)requestDeviceHWID {
    if ([_deviceHWID length]) return;
/* the file is the cheap path and it needs no daemon at all */
    NSString *shared = SenkoSharedDeviceHWID();
    if ([shared length]) {
        [_deviceHWID release];
        _deviceHWID = [shared copy];
        [_emptyState setHWID:_deviceHWID];
        return;
    }
    if (_hwidRetries >= 12) return;
    _hwidRetries++;
/* the next attempt is armed here rather than inside the reply block: the block
   does not run at all when there is no control object yet, and arming it from
   in there made the whole chain depend on the first attempt reaching a daemon */
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(requestDeviceHWID)
                                               object:nil];
    [self performSelector:@selector(requestDeviceHWID)
               withObject:nil
               afterDelay:3.0];
    [_ctl deviceHWID:^(NSString *hwid) {
        NSString *value = [hwid length] ? hwid : SenkoSharedDeviceHWID();
        if (![value length]) return;
        [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                 selector:@selector(requestDeviceHWID)
                                                   object:nil];
        [_deviceHWID release];
        _deviceHWID = [value copy];
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
/* launchd restarts a dead daemon; wait for it before showing an error */
        if (!servers) {
            [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
                if (generation != _catalogGeneration) return;
                if (!up) {
                    _catalogLoaded = YES;
                    [self hideBusyOverlay];
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
                        _catalogLoaded = YES;
                        [self hideBusyOverlay];
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
    [self refreshTunnelState];
}


- (void)refreshTunnelState {
    [self refreshTunnelStateRedrawing:YES];
}

/* the heartbeat runs whether or not anything moved, and -applyState repaints the
   whole background stack. redrawing that every five seconds for a state nobody
   changed is work a 4s does not have to spare, so a quiet poll only repaints
   when the daemon reports something other than what is already on screen */
- (void)refreshTunnelStateRedrawing:(BOOL)always {
    NSInteger generation = _catalogGeneration;
    NSUInteger stateGeneration = ++_tunnelStateGeneration;
    __block NSString *vlessState = nil;
    __block NSString *awgState = nil;
    __block long vlessUptime = 0;
    __block NSInteger pending = 2;
    void (^applyBackendState)(void) = ^{
        NSString *stateBefore;
        SenkoBackendKind backendBefore;
        if (generation != _catalogGeneration ||
            stateGeneration != _tunnelStateGeneration) {
            [vlessState release];
            [awgState release];
            vlessState = nil;
            awgState = nil;
            return;
        }
        pending--;
        if (pending != 0) return;

        stateBefore = [[_state copy] autorelease];
        backendBefore = _activeBackend;

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
            if (backendBefore != SenkoBackendAmneziaWG)
                _tunnelUptimeKnown = NO;
            _activeBackend = SenkoBackendAmneziaWG;
            _selectedBackend = SenkoBackendAmneziaWG;
            [[NSUserDefaults standardUserDefaults] setInteger:_selectedBackend
                                                       forKey:SENKO_SELECTED_BACKEND_KEY];
            [_state release];
            _state = [awg copy];
            [self setLastErr:nil];
        } else if (vlessUp) {
            if ([vlessState isEqualToString:@"connected"]) {
                _tunnelUptime = vlessUptime;
                _tunnelUptimeAt = CACurrentMediaTime();
                _tunnelUptimeKnown = YES;
            } else {
                _tunnelUptimeKnown = NO;
            }
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
        if (always || backendBefore != _activeBackend ||
            ![stateBefore isEqualToString:_state] ||
            (awgUp && [awg isEqualToString:@"connected"] && !_tunnelUptimeKnown))
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
    void (^statusReply)(NSString *, long) = ^(NSString *state, long uptime) {
        if (generation != _catalogGeneration ||
            stateGeneration != _tunnelStateGeneration) return;
        /* STATUS is for the server backend; its idle reply says nothing about
           the amneziawg clock until both backend replies have been resolved */
        vlessUptime = uptime;
        vlessState = [state copy];
        applyBackendState();
    };
    if ([SenkoNativeVPN available])
        [self nativeStatusWithReply:statusReply];
    else
        [_ctl statusStateWithUptime:statusReply];
    [_ctl awgStatus:^(NSString *status) {
        if (generation != _catalogGeneration ||
            stateGeneration != _tunnelStateGeneration) return;
        awgState = [status copy];
        applyBackendState();
    }];
}

/* the daemon is the only thing that knows whether a tunnel is up, and it is
   never asked again once a screenful of state has been drawn. a tunnel that
   came up, dropped, or was replaced outside this screen therefore stayed
   invisible until the app was launched again, which is exactly what a tester
   sees as "senko says disconnected while the vpn badge is lit". five seconds is
   one control round trip on a 4s and keeps the card honest without polling the
   catalog, which is the expensive half of -refresh */
- (void)statusHeartbeat {
/* a connect or disconnect already owns the state machine, and a poll landing in
   the middle of one would report the state it is leaving */
    if (_busy) return;
    [self refreshTunnelStateRedrawing:NO];
}

- (void)startStatusHeartbeat {
    if (_statusTimer) return;
    _statusTimer = [NSTimer scheduledTimerWithTimeInterval:5.0
                                                    target:self
                                                  selector:@selector(statusHeartbeat)
                                                  userInfo:nil
                                                   repeats:YES];
}

- (void)stopStatusHeartbeat {
    [_statusTimer invalidate];
    _statusTimer = nil;
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
    /* ios 9+ draws UIAlertView through a compatibility shim, and the error
       alert can arrive just after an import dismisses its own prompt */
    Class controllerCls = NSClassFromString(@"UIAlertController");
    Class actionCls = NSClassFromString(@"UIAlertAction");
    SEL makeAlert = @selector(alertControllerWithTitle:message:preferredStyle:);
    SEL makeAction = @selector(actionWithTitle:style:handler:);
    if (controllerCls && actionCls &&
        [controllerCls respondsToSelector:makeAlert] &&
        [actionCls respondsToSelector:makeAction]) {
        id alert = ((id (*)(id, SEL, id, id, NSInteger))objc_msgSend)(
            controllerCls, makeAlert, SenkoLocalizedText(@"Connection failed"),
            shown, 1 /* UIAlertControllerStyleAlert */);
        id ok = ((id (*)(id, SEL, id, NSInteger, id))objc_msgSend)(
            actionCls, makeAction, SenkoLocalizedText(@"OK"),
            0 /* UIAlertActionStyleDefault */, nil);
        if (alert && ok) {
            ((void (*)(id, SEL, id))objc_msgSend)(alert, @selector(addAction:), ok);
            UIViewController *presenter = self;
            while (presenter.presentedViewController)
                presenter = presenter.presentedViewController;
            [presenter presentViewController:alert animated:YES completion:nil];
            return;
        }
    }
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

- (void)sectionToggleTapped:(UIButton *)button {
    int subIdx = (int)button.tag - 4000;
    if (subIdx < 0) return;
    NSNumber *key = [NSNumber numberWithInt:subIdx];
    BOOL collapse = ![_collapsedSubs containsObject:key];
    if (collapse)
        [_collapsedSubs addObject:key];
    else
        [_collapsedSubs removeObject:key];
    button.enabled = NO;

/* finish the button action before mutating its table section. ios 5 continues
   to touch the sender after this method returns, and removing its header from
   inside the event dispatch used to leave a stale plate on screen. */
    dispatch_async(dispatch_get_main_queue(), ^{
        NSInteger section = NSNotFound;
        for (NSInteger i = 0; i < (NSInteger)[_sections count]; ++i) {
            if ([[[_sections objectAtIndex:i] objectForKey:@"subIdx"] intValue] == subIdx) {
                section = i;
                break;
            }
        }
        if (section == NSNotFound) return;
        NSArray *rows = [[_sections objectAtIndex:section] objectForKey:@"rows"];
        NSMutableArray *paths = [NSMutableArray arrayWithCapacity:[rows count]];
        for (NSInteger row = 0; row < (NSInteger)[rows count]; ++row)
            [paths addObject:[NSIndexPath indexPathForRow:row inSection:section]];

        SenkoSectionHeader *header = (SenkoSectionHeader *)
            [[button superview] superview];
        NSString *title = [[_sections objectAtIndex:section] objectForKey:@"title"];
        if (header && header->title) {
            header->title.text = [NSString stringWithFormat:@"%@ %@",
                                  collapse ? @"\u25B8" : @"\u25BE", title];
            header->title.alpha = 0.32f;
            header->title.transform = CGAffineTransformMakeTranslation(0.0f,
                collapse ? -3.0f : 3.0f);
            [UIView animateWithDuration:0.18
                                  delay:0.0
                                options:UIViewAnimationOptionBeginFromCurrentState |
                                        UIViewAnimationOptionCurveEaseOut
                             animations:^{
                header->title.alpha = 1.0f;
                header->title.transform = CGAffineTransformIdentity;
            } completion:NULL];
        }
        if (![paths count]) {
            button.enabled = YES;
            return;
        }
        [_table beginUpdates];
        if (collapse)
            [_table deleteRowsAtIndexPaths:paths
                           withRowAnimation:UITableViewRowAnimationFade];
        else
            [_table insertRowsAtIndexPaths:paths
                           withRowAnimation:UITableViewRowAnimationFade];
        [_table endUpdates];
        button.enabled = YES;
    });
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

        UIView *snapshot = SenkoSectionDragProxy(gesture.view);
        if (!snapshot) return;
        CGRect frame = [_table convertRect:gesture.view.bounds fromView:gesture.view];
        snapshot.frame = frame;
        snapshot.alpha = 0.92f;
        snapshot.layer.cornerRadius = SenkoThemeCardRadius();
        snapshot.layer.masksToBounds = YES;
        [_table addSubview:snapshot];
        _sectionDragSnapshot = [snapshot retain];
/* a status refresh reloads the table every couple of seconds under a live
   tunnel, and that frees every header. the drag holds this one itself so the
   pointer cannot outlive the view it names */
        _sectionDragHeader = [gesture.view retain];
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
/* a status refresh can empty the catalog while a header is under the finger,
   and -rectForHeaderInSection: raises on a section the table no longer has */
    NSInteger sectionCount = [_table numberOfSections];
    if (finalSection >= sectionCount) finalSection = sectionCount - 1;
    if (finalSection < 0) finalSection = 0;
    if (sourceSection >= sectionCount) sourceSection = finalSection;
    BOOL moved = sectionCount > 0 && finalSection != sourceSection;
    UIView *sourceHeader = [_sectionDragHeader retain];
    _table.scrollEnabled = YES;

    UIView *snapshot = [_sectionDragSnapshot retain];
    [_sectionDragSnapshot release];
    _sectionDragSnapshot = nil;
    CGRect target = sectionCount > 0
        ? [_table rectForHeaderInSection:finalSection]
        : snapshot.frame;
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
                         [_sectionDragHeader release];
                         _sectionDragHeader = nil;
                         if (moved) {
                             [self moveSectionAtIndex:sourceSection toIndex:finalSection];
                         } else {
                             sourceHeader.alpha = 1.0f;
                         }
                         [sourceHeader release];
                     }];
}

/* a tap picks a row, so the card with the link, the check and the edit and
   delete actions sits behind a hold */
- (void)rowLongPressed:(UILongPressGestureRecognizer *)gesture {
    if (gesture.state != UIGestureRecognizerStateBegan || _table.editing) return;
    NSIndexPath *ip = [_table indexPathForRowAtPoint:
                       [gesture locationInView:_table]];
    if (!ip) return;
    if ([self isAWGRowAtIndexPath:ip]) {
        [self showManualMenu];
        return;
    }
    SenkoServer *sv = [self serverAtIndexPath:ip];
    if (sv) [self openSheetForServer:sv];
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
    return (_awgRowShown && [self isManualSection:section]) ? 1 : 0;
}

- (BOOL)isAWGRowAtIndexPath:(NSIndexPath *)ip {
    return [self awgRowOffsetInSection:ip.section] > 0 && ip.row == 0;
}

- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s {
    (void)tv;
    if (!_sections || s < 0 || s >= (NSInteger)[_sections count]) return 0;
    NSDictionary *sec = [_sections objectAtIndex:s];
    int subIdx = [[sec objectForKey:@"subIdx"] intValue];
/* a search looks through collapsed groups too, or a match could hide behind
   a heading the user folded long ago */
    if (subIdx >= 0 && ![_pickerQuery length] &&
        [_collapsedSubs containsObject:[NSNumber numberWithInt:subIdx]])
        return 0;
    return [self awgRowOffsetInSection:s] + (NSInteger)[[sec objectForKey:@"rows"] count];
}

- (CGFloat)tableView:(UITableView *)tv heightForRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv; (void)ip;
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad)
        return 72.0f;
    return 64.0f;
}

/* a pinned heading sits below its own slot. uikit may wrap it in a container,
   so look one level down */
- (void)syncPinnedHeaders {
    _pinnedSyncQueued = NO;
    if (!_table) return;
    NSMutableArray *headers = [NSMutableArray array];
    for (UIView *v in _table.subviews) {
        if ([v isKindOfClass:[SenkoSectionHeader class]]) {
            [headers addObject:v];
            continue;
        }
        for (UIView *inner in v.subviews)
            if ([inner isKindOfClass:[SenkoSectionHeader class]]) [headers addObject:inner];
    }
    NSInteger count = [self numberOfSectionsInTableView:_table];
    for (SenkoSectionHeader *h in headers) {
        NSInteger s = h.tag - 7000;
        if (s < 0 || s >= count || h.hidden) {
            [h setPinned:NO];
            continue;
        }
        CGFloat y = [h convertRect:h.bounds toView:_table].origin.y;
        [h setPinned:y > [_table rectForHeaderInSection:s].origin.y + 0.5f];
    }
}

- (void)scrollViewDidScroll:(UIScrollView *)scroll {
    if (scroll == _table) [self syncPinnedHeaders];
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    return 56.0f;
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    if (!_sections || s < 0 || s >= (NSInteger)[_sections count]) return nil;
    BOOL pad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
    CGFloat scrH = [UIScreen mainScreen].bounds.size.height;
    CGFloat scrW = [UIScreen mainScreen].bounds.size.width;
    if (scrW > scrH) { CGFloat t = scrH; scrH = scrW; scrW = t; } /* use the short side */
    BOOL compact = (!pad && scrH <= 568.0f);
    CGFloat hh = 56.0f;
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
    BOOL manualHasAwg = (subIdx < 0 && _awgRowShown);
    NSUInteger shown = n + (manualHasAwg ? 1 : 0);
    BOOL collapsed = subIdx >= 0 &&
        [_collapsedSubs containsObject:[NSNumber numberWithInt:subIdx]];
    if (subIdx >= 0)
        title = [NSString stringWithFormat:@"%@ %@", collapsed ? @"\u25B8" : @"\u25BE", title];
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
    [wrap addSubview:plate];
    wrap->plate = plate;

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
    lab.font = SenkoFontBody(15.0f, YES);
    lab.textColor = kInk;
    lab.shadowColor = nil;
    lab.shadowOffset = CGSizeZero;
    lab.text = title;
    lab.numberOfLines = 1;
    lab.lineBreakMode = NSLineBreakByTruncatingTail;
    [plate addSubview:lab];
    wrap->title = lab;

    UILabel *meta = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    meta.backgroundColor = [UIColor clearColor];
    meta.font = SenkoFontBody(12.0f, NO);
    meta.textColor = kInkMuted;
    meta.shadowColor = nil;
    meta.shadowOffset = CGSizeZero;
    meta.text = metaText;
    meta.numberOfLines = 1;
    meta.lineBreakMode = NSLineBreakByTruncatingTail;
    [plate addSubview:meta];
    wrap->meta = meta;

    /* refresh and ping belong to a subscription or to the amneziawg profile */
    BOOL showActions = (subIdx >= 0) || manualHasAwg;
    /* the manual group keeps its own menu for the delete-all action */
    BOOL showMore = showActions || (subIdx < 0 && n > 0);
    BOOL subPingBusy = subIdx >= 0 &&
        [_pingingSubs containsObject:[NSNumber numberWithInt:subIdx]];

    if (showMore) {
        UIColor *iconTint = kInkMuted;
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
            SenkoSetGaugeSpinning(ping, subPingBusy, iconTint);
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
/* a reload while scrolled builds the pinned heading clear; it has no position
   until this returns */
    if (!_pinnedSyncQueued) {
        _pinnedSyncQueued = YES;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self syncPinnedHeaders];
        });
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
                          detail:@"awg / udp"
                          picked:picked
                          status:st];
        [cell setPingTarget:nil action:NULL serverIndex:-1];
        [cell setGroupFirst:YES
                       last:[self tableView:tv numberOfRowsInSection:ip.section] == 1];
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
    NSNumber *msVal = sv ? [self bestPingForServer:sv] : nil;
    NSString *shown = sv ? [_rowName objectForKey:
        [NSNumber numberWithInt:sv->index]] : nil;
    [cell configureWithServer:sv picked:picked pingVal:msVal
                  displayName:shown];
    [cell setPingTarget:self action:@selector(serverPingTapped:)
            serverIndex:sv ? sv->index : -1];
    [cell setGroupFirst:ip.row == 0
                   last:ip.row == [self tableView:tv numberOfRowsInSection:ip.section] - 1];
    return cell;
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
        [self syncPickerChrome];
        [self applyState];
        [self hideServerPicker];
        return;
    }
    SenkoServer *sv = [self serverAtIndexPath:ip];
    [tv deselectRowAtIndexPath:ip animated:YES];
    if (!sv) return;
    [self chooseServerIndex:sv->index];
}

/* picking a row is a choice of server, so quick connect steps aside. a live
   tunnel moves to the new server the same way the detail sheet moves it */
- (void)chooseServerIndex:(int)index {
    if ([self isTunnelActive] && _activeBackend == SenkoBackendAmneziaWG) {
        SetStatusDefault(_statusLabel, @"disconnect to switch backend");
        return;
    }
    if ([self isServerSelectionLocked] && _selectedSrvIdx != index) {
        SetStatusDefault(_statusLabel, @"disconnect to switch");
        return;
    }
    SenkoSetAutoServerEnabled(NO);
    [self selectServerIndex:index];
    [self syncPickerChrome];
    [self hideServerPicker];
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

/* the order a collapsed row's members get tried in when the representative
   itself cannot open a tunnel: best measured ping first, unmeasured after,
   ties keep catalog order. just the index itself when it stands for no one. */
- (NSArray *)connectCandidatesForServerIndex:(int)index {
    SenkoServer *sv = [self serverByIndex:index];
    NSArray *idxs = (sv && [sv->dupIndexes count]) ? sv->dupIndexes
        : [NSArray arrayWithObject:[NSNumber numberWithInt:index]];
    return [idxs sortedArrayUsingComparator:^NSComparisonResult(NSNumber *a, NSNumber *b) {
        NSNumber *pa = [_serverStatus objectForKey:a];
        NSNumber *pb = [_serverStatus objectForKey:b];
        int ra = SenkoSortRank(pa), rb = SenkoSortRank(pb);
        if (ra != rb) return ra < rb ? NSOrderedAscending : NSOrderedDescending;
        if (ra == 0 && [pa intValue] != [pb intValue])
            return [pa intValue] < [pb intValue] ? NSOrderedAscending : NSOrderedDescending;
        return NSOrderedSame;
    }];
}

- (void)openSheetForServer:(SenkoServer *)server {
    if (!server) return;
    [_sheet dismiss];
    [_sheet release];
    _sheet = nil;

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
                        ping:[self bestPingForServer:server]
                      source:source
                      active:active
                   canMutate:![self isListMutationLocked] && server->group < 0
                    delegate:self];
    [_sheet presentInView:self.view];

    int idx = server->index;
/* the reply outlives a sheet the user closed and reopened on another server,
   and the pending link would then be written into the wrong card */
    SenkoServerSheet *asking = _sheet;
    [_ctl serverLinkIndex:idx reply:^(NSString *link) {
        if (_sheet != asking) return;
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
        SenkoSetAutoServerEnabled(NO);
        if (!wasActive) [self selectServerIndex:index];
        [self syncPickerChrome];
        [self hideServerPicker];
        if (wasActive || ![self isTunnelActive])
            [self togglePressed];
        return;
    }
    if ([action isEqualToString:SenkoServerSheetActionPing]) {
        NSInteger generation = ++_checkGeneration;
        NSNumber *key = [NSNumber numberWithInt:index];
        NSString *mode = @"tcp";
        [_serverStatus setObject:[NSNumber numberWithInt:-3] forKey:key];
        [self reloadServerRowForIndex:index];
        [_ctl checkIndex:index mode:mode reply:^(int ms, NSString *error) {
            if (generation != _checkGeneration) return;
            if (ms < 0 && [error length]) [self setLastErr:error];
            [_serverStatus setObject:[NSNumber numberWithInt:ms] forKey:key];
            if (_sheet == sheet)
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
            SenkoPresentModal(self, nav);
        }];
        return;
    }
    if ([action isEqualToString:SenkoServerSheetActionDelete]) {
        if ([self isListMutationLocked]) return;
        [_ctl deleteServerIndex:index reply:^(NSString *reply) {
            if (!reply || [reply hasPrefix:@"ERR"]) {
                SetStatusDefault(_statusLabel, SenkoHumanReadableError(
                    reply ? reply : @"daemon offline: cannot remove server"));
                return;
            }
            if (index == _selectedSrvIdx) _selectedSrvIdx = -1;
            [self refresh];
        }];
    }
}

- (void)editServerVC:(EditServerVC *)vc saveLink:(NSString *)link index:(int)idx {
    [_ctl replaceServerIndex:idx link:link reply:^(NSString *reply) {
        if (!reply || [reply hasPrefix:@"ERR"]) {
            SetStatusDefault(_statusLabel, SenkoHumanReadableError(
                reply ? reply : @"daemon offline: cannot update server"));
            return;
        }
        SenkoDismissModal(vc, nil);
        SetStatusRefresh(_statusLabel, @"server updated");
        [self refresh];
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
    [_ctl deleteServerIndex:targetIdx reply:^(NSString *reply) {
        if (!reply || [reply hasPrefix:@"ERR"]) {
            SetStatusDefault(_statusLabel, SenkoHumanReadableError(
                reply ? reply : @"daemon offline: cannot remove server"));
            return;
        }
        if (targetIdx == _selectedSrvIdx) _selectedSrvIdx = -1;
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
    if (subIdx < 0 || _subscriptionMutationBusy ||
        ![self subscriptionByIndex:subIdx]) return;
    btn.enabled = NO;
    btn.alpha = 0.55f;
    SenkoSetButtonSpinning(btn, kSenkoSubscriptionRefreshKey, YES);
    SetStatusRefresh(_statusLabel, @"refreshing subscription...");
    [_ctl refreshSubIndex:subIdx reply:^(NSString *reply) {
        SenkoSetButtonSpinning(btn, kSenkoSubscriptionRefreshKey, NO);
        btn.enabled = YES;
        btn.alpha = 1.0f;
        if (!reply || [reply hasPrefix:@"ERR"]) {
            [self setLastErr:reply ? [reply stringByTrimmingCharactersInSet:
                  [NSCharacterSet whitespaceAndNewlineCharacterSet]]
                                     : @"daemon offline: cannot refresh subscription"];
            [self applyState];
        } else {
            SetStatusRefresh(_statusLabel, @"subscription updated");
        }
        [self refresh];
    }];
}

- (void)subPingTapped:(UIButton *)btn {
    int subIdx = (int)btn.tag - 3000;
    if (subIdx < 0 || _subscriptionMutationBusy ||
        ![self subscriptionByIndex:subIdx]) return;
    [self pingServersInSub:subIdx];
}

- (void)subMenuTapped:(UIButton *)btn {
    int subIdx = (int)btn.tag - 2000;
    if (subIdx < 0 || _subscriptionMutationBusy ||
        ![self subscriptionByIndex:subIdx]) return;
    [self dismissCurrentActionSheetAnimated:NO];
    _menuSubIdx = subIdx;
    SenkoSub *entry = [self subscriptionByIndex:subIdx];
    UIActionSheet *as = [[UIActionSheet alloc]
                         initWithTitle:[entry->name length] ? entry->name : SenkoLocalizedText(@"Subscription")
                         delegate:self
                         cancelButtonTitle:SenkoLocalizedText(@"Cancel")
                         destructiveButtonTitle:SenkoLocalizedText(@"Remove")
                         otherButtonTitles:SenkoLocalizedText(@"Refresh now"),
                                           SenkoLocalizedText(@"Check ping"),
                                           SenkoLocalizedText(@"Subscription details"),
                                           SenkoLocalizedText(@"Edit details"), nil];
    as.tag = 400000 + subIdx;
    _actionSheet = as;
    UIView *anchor = btn.superview ? btn : _picker->subscriptionButton;
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad &&
        [as respondsToSelector:@selector(showFromRect:inView:animated:)])
        [as showFromRect:anchor.bounds inView:anchor animated:YES];
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
        if (!reply || [reply hasPrefix:@"ERR"]) {
            SetStatusRefresh(_statusLabel, @"amneziawg timeout");
            return;
        }
        NSRange mark = [reply rangeOfString:@"PING "];
        if (mark.location == NSNotFound) {
            SetStatusRefresh(_statusLabel, @"amneziawg timeout");
            return;
        }
        NSInteger ms = [[reply substringFromIndex:mark.location + mark.length] integerValue];
        if (ms < 0) {
            SetStatusRefresh(_statusLabel, @"amneziawg timeout");
            return;
        }
        [_serverStatus setObject:[NSNumber numberWithInteger:ms]
                          forKey:[NSNumber numberWithInt:-1]];
        SetStatusRefresh(_statusLabel, [NSString stringWithFormat:@"%ld ms", (long)ms]);
        NSArray *vis = [_table indexPathsForVisibleRows];
        if ([vis count])
            [_table reloadRowsAtIndexPaths:vis withRowAnimation:UITableViewRowAnimationNone];
    }];
}

/* a subscription refresh or a full ping sweep can take several seconds one
   request at a time; without this the list just sits there and the only sign
   of life is the status line text changing underneath it */
- (void)showBusyOverlay:(NSString *)text {
    UIActivityIndicatorView *spinner;
    UILabel *label;
    CGRect b = self.view.bounds;
    if (!_busyOverlay) {
        _busyOverlay = [[UIView alloc] initWithFrame:b];
        _busyOverlay.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                        UIViewAutoresizingFlexibleHeight;
        _busyOverlay.backgroundColor = [UIColor colorWithWhite:0.0f alpha:0.32f];
        _busyOverlay.userInteractionEnabled = YES; /* eat taps while busy */
        _busyOverlay.alpha = 0.0f;

        spinner = [[[UIActivityIndicatorView alloc]
            initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhiteLarge] autorelease];
        spinner.tag = 88601;
        spinner.hidesWhenStopped = YES;
        [_busyOverlay addSubview:spinner];

        label = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
        label.tag = 88602;
        label.textColor = [UIColor whiteColor];
        label.font = [UIFont boldSystemFontOfSize:14.0f];
        label.textAlignment = NSTextAlignmentCenter;
        label.numberOfLines = 2;
        label.backgroundColor = [UIColor clearColor];
        [_busyOverlay addSubview:label];
    }
    spinner = (UIActivityIndicatorView *)[_busyOverlay viewWithTag:88601];
    label = (UILabel *)[_busyOverlay viewWithTag:88602];
    label.text = text;
    [self layoutBusyOverlay];

    if (![_busyOverlay superview]) [self.view addSubview:_busyOverlay];
    [self.view bringSubviewToFront:_busyOverlay];
    [spinner startAnimating];
    SenkoAnimate(0.18, ^{ _busyOverlay.alpha = 1.0f; }, NULL);
}

/* the overlay first shows from viewWillAppear, where an ipad on ios 5 and 6
   still reports portrait bounds in landscape; the resize that follows only
   stretches the dim view, so the spinner and text are placed again from
   layoutMainChrome */
- (void)layoutBusyOverlay {
    if (!_busyOverlay) return;
    CGRect b = self.view.bounds;
    UIView *spinner = [_busyOverlay viewWithTag:88601];
    UILabel *label = (UILabel *)[_busyOverlay viewWithTag:88602];
    _busyOverlay.frame = b;
    CGFloat cx = floorf(b.size.width * 0.5f);
    CGFloat cy = floorf(b.size.height * 0.5f);
    spinner.center = CGPointMake(cx, cy - 14.0f);
    CGFloat labelW = MIN(b.size.width - 48.0f, 260.0f);
    CGSize fit = [label sizeThatFits:CGSizeMake(labelW, CGFLOAT_MAX)];
    label.frame = CGRectMake(cx - floorf(labelW * 0.5f), cy + 20.0f, labelW, ceilf(fit.height));
}

- (void)hideBusyOverlay {
    if (!_busyOverlay || _busyOverlay.alpha == 0.0f) return;
    UIView *overlay = _busyOverlay;
    SenkoAnimate(0.18, ^{ overlay.alpha = 0.0f; }, ^(BOOL done) {
        if (done) [(UIActivityIndicatorView *)[overlay viewWithTag:88601] stopAnimating];
    });
}

/* one dead panel must not leave the subscriptions after it unrefreshed, so a
   failure is noted and the walk goes on; failures holds "name: reason" */
- (void)refreshSubscriptionIndex:(int)pos failures:(NSMutableArray *)failures {
    NSMutableArray *idxs = [NSMutableArray array];
    for (SenkoSub *s in _subs)
        [idxs addObject:[NSNumber numberWithInt:s->index]];
    if (pos >= (int)[idxs count]) {
        _isRefreshingCatalog = NO;
        [self hideBusyOverlay];
        [self refresh];
        if (![failures count]) {
            SetStatusRefresh(_statusLabel, @"subscriptions refreshed");
        } else if ([failures count] == 1) {
            SetStatusDefault(_statusLabel, [failures objectAtIndex:0]);
        } else {
            SetStatusDefault(_statusLabel, [NSString stringWithFormat:@"%@ (+%d)",
                [failures objectAtIndex:0], (int)[failures count] - 1]);
        }
        return;
    }
    SenkoSub *sub = [_subs objectAtIndex:pos];
    NSString *subName = [sub->name length] ? [[sub->name copy] autorelease] : @"?";
    [self showBusyOverlay:[NSString stringWithFormat:@"%@ %d/%d",
        SenkoLocalizedText(@"Refreshing subscriptions"), pos + 1, (int)[idxs count]]];
    [_ctl refreshSubIndex:sub->index reply:^(NSString *reply) {
        if (!reply) {
            /* no daemon answers the rest either */
            _isRefreshingCatalog = NO;
            [self hideBusyOverlay];
            SetStatusDefault(_statusLabel, SenkoHumanReadableError(
                @"daemon offline: cannot refresh subscription"));
            [self refresh];
            return;
        }
        if ([reply hasPrefix:@"ERR"])
            [failures addObject:[NSString stringWithFormat:@"%@: %@", subName,
                                 SenkoHumanReadableError(reply)]];
        [self refreshSubscriptionIndex:(pos + 1) failures:failures];
    }];
}

- (void)refreshPressed {
    if (_isRefreshingCatalog) return;
    if ([_subs count] == 0) {
        [self refresh];
        SetStatusRefresh(_statusLabel, @"list reloaded");
        return;
    }
    _isRefreshingCatalog = YES;
    SetStatusRefresh(_statusLabel, @"refreshing subscriptions...");
    [self refreshSubscriptionIndex:0 failures:[NSMutableArray array]];
}

- (void)pingPressed {
    if ([_servers count] == 0) {
        SetStatusDefault(_statusLabel, @"no servers to ping");
        return;
    }
    if (_activeBackend == SenkoBackendAmneziaWG && [self isTunnelActive]) {
        [self awgPingTapped:nil];
        return;
    }
    [self startPingSweep];
}

- (void)serverPingTapped:(UIButton *)button {
    int serverIndex = (int)button.tag;
    if (serverIndex < 0) return;
    NSInteger generation = ++_checkGeneration;
    NSNumber *key = [NSNumber numberWithInt:serverIndex];
    [_serverStatus setObject:[NSNumber numberWithInt:-3] forKey:key];
    [self reloadServerRowForIndex:serverIndex];
    NSString *mode = @"tcp";
    SetStatusDefault(_statusLabel, SenkoLocalizedText(@"Checking server"));
    [_ctl checkIndex:serverIndex mode:mode reply:^(int ms, NSString *error) {
        if (generation != _checkGeneration) return;
        [_serverStatus setObject:[NSNumber numberWithInt:ms] forKey:key];
        [self reloadServerRowForIndex:serverIndex];
        (void)error;
        SetStatusDefault(_statusLabel, ms >= 0
            ? [NSString stringWithFormat:@"TCP: %d ms", ms]
            : SenkoLocalizedText(@"Timeout"));
    }];
}

- (void)startPingSweep {
    _checkGeneration++;
    NSSet *busySubs = [_pingingSubs copy];
    [_pingingSubs removeAllObjects];
    [_serverStatus removeAllObjects];
    [self updateSubscriptionPingButtons:busySubs];
    [busySubs release];
    SetStatusDefault(_statusLabel, SenkoLocalizedText(@"Checking server"));
    NSMutableArray *indexes = [NSMutableArray array];
    NSMutableSet *seen = [NSMutableSet set];
/* the queue follows the rows the person sees. walking the backing catalog made
   a latency-sorted screen check locations in a seemingly random old order */
    for (NSDictionary *section in _sections) {
        for (SenkoServer *server in [section objectForKey:@"rows"]) {
            NSArray *members = [server->dupIndexes count]
                ? server->dupIndexes
                : [NSArray arrayWithObject:[NSNumber numberWithInt:server->index]];
            for (NSNumber *number in members) {
                if ([seen containsObject:number]) continue;
                [seen addObject:number];
                [indexes addObject:number];
            }
        }
    }
    if (![indexes count]) {
        for (SenkoServer *server in _servers)
            [indexes addObject:[NSNumber numberWithInt:server->index]];
    }
    if ([indexes count]) {
        SenkoSetGaugeSpinning(_picker->pingButton, YES, kInk);
        [self showBusyOverlay:SenkoLocalizedText(@"Checking server")];
    }
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
            SenkoSetGaugeSpinning(ping, busy, kInkMuted);
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
/* the manual group draws the saved amneziawg profile above its servers, so a
   server's table row is its position in the section plus that offset. without
   it a ping landed on the profile row and never on the server it measured */
        NSInteger offset = [self awgRowOffsetInSection:section];
        for (NSInteger i = 0; i < (NSInteger)[rows count]; ++i) {
            SenkoServer *server = [rows objectAtIndex:i];
            if (server->index != serverIndex &&
                ![server->dupIndexes containsObject:[NSNumber numberWithInt:serverIndex]])
                continue;
            NSInteger row = i + offset;
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
    SetStatusDefault(_statusLabel, SenkoLocalizedText(@"Checking server"));
    [self beginBoundedPing:idxs subIndex:subIdx generation:gen];
}

- (void)beginBoundedPing:(NSArray *)idxs subIndex:(int)subIdx generation:(NSInteger)gen {
    [_pingQueue release];
    _pingQueue = [idxs copy];
    _pingNext = 0;
    _pingPending = 0;
    _pingCompleted = 0;
    _pingSubIndex = subIdx;
    [self launchBoundedPingsForGeneration:gen];
}

- (void)launchBoundedPingsForGeneration:(NSInteger)gen {
    if (gen != _checkGeneration) return;
    /* every ping holds a daemon client slot for its whole probe */
    NSUInteger parallelLimit = 3;
    NSString *mode = @"tcp";
    while (_pingPending < parallelLimit && _pingNext < [_pingQueue count]) {
        int serverIndex = [[_pingQueue objectAtIndex:_pingNext++] intValue];
        _pingPending++;
        NSString *progress = [NSString stringWithFormat:@"%@ %lu/%lu",
            SenkoLocalizedText(@"Checking server"), (unsigned long)_pingNext,
            (unsigned long)[_pingQueue count]];
        SetStatusDefault(_statusLabel, progress);
        if (_pingSubIndex < 0) [self showBusyOverlay:progress];
/* the sweep cleared every result, so without this the rows sit blank for the
   whole run and nothing shows which server is being checked right now */
        [_serverStatus setObject:[NSNumber numberWithInt:-3]
                          forKey:[NSNumber numberWithInt:serverIndex]];
        [self reloadServerRowForIndex:serverIndex];
        [_ctl checkIndex:serverIndex mode:mode reply:^(int ms, NSString *error) {
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
    [self rebuildSections];
    [_table reloadData];
    if (_pingSubIndex >= 0) {
        NSNumber *key = [NSNumber numberWithInt:_pingSubIndex];
        [_pingingSubs removeObject:key];
        [self updateSubscriptionPingButtons:[NSSet setWithObject:key]];
        SetStatusDefault(_statusLabel, SenkoLocalizedText(@"Ping complete"));
    } else {
        SenkoSetGaugeSpinning(_picker->pingButton, NO, kInk);
        [self hideBusyOverlay];
        SetStatusDefault(_statusLabel, SenkoLocalizedText(@"Ping complete"));
    }
    [_pingQueue release];
    _pingQueue = nil;
}

- (void)startStatusChecks {
    NSSet *busySubs = [_pingingSubs copy];
    [_pingingSubs removeAllObjects];
    [self updateSubscriptionPingButtons:busySubs];
    [busySubs release];
    SenkoSetGaugeSpinning(_picker->pingButton, NO, kInk);
    _checkGeneration++;
}


@end
