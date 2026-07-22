#import "main_vc_priv.h"

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

static NSString *SenkoFlagCode(NSString *flag) {
    if ([flag length] != 4) return nil;
    unichar a = [flag characterAtIndex:1];
    unichar b = [flag characterAtIndex:3];
    if (!SenkoRegional(a) || !SenkoRegional(b)) return nil;
    return [NSString stringWithFormat:@"%c%c",
            (char)('a' + a - 0xDDE6),
            (char)('a' + b - 0xDDE6)];
}

static UIImage *SenkoFlagImage(NSString *flag) {
    NSString *code = SenkoFlagCode(flag);
    if (![code length]) return nil;
    return [UIImage imageNamed:[NSString stringWithFormat:@"flag-%@.png", code]];
}

static UIView *SenkoFlagBadge(NSString *flag, CGRect frame) {
    UIView *badge = [[[UIView alloc] initWithFrame:frame] autorelease];
    badge.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:1.0f alpha:0.92f]
        : [UIColor colorWithWhite:0.0f alpha:0.20f];
    badge.layer.cornerRadius = 7.0f;
    badge.layer.borderWidth = 0.8f;
    badge.layer.borderColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:1.0f alpha:0.95f].CGColor
        : [UIColor colorWithWhite:1.0f alpha:0.28f].CGColor;
    badge.layer.shadowColor = [UIColor blackColor].CGColor;
    badge.layer.shadowOpacity = 0.28f;
    badge.layer.shadowOffset = CGSizeMake(0.0f, 1.0f);
    badge.layer.shadowRadius = 1.5f;

    CAGradientLayer *gloss = [CAGradientLayer layer];
    gloss.frame = badge.bounds;
    gloss.cornerRadius = 6.0f;
    gloss.colors = [NSArray arrayWithObjects:
                    (id)[UIColor colorWithWhite:1.0f alpha:0.42f].CGColor,
                    (id)[UIColor colorWithWhite:1.0f alpha:0.0f].CGColor, nil];
    [badge.layer insertSublayer:gloss atIndex:0];

    UIImageView *imageView = [[[UIImageView alloc]
                               initWithFrame:CGRectMake(3.0f, 6.0f,
                                                        frame.size.width - 6.0f,
                                                        frame.size.height - 12.0f)] autorelease];
    imageView.backgroundColor = [UIColor colorWithWhite:1.0f alpha:0.88f];
    imageView.layer.cornerRadius = 3.0f;
    imageView.layer.masksToBounds = YES;
    imageView.layer.borderWidth = 0.5f;
    imageView.layer.borderColor = [UIColor colorWithWhite:0.0f alpha:0.20f].CGColor;
    imageView.contentMode = UIViewContentModeScaleAspectFill;
    imageView.image = SenkoFlagImage(flag);
    if (!imageView.image) {
        UILabel *fallback = [[[UILabel alloc] initWithFrame:imageView.bounds] autorelease];
        fallback.backgroundColor = [UIColor clearColor];
        fallback.textAlignment = NSTextAlignmentCenter;
        fallback.font = [UIFont systemFontOfSize:16.0f];
        fallback.text = flag;
        [imageView addSubview:fallback];
    }
    [badge addSubview:imageView];
    return badge;
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
        return @"expired";
    NSDateFormatter *fmt = [[[NSDateFormatter alloc] init] autorelease];
    fmt.locale = [[[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"] autorelease];
    fmt.dateFormat = @"'until' yyyy-MM-dd HH:mm";
    return [fmt stringFromDate:date];
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
    return a->port == b->port &&
           [a->proto isEqualToString:b->proto] &&
           [a->net isEqualToString:b->net] &&
           [a->security isEqualToString:b->security] &&
           [a->host isEqualToString:b->host];
}

@implementation MainVC (List)

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
    [_serverStatus removeAllObjects];
    [self rebuildSections];
    [self reconcileSelectionAfterListKeeping:oldSelected];
    [oldSelected release];
    [_table reloadData];
    [self styleListWell];
    [self layoutMainChrome];
}

- (void)rebuildSections {
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
                             manual, @"rows", nil]];
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
        NSString *meta = expiry
            ? [NSString stringWithFormat:@"%lu server%@  %@", (unsigned long)[rows count],
               [rows count] == 1 ? @"" : @"s", expiry]
            : [NSString stringWithFormat:@"%lu server%@", (unsigned long)[rows count],
               [rows count] == 1 ? @"" : @"s"];
        [secs addObject:[NSDictionary dictionaryWithObjectsAndKeys:
                         title, @"title", flag ? flag : @"", @"flag",
                         meta, @"meta", key, @"subIdx", rows, @"rows", nil]];
        [seen addObject:key];
    }

    if ([manual count] > 0 || [self hasAWGProfile]) {
        NSNumber *manualKey = [NSNumber numberWithInt:-1];
        if (![seen containsObject:manualKey])
            [secs insertObject:[NSDictionary dictionaryWithObjectsAndKeys:
                               @"Manual", @"title", manualKey, @"subIdx",
                               manual, @"rows", nil] atIndex:0];
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
        NSString *meta = expiry
            ? [NSString stringWithFormat:@"%lu server%@  %@", (unsigned long)[rows count],
               [rows count] == 1 ? @"" : @"s", expiry]
            : [NSString stringWithFormat:@"%lu server%@", (unsigned long)[rows count],
               [rows count] == 1 ? @"" : @"s"];
        [secs addObject:[NSDictionary dictionaryWithObjectsAndKeys:
                         title, @"title", flag ? flag : @"", @"flag", meta, @"meta",
                         key, @"subIdx", rows, @"rows", nil]];
        [_sectionOrder addObject:key];
    }

    [_sections release];
    _sections = [secs retain];

    NSMutableSet *valid = [NSMutableSet set];
    for (NSDictionary *sec in _sections) {
        int subIdx = [[sec objectForKey:@"subIdx"] intValue];
        if (subIdx >= 0) [valid addObject:[NSNumber numberWithInt:subIdx]];
    }
    [_collapsedSubs intersectSet:valid];
}

- (void)setListHeaderProgress:(CGFloat)progress animated:(BOOL)animated {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
/* skip tiny scroll changes */
    if (fabsf((float)(_listHeaderProgress - progress)) < 0.08f) return;
    if (!animated) {
        _listHeaderProgress = progress;
        [self layoutMainChromeGeometry];
        return;
    }
    _headerSnapAnimating = YES;
    [UIView animateWithDuration:0.18
                          delay:0
                        options:UIViewAnimationOptionBeginFromCurrentState |
                                UIViewAnimationOptionAllowUserInteraction |
                                UIViewAnimationOptionCurveEaseOut
                     animations:^{
                         _listHeaderProgress = progress;
                         [self layoutMainChromeGeometry];
                     }
                     completion:^(BOOL finished) {
                         (void)finished;
                         _headerSnapAnimating = NO;
                     }];
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
    [_ctl statusState:^(NSString *state) {
        if (generation != _catalogGeneration) return;
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
    return _busy || [self isTunnelActive];
}

- (void)applyServerListLock {
    BOOL locked = _busy;
    _table.allowsSelection = !locked;
    _table.alpha = locked ? 0.72f : 1.0f;
}

- (void)setLastErr:(NSString *)msg {
    [_lastErr release];
    _lastErr = msg ? [msg copy] : nil;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv {
    (void)tv;
    return (NSInteger)[_sections count];
}

- (void)scrollViewDidScroll:(UIScrollView *)scrollView {
    if (scrollView != _table) return;
    if (_headerSnapAnimating) return;
    CGFloat offset = scrollView.contentOffset.y;
    CGFloat progress = offset <= 0.0f ? 0.0f : offset / 72.0f;
    if (progress > 1.0f) progress = 1.0f;
    [self setListHeaderProgress:progress animated:NO];
}

- (void)snapListHeader {
    if (_headerSnapAnimating) return;
    CGFloat target = _listHeaderProgress >= 0.5f ? 1.0f : 0.0f;
    if (fabsf((float)(_listHeaderProgress - target)) < 0.01f) return;
    [self setListHeaderProgress:target animated:YES];
}

- (void)scrollViewDidEndDragging:(UIScrollView *)scrollView
                   willDecelerate:(BOOL)decelerate {
    if (scrollView != _table) return;
    if (!decelerate)
        [self snapListHeader];
}

- (void)scrollViewDidEndDecelerating:(UIScrollView *)scrollView {
    if (scrollView != _table) return;
    [self snapListHeader];
}

- (void)sectionToggleTapped:(UIButton *)button {
    int subIdx = (int)button.tag - 4000;
    if (subIdx < 0) return;
    NSNumber *key = [NSNumber numberWithInt:subIdx];
    if ([_collapsedSubs containsObject:key])
        [_collapsedSubs removeObject:key];
    else
        [_collapsedSubs addObject:key];

    NSInteger section = NSNotFound;
    for (NSInteger i = 0; i < (NSInteger)[_sections count]; ++i) {
        NSDictionary *sec = [_sections objectAtIndex:i];
        if ([[sec objectForKey:@"subIdx"] intValue] == subIdx) {
            section = i;
            break;
        }
    }
    if (section == NSNotFound) return;
/* reload without animation */
    [_table beginUpdates];
    [_table reloadSections:[NSIndexSet indexSetWithIndex:section]
          withRowAnimation:UITableViewRowAnimationNone];
    [_table endUpdates];
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
        return 100.0f;
    return 88.0f;
}

- (CGFloat)tableView:(UITableView *)tv heightForHeaderInSection:(NSInteger)s {
    (void)tv; (void)s;
    if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad)
        return 60.0f;
    return 52.0f;
}

- (UIView *)tableView:(UITableView *)tv viewForHeaderInSection:(NSInteger)s {
    BOOL pad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
    CGFloat scrH = [UIScreen mainScreen].bounds.size.height;
    CGFloat scrW = [UIScreen mainScreen].bounds.size.width;
    if (scrW > scrH) { CGFloat t = scrH; scrH = scrW; scrW = t; } /* use the short side */
    BOOL compact = (!pad && scrH <= 568.0f);
    CGFloat side = compact ? 8.0f : 10.0f;
    /* old ios can leave table bounds in portrait for one layout pass */
    CGRect rawTableBounds = tv.bounds;
    CGFloat w = CGRectGetWidth(tv.frame);
    if (w < 1.0f) w = CGRectGetWidth(rawTableBounds);
    UIInterfaceOrientation io = UIApplication.sharedApplication.statusBarOrientation;
    BOOL orientationKnown = io == UIInterfaceOrientationPortrait ||
        io == UIInterfaceOrientationPortraitUpsideDown ||
        UIInterfaceOrientationIsLandscape(io);
    if (orientationKnown) {
        BOOL wantsLandscape = UIInterfaceOrientationIsLandscape(io);
        BOOL boundsLandscape = rawTableBounds.size.width > rawTableBounds.size.height + 0.5f;
        if (wantsLandscape != boundsLandscape)
            w = CGRectGetWidth(SenkoViewBounds(tv));
    }
    if (w < 160.0f) w = 160.0f;
    CGFloat plateW = w - side * 2.0f;
    CGFloat hh = pad ? 60.0f : 52.0f;
    CGFloat plateH = hh - 8.0f; /* leave room for title and meta */
    CGFloat cr = SenkoThemeCardRadius();
/* keep actions inside the plate */
    CGFloat actionW = compact ? 30.0f : 32.0f;
    CGFloat actionGap = compact ? 3.0f : 4.0f;
    CGFloat actionRight = (SenkoThemeIsIos16() ? cr * 0.55f : 8.0f) + (compact ? 2.0f : 0.0f);
    if (actionRight < 10.0f) actionRight = 10.0f;
    CGFloat actionH = plateH - 8.0f;
    if (actionH < 28.0f) actionH = 28.0f;
    CGFloat actionY = floorf((plateH - actionH) * 0.5f);
    CGFloat iconPx = compact ? 18.0f : 20.0f;

    if (!_sections || s < 0 || s >= (NSInteger)[_sections count]) return nil;
    NSDictionary *sec = [_sections objectAtIndex:s];
    int subIdx = [[sec objectForKey:@"subIdx"] intValue];
    NSString *title = [sec objectForKey:@"title"];
    NSString *flag = [sec objectForKey:@"flag"];
    /* subscription headers are text-only; flags stay on server rows */
    BOOL showFlag = NO;
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

    UIView *wrap = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, w, hh)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    wrap.clipsToBounds = YES;
    wrap.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    wrap.userInteractionEnabled = YES;
    wrap.tag = 7000 + s;
    UILongPressGestureRecognizer *drag = [[[UILongPressGestureRecognizer alloc]
                                           initWithTarget:self action:@selector(sectionLongPressed:)] autorelease];
    drag.minimumPressDuration = 0.45;
    [wrap addGestureRecognizer:drag];

    UIView *plate = [[[UIView alloc] initWithFrame:CGRectMake(side, 4, plateW, plateH)] autorelease];
    plate.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    plate.layer.cornerRadius = cr;
    plate.layer.borderWidth = 0;
    plate.layer.borderColor = [UIColor clearColor].CGColor;
    plate.clipsToBounds = YES;
    CAGradientLayer *g = [CAGradientLayer layer];
    g.frame = plate.bounds;
    g.cornerRadius = cr;
    g.masksToBounds = YES;
    SenkoFillSectionGradient(g);
    SenkoStyleSectionPlate(plate);
/* keep the shadow outside the plate */
    if (SenkoThemeIsIos16() || SenkoThemeIsIos26()) {
        plate.layer.masksToBounds = NO;
        plate.clipsToBounds = NO;
    }
    [plate.layer insertSublayer:g atIndex:0];
/* cache section chrome */
    plate.layer.shouldRasterize = YES;
    plate.layer.rasterizationScale = [UIScreen mainScreen].scale;
    [wrap addSubview:plate];

    if (subIdx >= 0) {
        UIButton *collapse = [UIButton buttonWithType:UIButtonTypeCustom];
        collapse.frame = plate.bounds;
        collapse.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        collapse.tag = 4000 + subIdx;
        [collapse addTarget:self action:@selector(sectionToggleTapped:)
           forControlEvents:UIControlEventTouchUpInside];
        [plate addSubview:collapse];
    }

    CGFloat moreX = 0, pingX = 0, refreshX = 0;
    CGFloat textW;
    BOOL showActions = (subIdx >= 0) || manualHasAwg;
    /* subscription edit stays on the right with the other actions */
    BOOL showMore = (subIdx >= 0) || manualHasAwg;
    BOOL subPingBusy = subIdx >= 0 &&
        [_pingingSubs containsObject:[NSNumber numberWithInt:subIdx]];
    BOOL showPing = showActions && !subPingBusy;
    if (showActions) {
        CGFloat cursor = CGRectGetWidth(plate.bounds) - actionRight - actionW;
        if (showMore) {
            moreX = cursor;
            if (moreX < 0.0f) moreX = 0.0f;
            cursor -= actionGap + actionW;
        }
        /* keep the action slots stable while a group is being pinged */
        if (showActions) {
            pingX = cursor;
            cursor -= actionGap + actionW;
        }
        refreshX = cursor;
        textW = refreshX - 12.0f;
    } else {
        textW = plateW - 24.0f;
    }
    if (textW < 48.0f) textW = 48.0f;

    CGFloat textX = showFlag ? 48.0f : 12.0f;
    if (showFlag) {
        [plate addSubview:SenkoFlagBadge(flag, CGRectMake(10.0f, 6.0f, 30.0f, 28.0f))];
    }
    CGFloat labelWidth = textW - textX + 12.0f;
    if (labelWidth < 42.0f) labelWidth = 42.0f;
    UILabel *lab = [[[UILabel alloc] initWithFrame:CGRectMake(textX, 6,
                                                               labelWidth, 20)] autorelease];
    lab.backgroundColor = [UIColor clearColor];
    lab.font = SenkoThemeIsIos16() ? SenkoFontBody(15, YES) : [UIFont boldSystemFontOfSize:15];
    SenkoStyleSectionTitle(lab);
    lab.text = title;
    lab.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    lab.lineBreakMode = NSLineBreakByTruncatingTail;
    [plate addSubview:lab];

    UILabel *meta = [[[UILabel alloc] initWithFrame:CGRectMake(textX, 26, labelWidth, 14)] autorelease];
    meta.backgroundColor = [UIColor clearColor];
    meta.font = SenkoThemeIsIos16() ? SenkoFontBody(11, NO) : [UIFont systemFontOfSize:11];
    SenkoStyleSectionMeta(meta);
    meta.text = metaText;
    meta.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    meta.lineBreakMode = NSLineBreakByTruncatingTail;
    [plate addSubview:meta];

    if (showActions) {
        UIColor *iconTint = (SenkoThemeIsBoykisser() || SenkoThemeIsMiside())
            ? [UIColor colorWithRed:1.00 green:0.42 blue:0.72 alpha:1.0]
            : kAccentBlue;
        UIButton *ref = [UIButton buttonWithType:UIButtonTypeCustom];
        ref.frame = CGRectMake(refreshX, actionY, actionW, actionH);
        ref.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
        ref.contentMode = UIViewContentModeCenter;
        ref.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [ref setImage:TintedIconNamed(@"icon-refresh.png", iconPx, iconTint)
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

        UIButton *ping = [UIButton buttonWithType:UIButtonTypeCustom];
        ping.frame = CGRectMake(pingX, actionY, actionW, actionH);
        ping.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
        ping.contentMode = UIViewContentModeCenter;
        ping.imageView.contentMode = UIViewContentModeScaleAspectFit;
        ping.hidden = !showPing;
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

        if (showMore) {
            UIButton *more = [UIButton buttonWithType:UIButtonTypeCustom];
            more.frame = CGRectMake(moreX, actionY, actionW, actionH);
            more.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
            more.contentHorizontalAlignment = UIControlContentHorizontalAlignmentCenter;
            more.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
            [more setTitle:@"\u2022\u2022\u2022" forState:UIControlStateNormal];
            more.titleLabel.font = [UIFont boldSystemFontOfSize:compact ? 14.0f : 16.0f];
            SenkoStyleSectionGlyph(more);
            [more setTitleColor:iconTint forState:UIControlStateNormal];
            [more setTitleColor:[iconTint colorWithAlphaComponent:0.55f]
                       forState:UIControlStateHighlighted];
            more.titleLabel.shadowColor = nil;
            more.titleLabel.shadowOffset = CGSizeZero;
            if (manualHasAwg) {
                [more addTarget:self action:@selector(showAWGMenu)
               forControlEvents:UIControlEventTouchUpInside];
            } else {
                more.tag = 2000 + subIdx;
                [more addTarget:self action:@selector(subMenuTapped:)
               forControlEvents:UIControlEventTouchUpInside];
            }
            [plate addSubview:more];
        }
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
            st = @"...";
        else {
            NSNumber *ms = [_serverStatus objectForKey:[NSNumber numberWithInt:-1]];
            if (ms) st = [NSString stringWithFormat:@"%d ms", [ms intValue]];
        }
/* use the same card style */
        [cell configureWithTitle:@"AmneziaWG"
                          detail:@"awg / udp / full-device"
                          picked:picked
                          status:st];
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
    [cell configureWithServer:sv picked:picked hideLinks:hideLinks pingVal:msVal];
    return cell;
}

- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    SenkoThemeSfxPlay(); /* play the cell tap sound */
    if ([self isServerSelectionLocked]) {
        [tv deselectRowAtIndexPath:ip animated:YES];
        SetStatusDefault(_statusLabel, @"disconnect to switch");
        return;
    }
    if ([self isAWGRowAtIndexPath:ip]) {
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
    SenkoServer *sv = [self serverAtIndexPath:ip];
    if (!sv) return;
    if ([self isTunnelActive] && _activeBackend == SenkoBackendAmneziaWG) {
        [tv deselectRowAtIndexPath:ip animated:YES];
        SetStatusDefault(_statusLabel, @"disconnect to switch backend");
        return;
    }
    int oldIdx = _selectedSrvIdx;
    _selectedBackend = SenkoBackendServer;
    [[NSUserDefaults standardUserDefaults] setInteger:_selectedBackend forKey:SENKO_SELECTED_BACKEND_KEY];
    [[NSUserDefaults standardUserDefaults] synchronize];
    _selectedSrvIdx = sv->index;
    [tv deselectRowAtIndexPath:ip animated:YES];
    NSArray *vis = [tv indexPathsForVisibleRows];
    if ([vis count])
        [tv reloadRowsAtIndexPaths:vis withRowAnimation:UITableViewRowAnimationNone];
    else
        [tv reloadData];
    if ([self isTunnelActive] && _activeBackend == SenkoBackendServer && oldIdx != sv->index)
        [self switchActiveServerIndex:sv->index];
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
    if (subIdx < 0) return;
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
    if (subIdx < 0) return;
    [self pingServersInSub:subIdx];
}

- (void)subMenuTapped:(UIButton *)btn {
    int subIdx = (int)btn.tag - 2000;
    if (subIdx < 0) return;
    if (_actionSheet) {
        [_actionSheet dismissWithClickedButtonIndex:_actionSheet.cancelButtonIndex animated:NO];
        [_actionSheet release];
        _actionSheet = nil;
    }
    _menuSubIdx = subIdx;
    UIActionSheet *as = [[UIActionSheet alloc]
                         initWithTitle:@"Subscription"
                         delegate:self
                         cancelButtonTitle:@"Cancel"
                         destructiveButtonTitle:@"Remove"
                         otherButtonTitles:@"Refresh now", @"Check ping", @"Edit details", nil];
    as.tag = 40;
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
        if (!status || ![status hasPrefix:@"idle"]) {
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
    [self startPingSweep];
}

- (void)startPingSweep {
    _checkGeneration++;
    NSSet *busySubs = [_pingingSubs copy];
    [_pingingSubs removeAllObjects];
    [_serverStatus removeAllObjects];
    [self updateSubscriptionPingButtons:busySubs];
    [busySubs release];
    SetStatusDefault(_statusLabel, @"checking ping...");
    [self checkStatusOfServerAtIndex:0 generation:_checkGeneration];
}

- (void)updateSubscriptionPingButtons:(NSSet *)subIndexes {
    if (!_table || ![subIndexes count] || !_sections || ![_sections count]) return;
    for (NSInteger section = 0; section < (NSInteger)[_sections count]; ++section) {
        if (section >= [_table numberOfSections]) break;
        int subIdx = [[[_sections objectAtIndex:section] objectForKey:@"subIdx"] intValue];
        if (subIdx >= 0 &&
            [subIndexes containsObject:[NSNumber numberWithInt:subIdx]]) {
            UIView *header = [_table headerViewForSection:section];
            UIButton *ping = (UIButton *)[header viewWithTag:(3000 + subIdx)];
            ping.hidden = ![_pingingSubs containsObject:
                            [NSNumber numberWithInt:subIdx]];
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
        SetStatusDefault(_statusLabel, @"ping check complete");
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
    NSMutableArray *idxs = [NSMutableArray array];
    for (SenkoServer *sv in _servers) {
        if (sv->group == subIdx)
            [idxs addObject:[NSNumber numberWithInt:sv->index]];
    }
    if ([idxs count] == 0) {
        SetStatusDefault(_statusLabel, @"no servers in group");
        return;
    }
    [_pingingSubs addObject:[NSNumber numberWithInt:subIdx]];
    [self updateSubscriptionPingButtons:[NSSet setWithObject:
                                         [NSNumber numberWithInt:subIdx]]];
    _checkGeneration++;
    NSInteger gen = _checkGeneration;
    SetStatusDefault(_statusLabel, @"checking group ping...");
    [self pingIndexList:idxs at:0 generation:gen];
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
        SetStatusDefault(_statusLabel, @"group ping complete");
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
    _checkGeneration++;
}


@end
