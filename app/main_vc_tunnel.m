#import "main_vc_priv.h"

#import <fcntl.h>
#import <unistd.h>

@implementation MainVC (Tunnel)

/* a stuck-connecting or missing reply means routing may already be half
   applied, so only a reply where the daemon itself settled on a non-connected
   outcome is safe to retry against a different candidate */
static BOOL SenkoConnectReplyIsCleanFailure(NSString *reply) {
    if (!reply) return NO;
    NSString *errReason = nil;
    NSString *finalState = nil;
    for (NSString *line in [reply componentsSeparatedByString:@"\n"]) {
        if ([line length] == 0) continue;
        if ([line hasPrefix:@"ERR "]) {
            errReason = [line substringFromIndex:4];
        } else if ([line hasPrefix:@"STATE "]) {
            finalState = SenkoControlStateFromReply(line, NULL);
        }
    }
    if (!finalState) return NO;
    if ([finalState isEqualToString:@"connecting"] && !errReason) return NO;
    return ![finalState isEqualToString:@"connected"];
}

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
    SetStatusDefault(_statusLabel, [self selectionSummary]);
    if (_trafficPending || _busy || _selectedBackend == SenkoBackendAmneziaWG) return;
    _trafficPending = YES;
    NSUInteger generation = _trafficGeneration;
    [_ctl traffic:^(BOOL known, uint64_t up, uint64_t down) {
        _trafficPending = NO;
        if (generation != _trafficGeneration || !_uptimeTimer ||
            ![_state isEqualToString:@"connected"]) return;
        _trafficKnown = known;
        _trafficUp = up;
        _trafficDown = down;
        SenkoHomeApplyTraffic(&_ui, known, up, down);
        if (SenkoClassicHomeEnabled())
            SetStatusDefault(_statusLabel, [self selectionSummary]);
    }];
}

- (NSString *)selectionSummary {
    if (_selectedBackend == SenkoBackendAmneziaWG)
        return SenkoLocalizedText(@"AmneziaWG profile");
    SenkoServer *picked = nil;
    for (SenkoServer *s in _servers) {
        if (s->index == _selectedSrvIdx) { picked = s; break; }
    }
    if (!picked) return SenkoLocalizedText(@"No server selected");
    NSString *name = [picked->remark length]
        ? SenkoServerDisplayName(picked->remark)
        : (picked->host ? picked->host : @"server");
    NSMutableString *line = [NSMutableString stringWithString:name];
    NSNumber *ms = [_serverStatus objectForKey:[NSNumber numberWithInt:picked->index]];
    if (ms && [ms intValue] >= 0)
        [line appendFormat:@" · %d ms", [ms intValue]];
    if ([_state isEqualToString:@"connected"] && _tunnelUptimeKnown) {
        long elapsed = _tunnelUptime +
            (long)(CACurrentMediaTime() - _tunnelUptimeAt);
        NSString *age = SenkoFormatUptime(elapsed);
        if (age) [line appendFormat:@" · %@", age];
    }
    if (_trafficKnown && SenkoClassicHomeEnabled())
        [line appendFormat:@" · ↑ %@ ↓ %@", SenkoFormatBytes(_trafficUp),
                           SenkoFormatBytes(_trafficDown)];
    return line;
}

- (void)applyState {
    BOOL connecting = [_state isEqualToString:@"connecting"];
    BOOL connected = [_state isEqualToString:@"connected"];
/* the connect reply carries a state but no clock, and nothing polled STATUS
   again while the tunnel stayed up, so the age never arrived and the ticker
   never started: it only appeared after leaving the screen and coming back.
   the clock starts here at zero and the next status reply corrects it with the
   daemon's own, which is the one that survives the app being closed */
    if (connected && !_tunnelUptimeKnown) {
        _tunnelUptime = 0;
        _tunnelUptimeAt = CACurrentMediaTime();
        _tunnelUptimeKnown = YES;
    } else if (!connected) {
        ++_trafficGeneration;
        _trafficKnown = NO;
        _trafficUp = _trafficDown = 0;
        SenkoHomeApplyTraffic(&_ui, NO, 0, 0);
        _tunnelUptimeKnown = NO;
        _tunnelUptime = 0;
        _tunnelUptimeAt = 0.0;
    }
    BOOL active = connected || connecting;
    if (!connecting)
        [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                 selector:@selector(refresh)
                                                   object:nil];
/* the glow behind the orb is painted from backgroundStatusKey, which reports an
   error for as long as one is standing even though _state has already gone back
   to idle. feeding the orb the same key is what stops a grey dot sitting in a
   red halo with nothing on the card to explain it */
    SenkoHomeApplyStatus(&_ui, [self backgroundStatusKey], [self stateHeadline], YES);

    if (connected)
        [self setLastErr:nil];
    else if ([_state isEqualToString:@"error"]) {
        [_state release];
        _state = [@"idle" copy];
    }
    /* a failure is announced by the alert that setLastErr raises; repeating it
       as body text next to the button is what crowded the card */
    SetStatusDefault(_statusLabel, [self selectionSummary]);
    [self syncUptimeTicker];
    (void)active;

    [self applyBackgroundForCurrentState:YES];

    if (_busy)
        _connectBtn.enabled = NO;
    [self applyServerListLock];
}

static void senkoClearStatus(void) {
#if SENKO_STOCK_NATIVE
    return;
#else
    const char *path = "/var/mobile/Library/Preferences/com.senko.status.state";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        (void)write(fd, "0\n", 2);
        close(fd);
    }
#endif
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
#if SENKO_STOCK_NATIVE
    [self toggleAfterAWGCheck];
    return;
#else
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(refresh)
                                               object:nil];
    [_ctl awgStatus:^(NSString *status) {
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
#endif
}

/* tries idx, and on a clean (non-stuck) failure moves to the next
   best-measured sibling collapsed into the same row, until one connects or
   the row is exhausted. replyBlock always fires exactly once, with whichever
   attempt's reply decided the outcome. */
- (void)connectTryingCandidates:(NSArray *)candidates offset:(NSUInteger)offset
                           reply:(void (^)(NSString *reply))replyBlock {
    int idx = [[candidates objectAtIndex:offset] intValue];
    [_ctl connectIndex:idx reply:^(NSString *reply) {
        if (offset + 1 < [candidates count] && SenkoConnectReplyIsCleanFailure(reply)) {
            [self connectTryingCandidates:candidates offset:offset + 1 reply:replyBlock];
            return;
        }
        replyBlock(reply);
    }];
}

- (void)startNativeServerIndex:(int)idx {
#if SENKO_STOCK_NATIVE
    SenkoServer *server = [self serverByIndex:idx];
    if (!server || ![server->link length]) {
        [self setLastErr:@"server link is not cached"];
        [_state release]; _state = [@"error" copy];
        [self applyState];
        [self setToggleBusy:NO];
        return;
    }
    SenkoNativeConfigurationForLink(server->link,
        ^(NSString *json, NSString *endpoint, NSString *error) {
        if (!json) {
            [self setLastErr:error ? error : @"native VPN configuration failed"];
            [_state release]; _state = [@"error" copy];
            [self applyState];
            [self setToggleBusy:NO];
            return;
        }
        [_nativeVPN startWithConfiguration:json serverAddress:endpoint
                                completion:^(NSError *nativeError) {
            if (nativeError) {
                [self setLastErr:[nativeError localizedDescription]];
                [_state release]; _state = [@"error" copy];
                [self applyState];
                [self setToggleBusy:NO];
                return;
            }
            _activeBackend = SenkoBackendServer;
            [_state release]; _state = [@"connected" copy];
            [self setLastErr:nil];
            [self applyState];
            [self setToggleBusy:NO];
        }];
    });
#else
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
            [_state release]; _state = [@"connected" copy];
            [self setLastErr:nil];
            [self applyState];
            [self setToggleBusy:NO];
        }];
    }];
#endif
}

- (void)toggleAfterAWGCheck {
    BOOL on = [_state isEqualToString:@"connected"] || [_state isEqualToString:@"connecting"];
    if (!on && _selectedBackend == SenkoBackendAmneziaWG) {
        [self startSavedAWGProfile];
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
#if SENKO_STOCK_NATIVE
                _activeBackend = SenkoBackendNone;
                [_state release]; _state = [@"idle" copy];
                senkoClearStatus();
                [self setLastErr:nil];
                [self applyState];
                [self setToggleBusy:NO];
#else
                [_ctl disconnectReply:^(NSString *reply) {
                    (void)reply;
                    _activeBackend = SenkoBackendNone;
                    [_state release]; _state = [@"idle" copy];
                    senkoClearStatus();
                    [self setLastErr:nil];
                    [self applyState];
                    [self setToggleBusy:NO];
                }];
#endif
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
#if SENKO_STOCK_NATIVE
        [self startNativeServerIndex:_selectedSrvIdx];
#else
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
            [self connectTryingCandidates:[self connectCandidatesForServerIndex:_selectedSrvIdx]
                                    offset:0
                                     reply:^(NSString *reply) {
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
#endif
    }
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
#if SENKO_STOCK_NATIVE
            [self startNativeServerIndex:idx];
#else
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
#endif
        }];
        return;
    }
    [self connectTryingCandidates:[self connectCandidatesForServerIndex:idx]
                            offset:0
                             reply:^(NSString *reply) {
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
    [self presentViewController:nav animated:YES completion:nil];
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
    [vc dismissViewControllerAnimated:YES completion:nil];
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


@end
