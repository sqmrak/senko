#import "main_vc_priv.h"

BOOL SenkoServerIdentityEqual(SenkoServer *a, SenkoServer *b) {
    if (!a || !b) return NO;
    return a->port == b->port &&
           [a->proto isEqualToString:b->proto] &&
           [a->net isEqualToString:b->net] &&
           [a->security isEqualToString:b->security] &&
           [a->host isEqualToString:b->host];
}

@implementation MainVC (List)

- (void)applyCatalog:(NSArray *)servers subs:(NSArray *)subs {
/* keep chrome geometry after reload (refresh / settings return) */
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
    _checkGeneration++;
    [_serverStatus removeAllObjects];
    [self rebuildSections];
    [self reconcileSelectionAfterListKeeping:oldSelected];
    [oldSelected release];
    [_table reloadData];
    [self styleListWell];
    [self layoutMainChrome];
}

/* manual servers first, then one section per subscription */
- (void)rebuildSections {
    NSMutableArray *secs = [NSMutableArray array];
    NSMutableArray *manual = [NSMutableArray array];
    NSMutableDictionary *bySub = [NSMutableDictionary dictionary];

    for (SenkoServer *sv in _servers) {
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

/* awg sits in Manual as a normal row so it is not a third product in the list */
    if ([manual count] > 0 || [self hasAWGProfile]) {
        [secs addObject:[NSDictionary dictionaryWithObjectsAndKeys:
                         @"Manual", @"title",
                         [NSNumber numberWithInt:-1], @"subIdx",
                         manual, @"rows", nil]];
    }

    NSString *pinnedURL = [[NSUserDefaults standardUserDefaults] stringForKey:SENKO_PINNED_SUB_URL_KEY];
    NSMutableArray *orderedSubs = [NSMutableArray arrayWithArray:_subs];
    if ([pinnedURL length]) {
        for (NSUInteger i = 0; i < [orderedSubs count]; ++i) {
            SenkoSub *sub = [orderedSubs objectAtIndex:i];
            if ([sub->url isEqualToString:pinnedURL]) {
                [[sub retain] autorelease];
                [orderedSubs removeObjectAtIndex:i];
                [orderedSubs insertObject:sub atIndex:0];
                break;
            }
        }
    }

    NSMutableSet *seen = [NSMutableSet set];
    for (SenkoSub *sub in orderedSubs) {
        NSNumber *k = [NSNumber numberWithInt:sub->index];
        [seen addObject:k];
        NSArray *rows = [bySub objectForKey:k];
        if (!rows) rows = [NSArray array];
        NSString *title = [sub->name length] ? sub->name : @"Subscription";
        title = [title stringByReplacingOccurrencesOfString:@"_" withString:@" "];
        [secs addObject:[NSDictionary dictionaryWithObjectsAndKeys:
                         title, @"title",
                         k, @"subIdx",
                         rows, @"rows", nil]];
    }
    NSArray *keys = [[bySub allKeys] sortedArrayUsingSelector:@selector(compare:)];
    for (NSNumber *k in keys) {
        if ([seen containsObject:k]) continue;
        [secs addObject:[NSDictionary dictionaryWithObjectsAndKeys:
                         [NSString stringWithFormat:@"Subscription %d", [k intValue]], @"title",
                         k, @"subIdx",
                         [bySub objectForKey:k], @"rows", nil]];
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
/* quantize so scroll does not layout every pixel */
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
    [_ctl listCatalog:^(NSArray *servers, NSArray *subs) {
        if (generation != _catalogGeneration) return;
/* nil reply: daemon down; kick then reload once */
        if (!servers) {
            [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
                if (generation != _catalogGeneration) return;
                if (!up) {
                    if ([self isTunnelActive]) {
/* keep prior strip; transient ctl miss while connected */
                        return;
                    }
                    [self setLastErr:detail ? detail : @"daemon offline"];
                    [_state release];
                    _state = [@"error" copy];
                    [self applyState];
                    return;
                }
                [_ctl listCatalog:^(NSArray *servers2, NSArray *subs2) {
                    if (generation != _catalogGeneration) return;
                    if (!servers2) {
                        if (![self isTunnelActive]) {
                            [self setLastErr:@"daemon offline"];
                            [self applyState];
                        }
                        return;
                    }
                    [self applyCatalog:servers2 subs:subs2];
                    [self startStatusChecks];
                }];
            }];
            return;
        }
        [self applyCatalog:servers subs:subs];
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
/* error* is not live; leftover awg status must not clobber vless */
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
/* unknown/nil status -> idle, not sticky error */
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
    return _busy || [self isTunnelActive];
}

- (void)applyServerListLock {
    BOOL locked = [self isServerSelectionLocked];
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
/* no animation: cheaper and avoids action-button drift on reload */
    [_table beginUpdates];
    [_table reloadSections:[NSIndexSet indexSetWithIndex:section]
          withRowAnimation:UITableViewRowAnimationNone];
    [_table endUpdates];
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
/* plate-local x; inset actions so radius does not clip glyphs */
    BOOL pad = ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad);
    CGFloat scrH = [UIScreen mainScreen].bounds.size.height;
    CGFloat scrW = [UIScreen mainScreen].bounds.size.width;
    if (scrW > scrH) { CGFloat t = scrH; scrH = scrW; scrW = t; } /* portrait short side */
    BOOL compact = (!pad && scrH <= 568.0f);
    CGFloat side = compact ? 8.0f : 10.0f;
    CGFloat w = tv.bounds.size.width;
    if (w < 160.0f) w = 160.0f;
    CGFloat plateW = w - side * 2.0f;
    CGFloat hh = pad ? 60.0f : 52.0f;
    CGFloat plateH = hh - 8.0f; /* 44 / 52: room for title+meta without clipping */
    CGFloat cr = SenkoThemeCardRadius();
/* keep --- inside rounded plate */
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
    NSUInteger n = [[sec objectForKey:@"rows"] count];
    BOOL manualHasAwg = (subIdx < 0 && [self hasAWGProfile]);
    NSUInteger shown = n + (manualHasAwg ? 1 : 0);
    BOOL collapsed = subIdx >= 0 &&
        [_collapsedSubs containsObject:[NSNumber numberWithInt:subIdx]];
    if (subIdx >= 0)
        title = [NSString stringWithFormat:@"%@  %@", collapsed ? @">" : @"v", title];

    UIView *wrap = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, w, hh)] autorelease];
    wrap.backgroundColor = [UIColor clearColor];
    wrap.clipsToBounds = YES;

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
/* shadow needs maskstobounds=no; keep clips on content via gradient only */
    if (SenkoThemeIsIos16() || SenkoThemeIsIos26()) {
        plate.layer.masksToBounds = NO;
        plate.clipsToBounds = NO;
    }
    [plate.layer insertSublayer:g atIndex:0];
/* bake section chrome once (few headers; glass is alpha gradient) */
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
    if (showActions) {
        moreX = plateW - actionRight - actionW;
        pingX = moreX - actionGap - actionW;
        refreshX = pingX - actionGap - actionW;
        textW = refreshX - 12.0f;
    } else {
        textW = plateW - 24.0f;
    }
    if (textW < 48.0f) textW = 48.0f;

    UILabel *lab = [[[UILabel alloc] initWithFrame:CGRectMake(12, 6, textW, 20)] autorelease];
    lab.backgroundColor = [UIColor clearColor];
    lab.font = SenkoThemeIsIos16() ? SenkoFontBody(15, YES) : [UIFont boldSystemFontOfSize:15];
    SenkoStyleSectionTitle(lab);
    lab.text = title;
    lab.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    lab.lineBreakMode = NSLineBreakByTruncatingTail;
    [plate addSubview:lab];

    UILabel *meta = [[[UILabel alloc] initWithFrame:CGRectMake(12, 26, textW, 14)] autorelease];
    meta.backgroundColor = [UIColor clearColor];
    meta.font = SenkoThemeIsIos16() ? SenkoFontBody(11, NO) : [UIFont systemFontOfSize:11];
    SenkoStyleSectionMeta(meta);
    if (subIdx < 0)
        meta.text = [NSString stringWithFormat:@"%lu single config%@",
                     (unsigned long)shown, shown == 1 ? @"" : @"s"];
    else
        meta.text = [NSString stringWithFormat:@"%lu server%@",
                     (unsigned long)n, n == 1 ? @"" : @"s"];
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

        UIButton *more = [UIButton buttonWithType:UIButtonTypeCustom];
        more.frame = CGRectMake(moreX, actionY, actionW, actionH);
        more.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
        more.contentHorizontalAlignment = UIControlContentHorizontalAlignmentCenter;
        more.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
        [more setTitle:@"\u2022\u2022\u2022" forState:UIControlStateNormal];
        more.titleLabel.font = [UIFont boldSystemFontOfSize:compact ? 14.0f : 16.0f];
        SenkoStyleSectionGlyph(more);
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
/* same card language as vless rows; not a separate product */
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
    SenkoThemeSfxPlay(); /* table cell tap is not a uicontrol sendaction */
    if ([self isServerSelectionLocked]) {
        [tv deselectRowAtIndexPath:ip animated:YES];
        SetStatusDefault(_statusLabel, @"disconnect to switch");
        return;
    }
    if ([self isAWGRowAtIndexPath:ip]) {
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
}

- (BOOL)tableView:(UITableView *)tv canEditRowAtIndexPath:(NSIndexPath *)ip {
    (void)tv;
    return ![self isServerSelectionLocked];
}

- (UITableViewCellEditingStyle)tableView:(UITableView *)tv editingStyleForRowAtIndexPath:(NSIndexPath *)ip {
    return UITableViewCellEditingStyleDelete;
}

- (void)tableView:(UITableView *)tv commitEditingStyle:(UITableViewCellEditingStyle)style forRowAtIndexPath:(NSIndexPath *)ip {
    if (style != UITableViewCellEditingStyleDelete) return;
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
    [_serverStatus removeAllObjects];
    [_table reloadData];
    SetStatusDefault(_statusLabel, @"checking ping...");
    [self checkStatusOfServerAtIndex:0 generation:_checkGeneration];
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
    _checkGeneration++;
    NSInteger gen = _checkGeneration;
    SetStatusDefault(_statusLabel, @"checking group ping...");
    [self pingIndexList:idxs at:0 generation:gen];
}

- (void)pingIndexList:(NSArray *)idxs at:(NSUInteger)i generation:(NSInteger)gen {
    if (gen != _checkGeneration) return;
    if (i >= [idxs count]) {
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
