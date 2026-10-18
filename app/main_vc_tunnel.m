#import "main_vc_priv.h"

#import <fcntl.h>
#import <unistd.h>

@implementation MainVC (Tunnel)

/* the headline names the state, the detail line under it keeps carrying the
   daemon's own wording, so a failure still shows its exact reason */
- (NSString *)stateHeadline {
    if ([_state isEqualToString:@"connecting"])
        return SenkoLocalizedText(@"Connecting");
    if ([_state isEqualToString:@"connected"])
        return SenkoLocalizedText(@"Connected");
    return SenkoLocalizedText(@"Disconnected");
}

/* the detail line answers "which server, how fast" while the headline answers
   "what is the tunnel doing", so the two never repeat each other */
/* h:mm:ss once past an hour, m:ss below, which is what a session actually
   reads like on this screen */
static NSString *SenkoFormatUptime(long seconds) {
    if (seconds < 0) return nil;
    long h = seconds / 3600;
    long m = (seconds % 3600) / 60;
    long s = seconds % 60;
    if (h > 0)
        return [NSString stringWithFormat:@"%ld:%02ld:%02ld", h, m, s];
    return [NSString stringWithFormat:@"%ld:%02ld", m, s];
}

- (void)nativeStatusWithReply:(void (^)(NSString *, long))done {
    [_nativeVPN status:^(NSInteger status, NSDate *connectedDate) {
        NSString *state = nil;
        if (status == 2 || status == 4)
            state = @"connecting";
        else if (status == 3)
            state = @"connected";
        else if (status == 0)
            state = @"error";
        else if (status == 1 || status == 5)
            state = @"idle";
/* the extension's connectedDate is this backend's equivalent of the daemon's
   own uptime: without it every poll restarted the clock at zero, which is
   what made the on-screen timer climb for one poll interval and drop back */
        long uptime = 0;
        if (connectedDate) {
            NSTimeInterval since = -[connectedDate timeIntervalSinceNow];
            if (since > 0) uptime = (long)since;
        }
        if (done) done(state, uptime);
    }];
}

/* the label ticks once a second while a tunnel is up and stops otherwise, so an
   idle screen schedules nothing */
- (void)syncUptimeTicker {
    BOOL wanted = [_state isEqualToString:@"connected"] && _tunnelUptimeKnown;
    if (wanted == (_uptimeTimer != nil)) return;
    if (!wanted) {
        [_uptimeTimer invalidate];
        _uptimeTimer = nil;
        return;
    }
    _uptimeTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                    target:self
                                                  selector:@selector(uptimeTick)
                                                  userInfo:nil
                                                   repeats:YES];
}

- (void)uptimeTick {
    if (![_state isEqualToString:@"connected"]) {
        [self syncUptimeTicker];
        return;
    }
    SetStatusDefault(_statusLabel, [self homeDetailText]);
}

/* the line under the state answers "for how long", the card below answers
   "through which server", so the two never repeat each other */
- (NSString *)homeDetailText {
    if ([_state isEqualToString:@"connected"]) {
        if (!_tunnelUptimeKnown) return @"";
        long elapsed = _tunnelUptime + (long)(CACurrentMediaTime() - _tunnelUptimeAt);
        NSString *age = SenkoFormatUptime(elapsed);
        return age ? age : @"";
    }
    if ([_state isEqualToString:@"connecting"]) return @"";
    return SenkoLocalizedText(@"Tap to connect");
}

- (void)applyState {
    BOOL connecting = [_state isEqualToString:@"connecting"];
    BOOL connected = [_state isEqualToString:@"connected"];
/* the connect reply carries a state but no clock, and nothing polled STATUS
   again while the tunnel stayed up, so the age never arrived and the ticker
   never started: it only appeared after leaving the screen and coming back.
   the clock starts here at zero. the server backend later reports its own age;
   amneziawg keeps the local clock because AWG STATUS reports only state */
    if (connected && !_tunnelUptimeKnown) {
        _tunnelUptime = 0;
        _tunnelUptimeAt = CACurrentMediaTime();
        _tunnelUptimeKnown = YES;
    } else if (!connected) {
        _tunnelUptimeKnown = NO;
        _tunnelUptime = 0;
        _tunnelUptimeAt = 0.0;
    }
    if (!connecting)
        [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                 selector:@selector(refresh)
                                                   object:nil];
/* the orbit is painted from backgroundStatusKey, which reports an error for as
   long as one is standing even though _state has already gone back to idle */
    [_home->power setStateKey:[self backgroundStatusKey]];
    _home->state.text = [self stateHeadline];

    if (connected)
        [self setLastErr:nil];
    else if ([_state isEqualToString:@"error"]) {
        [_state release];
        _state = [@"idle" copy];
    }
    /* a failure is announced by the alert that setLastErr raises; repeating it
       as body text under the button is what crowded the screen */
    SetStatusDefault(_statusLabel, [self homeDetailText]);
    [self syncHomeServerCard];
    [self syncUptimeTicker];

    [self applyBackgroundForCurrentState:YES];

    if (_busy)
        _connectBtn.enabled = NO;
    [self applyServerListLock];
}

static void senkoClearStatus(void) {
    const char *path = "/var/mobile/Library/Preferences/com.senko.status.state";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        (void)write(fd, "0\n", 2);
        close(fd);
    }
}

- (void)forceTunnelCleanupWithReason:(NSString *)reason {
    senkoClearStatus();
    [_ctl disconnectReply:^(NSString *reply) {
        (void)reply;
        _activeBackend = SenkoBackendNone;
        [_state release];
        _state = [@"idle" copy];
        if (reason && [reason length])
            [self setLastErr:reason];
        else
            [self setLastErr:nil];
        [self applyState];
        [self setToggleBusy:NO];
/* a delayed pull resolves helpers that exit without delivering a callback */
        [self refresh];
    }];
}

- (void)togglePressed {
    if (_busy)
        return;
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(refresh)
                                               object:nil];
    [_ctl awgStatus:^(NSString *status) {
/* no answer at all means senkod itself is down. saying so, with the reason its
   log gives, beats failing one step later on a request it cannot answer */
        if (!status) {
            [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
                if (up) {
                    [self toggleAfterAWGCheck];
                    return;
                }
                [_lastAlertErr release];
                _lastAlertErr = nil;
                [self setLastErr:detail ? detail : @"daemon offline"];
                [self applyState];
            }];
            return;
        }
        NSString *s = [status stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceAndNewlineCharacterSet]];
/* stale awg state must not make a stopped helper appear connected */
        BOOL awgLive = [s isEqualToString:@"connecting"] ||
                       [s isEqualToString:@"connected"];
        if (awgLive) {
            [self setToggleBusy:YES];
            [_ctl stopAWG:^(NSString *stopReply) {
                NSString *stopClean = [stopReply stringByTrimmingCharactersInSet:
                                       [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                if (!stopClean.length || [stopClean hasPrefix:@"error"]) {
                    [self setLastErr:stopClean.length ? stopClean :
                        @"could not stop amneziawg"];
                    [_state release]; _state = [@"error" copy];
                    senkoClearStatus();
                    [self applyState];
                    [self setToggleBusy:NO];
                    return;
                }
                _activeBackend = SenkoBackendNone;
                [_state release]; _state = [@"idle" copy];
                [self setLastErr:nil];
                senkoClearStatus();
                [self applyState];
                [self setToggleBusy:NO];
            }];
            return;
        }
        [self toggleAfterAWGCheck];
    }];
}

- (void)startNativeServerIndex:(int)idx {
    [_ctl nativeConfigurationIndex:idx reply:^(NSString *json, NSString *error) {
        if (!json) {
            [self setLastErr:error ? error : @"native VPN configuration failed"];
            [_state release]; _state = [@"error" copy];
            [self applyState];
            [self setToggleBusy:NO];
            return;
        }
        [_nativeVPN startWithConfiguration:json serverAddress:nil completion:^(NSError *nativeError) {
            if (nativeError) {
                [self setLastErr:[nativeError localizedDescription]];
                [_state release]; _state = [@"error" copy];
                [self applyState];
                [self setToggleBusy:NO];
                return;
            }
            _activeBackend = SenkoBackendServer;
            [_state release]; _state = [@"connecting" copy];
            [self setLastErr:nil];
            [self applyState];
            [self setToggleBusy:NO];
        }];
    }];
}

- (void)toggleAfterAWGCheck {
    BOOL on = [_state isEqualToString:@"connected"] || [_state isEqualToString:@"connecting"];
    if (!on && _selectedBackend == SenkoBackendAmneziaWG) {
        [self startSavedAWGProfile];
        return;
    }
    if (!on && SenkoAutoServerEnabled()) {
        [self connectFastestServer];
        return;
    }
    [self setToggleBusy:YES];
    if (on) {
        if ([SenkoNativeVPN available]) {
            [_nativeVPN stopWithCompletion:^(NSError *nativeError) {
                if (nativeError) {
                    [self setLastErr:[nativeError localizedDescription]];
                    [self applyState];
                    [self setToggleBusy:NO];
                    return;
                }
                [_ctl disconnectReply:^(NSString *reply) {
                    (void)reply;
                    _activeBackend = SenkoBackendNone;
                    [_state release]; _state = [@"idle" copy];
                    senkoClearStatus();
                    [self setLastErr:nil];
                    [self applyState];
                    [self setToggleBusy:NO];
                }];
            }];
            return;
        }
        [_ctl disconnectReply:^(NSString *reply) {
            (void)reply;
            _activeBackend = SenkoBackendNone;
            senkoClearStatus();
            [self refresh];
            [self setToggleBusy:NO];
        }];
    } else {
        [self connectSelectedServer];
    }
}

/* the caller has already marked the toggle busy */
- (void)connectSelectedServer {
    if (_selectedSrvIdx < 0)
        [self syncSelectionFromDaemon];
    if (_selectedSrvIdx < 0) {
/* the user asked for this by tapping connect, so the alert has to show even
   when the same message was raised a moment ago */
        [_lastAlertErr release];
        _lastAlertErr = nil;
        [self setLastErr:@"no configuration is selected"];
        [self applyState];
        [self setToggleBusy:NO];
        return;
    }
    [_state release];
    _state = [@"connecting" copy];
    [self setLastErr:nil];
    [self applyState];
    [_ctl stopAWG:^(NSString *stopReply) {
        NSString *stopClean = [stopReply stringByTrimmingCharactersInSet:
                               [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (!stopClean.length || [stopClean hasPrefix:@"error"]) {
            [self setLastErr:stopClean.length ? stopClean :
                @"could not stop amneziawg"];
            [_state release]; _state = [@"error" copy];
            senkoClearStatus();
            [self applyState];
            [self setToggleBusy:NO];
            return;
        }
        if ([SenkoNativeVPN available]) {
            [_ctl disconnectReply:^(NSString *reply) {
                if (!reply || [reply hasPrefix:@"ERR "]) {
                    NSString *message = reply && [reply length] > 4
                        ? [reply substringFromIndex:4]
                        : @"could not stop legacy VPN";
                    [self setLastErr:message];
                    [_state release]; _state = [@"error" copy];
                    [self applyState];
                    [self setToggleBusy:NO];
                    return;
                }
                [self startNativeServerIndex:_selectedSrvIdx];
            }];
            return;
        }
        [_ctl connectIndex:_selectedSrvIdx reply:^(NSString *reply) {
        NSString *errReason = nil;
        NSString *finalState = nil;
        if (reply) {
            NSArray *lines = [reply componentsSeparatedByString:@"\n"];
            for (NSString *ln in lines) {
                if ([ln length] == 0) continue;
                if ([ln hasPrefix:@"ERR "]) {
                    errReason = [[ln substringFromIndex:4]
                        stringByTrimmingCharactersInSet:
                        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                } else if ([ln hasPrefix:@"STATE "]) {
                    finalState = SenkoControlStateFromReply(ln, NULL);
                }
            }
        }
/* missing replies leave routing active unless the timeout tears it down */
        BOOL stuckConnecting = finalState &&
            [finalState isEqualToString:@"connecting"] && !errReason;
        if (!reply || stuckConnecting) {
            [self forceTunnelCleanupWithReason:@"connect timeout"];
            return;
        }
        if (errReason && [errReason length])
            [self setLastErr:errReason];
        if (finalState && [finalState length]) {
            [_state release];
            _state = [finalState copy];
        } else if (errReason) {
            [_state release];
            _state = [@"error" copy];
        }
        if ([_state isEqualToString:@"error"] ||
            [_state isEqualToString:@"idle"])
            senkoClearStatus();
        [self applyState];
        [_ctl listCatalog:^(NSArray *servers, NSArray *subs, NSArray *order) {
            if (servers) [self applyCatalog:servers subs:subs order:order];
            [self setToggleBusy:NO];
        }];
        }];
    }];
}

- (void)switchActiveServerIndex:(int)idx {
    if (_busy || _activeBackend != SenkoBackendServer) return;
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(refresh)
                                               object:nil];
    [self setToggleBusy:YES];
    [_state release];
    _state = [@"connecting" copy];
    [self setLastErr:nil];
    [self applyState];
    if ([SenkoNativeVPN available]) {
        [_nativeVPN stopWithCompletion:^(NSError *nativeError) {
            if (nativeError) {
                [self setLastErr:[nativeError localizedDescription]];
                [_state release]; _state = [@"error" copy];
                [self applyState];
                [self setToggleBusy:NO];
                return;
            }
            [_ctl disconnectReply:^(NSString *reply) {
                if (!reply || [reply hasPrefix:@"ERR "]) {
                    [self setLastErr:reply && [reply length] > 4
                        ? [reply substringFromIndex:4] : @"could not stop legacy VPN"];
                    [_state release]; _state = [@"error" copy];
                    [self applyState];
                    [self setToggleBusy:NO];
                    return;
                }
                [self startNativeServerIndex:idx];
            }];
        }];
        return;
    }
    [_ctl connectIndex:idx reply:^(NSString *reply) {
        NSString *errReason = nil;
        NSString *finalState = nil;
        if (reply) {
            for (NSString *line in [reply componentsSeparatedByString:@"\n"]) {
                if ([line hasPrefix:@"ERR "])
                    errReason = [[line substringFromIndex:4]
                                 stringByTrimmingCharactersInSet:
                                 [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                else if ([line hasPrefix:@"STATE "])
                    finalState = SenkoControlStateFromReply(line, NULL);
            }
        }
        if (!reply || !finalState) {
            [self forceTunnelCleanupWithReason:@"switch timeout"];
            [self setToggleBusy:NO];
            return;
        }
        if (errReason && [errReason length]) [self setLastErr:errReason];
        [_state release];
        _state = [finalState copy];
        if ([finalState isEqualToString:@"error"] || [finalState isEqualToString:@"idle"])
            senkoClearStatus();
        [self applyState];
        [_ctl listCatalog:^(NSArray *servers, NSArray *subs, NSArray *order) {
            if (servers) [self applyCatalog:servers subs:subs order:order];
            [self setToggleBusy:NO];
        }];
    }];
}

- (void)editAWGProfile {
    if ([self isServerSelectionLocked]) {
        SetStatusDefault(_statusLabel, @"disconnect to edit");
        return;
    }
    NSError *err = nil;
    NSString *config = [NSString stringWithContentsOfFile:[self awgProfilePath]
                                                   encoding:NSUTF8StringEncoding error:&err];
    if (!config) {
        SetStatusDefault(_statusLabel, @"could not read amneziawg config");
        return;
    }
    EditAWGVC *editor = [[[EditAWGVC alloc] initWithConfig:config delegate:self] autorelease];
    UINavigationController *nav = [[[UINavigationController alloc]
                                    initWithRootViewController:editor] autorelease];
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    SenkoPresentModal(self, nav);
}

- (void)editAWGVC:(EditAWGVC *)vc saveConfig:(NSString *)config {
    if ([self isServerSelectionLocked]) {
        SetStatusDefault(_statusLabel, @"disconnect to edit");
        return;
    }
    BOOL valid = [config rangeOfString:@"[Interface]"].location != NSNotFound &&
                 [config rangeOfString:@"[Peer]"].location != NSNotFound &&
                 [config rangeOfString:@"PrivateKey"].location != NSNotFound &&
                 [config rangeOfString:@"Endpoint"].location != NSNotFound;
    if (!valid) {
        SetStatusDefault(_statusLabel, @"invalid amneziawg config");
        return;
    }
    NSError *err = nil;
    if (![config writeToFile:[self awgProfilePath] atomically:YES encoding:NSUTF8StringEncoding error:&err]) {
        SetStatusDefault(_statusLabel, @"could not save amneziawg config");
        return;
    }
    SenkoDismissModal(vc, nil);
    if (_activeBackend == SenkoBackendAmneziaWG) {
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
    } else {
        SetStatusRefresh(_statusLabel, @"amneziawg profile saved");
    }
}

- (void)startSavedAWGProfile {
    NSString *path = [self awgProfilePath];
    if (![self hasAWGProfile]) {
        SetStatusDefault(_statusLabel, @"amneziawg config not found");
        return;
    }
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(refresh)
                                               object:nil];
    _selectedBackend = SenkoBackendAmneziaWG;
    [[NSUserDefaults standardUserDefaults] setInteger:_selectedBackend forKey:SENKO_SELECTED_BACKEND_KEY];
    [[NSUserDefaults standardUserDefaults] synchronize];
    [self setToggleBusy:YES];
    SetStatusDefault(_statusLabel, @"starting amneziawg...");
    [_ctl disconnectReply:^(NSString *reply) {
        if (!reply || [reply hasPrefix:@"ERR "] ||
            [reply rangeOfString:@"STATE idle"].location == NSNotFound) {
            [self setLastErr:@"could not stop senkod"];
            [_state release]; _state = [@"error" copy];
            [self applyState];
            [self setToggleBusy:NO];
            return;
        }
        [_ctl startAWGAtPath:path reply:^(NSString *status) {
            if (!status || [status hasPrefix:@"error"]) {
                [self setLastErr:status ?
                    [status stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]] :
                    @"could not start amneziawg"];
                [_state release]; _state = [@"error" copy];
                [self applyState];
                [self setToggleBusy:NO];
                return;
            }
            _activeBackend = SenkoBackendAmneziaWG;
            [_state release]; _state = [@"connecting" copy];
            [self setLastErr:nil];
            [self applyState];
            [self setToggleBusy:NO];
            [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                     selector:@selector(refresh)
                                                       object:nil];
            [self performSelector:@selector(refresh) withObject:nil afterDelay:2.0];
        }];
    }];
}

- (void)removeSavedAWGProfile {
    NSString *path = [[self awgProfilePath] copy];
    void (^finish)(void) = ^{
        [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:SENKO_AWG_PROFILE_KEY];
        _activeBackend = SenkoBackendNone;
        _selectedBackend = SenkoBackendServer;
        [[NSUserDefaults standardUserDefaults] setInteger:_selectedBackend forKey:SENKO_SELECTED_BACKEND_KEY];
        [[NSUserDefaults standardUserDefaults] synchronize];
        [_state release]; _state = [@"idle" copy];
        [self setLastErr:nil];
        [self applyState];
        [_table reloadData];
        SetStatusRefresh(_statusLabel, @"amneziawg profile removed");
        [path release];
    };
    if (_activeBackend == SenkoBackendAmneziaWG) {
        [_ctl stopAWG:^(NSString *status) {
            NSString *finishClean = [status stringByTrimmingCharactersInSet:
                                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([finishClean hasPrefix:@"idle"]) finish();
            else {
                [self setLastErr:@"could not stop amneziawg"];
                [self applyState];
                [path release];
            }
        }];
    } else {
        finish();
    }
}

- (BOOL)hasManualServers {
    for (SenkoServer *sv in _servers)
        if (sv->group < 0) return YES;
    return NO;
}

/* the manual group owns the saved amneziawg profile and the single step that
   empties the group, so both live in one flat sheet */
- (void)showManualMenu {
    [self dismissCurrentActionSheetAnimated:NO];
    BOOL canClear = [self hasManualServers];
    BOOL hasAWG = [self hasAWGProfile];
    if (!canClear && !hasAWG) return;
    UIActionSheet *as = nil;
    if (hasAWG) {
        as = [[UIActionSheet alloc]
              initWithTitle:SenkoLocalizedText(@"Manual")
              delegate:self
              cancelButtonTitle:SenkoLocalizedText(@"Cancel")
              destructiveButtonTitle:canClear ? SenkoLocalizedText(@"Delete all servers") : nil
              otherButtonTitles:SenkoLocalizedText(@"AmneziaWG: refresh"),
                                SenkoLocalizedText(@"AmneziaWG: check ping"),
                                SenkoLocalizedText(@"AmneziaWG: edit details"),
                                SenkoLocalizedText(@"AmneziaWG: remove profile"), nil];
    } else {
        as = [[UIActionSheet alloc]
              initWithTitle:SenkoLocalizedText(@"Manual")
              delegate:self
              cancelButtonTitle:SenkoLocalizedText(@"Cancel")
              destructiveButtonTitle:SenkoLocalizedText(@"Delete all servers")
              otherButtonTitles:nil];
    }
    as.tag = 42;
    _actionSheet = as;
    [as showInView:self.view];
}

- (void)confirmClearManual {
    UIAlertView *av = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Delete all servers")
              message:SenkoLocalizedText(@"Every server in the Manual group is removed. Subscriptions are not touched.")
             delegate:self
    cancelButtonTitle:SenkoLocalizedText(@"Cancel")
    otherButtonTitles:SenkoLocalizedText(@"Delete"), nil] autorelease];
    av.tag = 5;
    [av show];
}

- (void)clearManualServers {
    if ([self isListMutationLocked]) {
        SetStatusDefault(_statusLabel, @"disconnect to remove");
        return;
    }
    _checkGeneration++;
    SetStatusRefresh(_statusLabel, @"removing manual servers...");
    [_ctl clearManualServers:^(NSString *reply) {
        NSString *clean = [reply stringByTrimmingCharactersInSet:
                           [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (![clean length] || [clean hasPrefix:@"ERR"]) {
            [self setLastErr:[clean hasPrefix:@"ERR "]
                             ? [clean substringFromIndex:4]
                             : @"daemon offline: cannot remove"];
            [self applyState];
        } else {
            SetStatusRefresh(_statusLabel, @"manual servers removed");
        }
        [self refresh];
    }];
}


/* quick connect measures at most this many servers before it dials. a panel
   feed can carry hundreds, and three checks at a time through a daemon that
   answers each within its own timeout would keep the button busy for minutes */
static const NSUInteger kSenkoAutoProbeLimit = 24;

/* servers already measured go first, fastest first, so a recent sweep decides
   the candidates, and unmeasured ones fill the rest in catalog order */
- (NSArray *)autoProbeCandidates {
    NSMutableArray *measured = [NSMutableArray array];
    NSMutableArray *unknown = [NSMutableArray array];
    for (SenkoServer *sv in _servers) {
        if (!sv->supported) continue;
        NSNumber *ms = [_serverStatus objectForKey:[NSNumber numberWithInt:sv->index]];
        if (ms && [ms intValue] >= 0) [measured addObject:sv];
        else [unknown addObject:sv];
    }
    [measured sortUsingComparator:^NSComparisonResult(SenkoServer *a, SenkoServer *b) {
        int pa = [[_serverStatus objectForKey:[NSNumber numberWithInt:a->index]] intValue];
        int pb = [[_serverStatus objectForKey:[NSNumber numberWithInt:b->index]] intValue];
        if (pa == pb) return NSOrderedSame;
        return pa < pb ? NSOrderedAscending : NSOrderedDescending;
    }];
    [measured addObjectsFromArray:unknown];
    if ([measured count] > kSenkoAutoProbeLimit)
        [measured removeObjectsInRange:NSMakeRange(kSenkoAutoProbeLimit,
                                                   [measured count] - kSenkoAutoProbeLimit)];
    return measured;
}

- (void)connectFastestServer {
    NSArray *candidates = [self autoProbeCandidates];
    if (![candidates count]) {
        [_lastAlertErr release];
        _lastAlertErr = nil;
        [self setLastErr:@"no configuration is selected"];
        [self applyState];
        return;
    }
    [self setToggleBusy:YES];
    [_state release];
    _state = [@"connecting" copy];
    [self setLastErr:nil];
    [self applyState];
    SetStatusRefresh(_statusLabel, SenkoLocalizedText(@"Looking for the fastest server"));
    [_autoQueue release];
    _autoQueue = [candidates copy];
    [_autoResults release];
    _autoResults = [[NSMutableArray alloc] initWithCapacity:[candidates count]];
    for (NSUInteger i = 0; i < [candidates count]; ++i)
        [_autoResults addObject:[NSNumber numberWithInt:-1]];
    _autoNext = 0;
    _autoPending = 0;
    _autoDone = 0;
    [self launchAutoProbes:++_autoGeneration];
}

- (void)launchAutoProbes:(NSUInteger)generation {
    if (generation != _autoGeneration) return;
    /* every check holds a daemon client slot for its whole probe */
    while (_autoPending < 3 && _autoNext < [_autoQueue count]) {
        NSUInteger slot = _autoNext++;
        SenkoServer *server = [_autoQueue objectAtIndex:slot];
        int index = server->index;
        NSNumber *key = [NSNumber numberWithInt:index];
        _autoPending++;
        [_serverStatus setObject:[NSNumber numberWithInt:-3] forKey:key];
        [self reloadServerRowForIndex:index];
        [_ctl checkIndex:index mode:@"tcp" reply:^(int ms, NSString *error) {
            (void)error;
            if (generation != _autoGeneration) return;
            _autoPending--;
            _autoDone++;
            [_autoResults replaceObjectAtIndex:slot withObject:[NSNumber numberWithInt:ms]];
            [self setPingResult:ms forServerIndex:[key intValue]];
            [self reloadServerRowForIndex:index];
            if (_autoDone >= [_autoQueue count])
                [self finishAutoProbes];
            else
                [self launchAutoProbes:generation];
        }];
    }
}

/* a subscription refresh can renumber the catalog while the checks run, so the
   winner is found again by what it is rather than by the index it had */
- (void)finishAutoProbes {
    SenkoServer *winner = nil;
    int bestMs = INT_MAX;
    for (NSUInteger slot = 0; slot < [_autoQueue count]; ++slot) {
        int ms = [[_autoResults objectAtIndex:slot] intValue];
        if (ms >= 0 && ms < bestMs) {
            bestMs = ms;
            winner = [_autoQueue objectAtIndex:slot];
        }
    }
    int best = -1;
    for (SenkoServer *sv in _servers) {
        if (winner && SenkoServerIdentityEqual(sv, winner)) {
            best = sv->index;
            break;
        }
    }
    [_autoQueue release];
    _autoQueue = nil;
    [_autoResults release];
    _autoResults = nil;
    if (best < 0) {
        [_state release];
        _state = [@"idle" copy];
        [_lastAlertErr release];
        _lastAlertErr = nil;
        [self setLastErr:@"no server answered the check"];
        [self applyState];
        [self setToggleBusy:NO];
        return;
    }
    _selectedBackend = SenkoBackendServer;
    _selectedSrvIdx = best;
    [self rebuildSections];
    [_table reloadData];
    [self syncHomeServerCard];
    [self connectSelectedServer];
}

- (SenkoSub *)subscriptionForServer:(SenkoServer *)server {
    if (!server || server->group < 0) return nil;
    for (SenkoSub *sub in _subs)
        if (sub->index == server->group) return sub;
    return nil;
}

- (NSString *)sourceNameForServer:(SenkoServer *)server {
    SenkoSub *sub = [self subscriptionForServer:server];
    if (sub && [sub->name length])
        return [sub->name stringByReplacingOccurrencesOfString:@"_" withString:@" "];
    return SenkoLocalizedText(@"Manual");
}

- (NSString *)nameForServer:(SenkoServer *)server {
    NSString *shown = [_rowName objectForKey:[NSNumber numberWithInt:server->index]];
    if ([shown length]) return shown;
    NSString *name = SenkoServerDisplayName(server->remark);
    if ([name length]) return name;
    return server->host ? server->host : @"server";
}

/* the card names what the power button will dial: the amneziawg profile, the
   quick connect choice, or one server with where it came from */
- (void)syncHomeServerCard {
    if (!_home) return;
    UIColor *accent = kAccentBlue;
    if (_selectedBackend == SenkoBackendAmneziaWG) {
        [_home setProtocolLine:@"AMNEZIAWG  ·  UDP"];
        [_home setServerTitle:@"AmneziaWG"
                     subtitle:SenkoLocalizedText(@"AmneziaWG profile")
                         icon:SenkoIconShield(20.0f, accent)
                       isFlag:NO];
        return;
    }
    if ([_servers count] == 0 && ![self hasAWGProfile]) {
        [_home setProtocolLine:nil];
        [_home setServerTitle:SenkoLocalizedText(@"No servers")
                     subtitle:SenkoLocalizedText(@"Add a subscription or a server")
                         icon:SenkoPlusIcon(20.0f, accent)
                       isFlag:NO];
        return;
    }
    SenkoServer *picked = [self serverByIndex:_selectedSrvIdx];
    if (SenkoAutoServerEnabled()) {
        BOOL live = [self isTunnelActive] && _activeBackend == SenkoBackendServer && picked;
        [_home setProtocolLine:live ? [[SenkoServerProtocolLabel(picked)
            stringByReplacingOccurrencesOfString:@"/" withString:@"  ·  "] uppercaseString] :
            [NSString stringWithFormat:SenkoLocalizedText(@"Available servers: %lu"),
                                       (unsigned long)[_servers count]]];
        [_home setServerTitle:SenkoLocalizedText(@"Auto")
                     subtitle:live ? [self nameForServer:picked]
                                   : SenkoLocalizedText(@"Fastest server")
                         icon:TintedIconNamed(@"glyph-bolt.png", 20.0f, accent)
                       isFlag:NO];
        return;
    }
    if (!picked) {
        [_home setProtocolLine:nil];
        [_home setServerTitle:SenkoLocalizedText(@"Choose a server")
                     subtitle:SenkoLocalizedText(@"Tap to open the list")
                         icon:TintedIconNamed(@"glyph-globe.png", 20.0f, accent)
                       isFlag:NO];
        return;
    }
    NSMutableString *subtitle = [NSMutableString stringWithString:
                                 [self sourceNameForServer:picked]];
    [_home setProtocolLine:[[SenkoServerProtocolLabel(picked)
        stringByReplacingOccurrencesOfString:@"/" withString:@"  ·  "] uppercaseString]];
    NSNumber *ms = [self bestPingForServer:picked];
    if (ms && [ms intValue] >= 0)
        [subtitle appendFormat:@" · %@", [NSString stringWithFormat:
                                          SenkoLocalizedText(@"%d ms"), [ms intValue]]];
    [_home setServerTitle:[self nameForServer:picked]
                 subtitle:subtitle
                     icon:SenkoServerBadgeImage(picked->remark, 44.0f)
                   isFlag:YES];
}

@end
