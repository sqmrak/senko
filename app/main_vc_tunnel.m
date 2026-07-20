#import "main_vc_priv.h"

#import <fcntl.h>
#import <unistd.h>

@implementation MainVC (Tunnel)

- (void)applyState {
    BOOL connecting = [_state isEqualToString:@"connecting"];
    BOOL connected = [_state isEqualToString:@"connected"];
    BOOL active = connected || connecting;
    if (!connecting)
        [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                 selector:@selector(refresh)
                                                   object:nil];
/* recolor connect; keep layout transform */
    if (_connectBtn) {
        CGAffineTransform t = _connectBtn.transform;
        _connectBtn.transform = CGAffineTransformIdentity;
        StyleDomeColors(_connectBtn,
                        active ? kConnOn : kIdleGrey,
                        active ? kConnOnLo : kIdleGreyLo);
        [_connectBtn bringSubviewToFront:_connectBtn.titleLabel];
        _connectBtn.transform = t;
        [_connectBtn setTitle:(connected ? @"ON" : (connecting ? @"..." : @"OFF"))
                     forState:UIControlStateNormal];
    }

/* strip prefers live state, then lasterr, never sticky bare "error" */
    NSString *strip = nil;
    if (connecting) {
        strip = @"connecting...";
    } else if (connected) {
        [self setLastErr:nil];
        strip = @"connected";
    } else if (_lastErr && [_lastErr length]) {
        strip = _lastErr;
    } else if ([_state isEqualToString:@"error"]) {
        strip = @"idle";
        [_state release];
        _state = [@"idle" copy];
    } else {
        strip = _state ? [_state lowercaseString] : @"idle";
    }
    SetStatusDefault(_statusLabel, strip);

/* wallpaper wash tracks tunnel status (animated crossfade) */
    [self applyBackgroundForCurrentState:YES];

    if (_busy)
        _connectBtn.enabled = NO;
    [self applyServerListLock];
}

/* drop status-bar vpn glyph when we know the tunnel is dead */
static void senkoClearVpnIcon(void) {
    const char *path = "/var/mobile/Library/Preferences/com.senko.vpnicon.state";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        (void)write(fd, "0\n", 2);
        close(fd);
    }
}

- (void)forceTunnelCleanupWithReason:(NSString *)reason {
    senkoClearVpnIcon();
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
/* second status pull so strip matches daemon after a stuck connect */
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
        NSString *s = [status stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceAndNewlineCharacterSet]];
/* error* is a dead profile, not a live tunnel */
        BOOL awgLive = [s isEqualToString:@"connecting"] ||
                       [s isEqualToString:@"connected"];
        if (awgLive) {
            [self setToggleBusy:YES];
            [_ctl stopAWG:^(NSString *stopReply) {
                if (!stopReply || [stopReply hasPrefix:@"error"]) {
                    [self setLastErr:stopReply ?
                        [stopReply stringByTrimmingCharactersInSet:
                         [NSCharacterSet whitespaceAndNewlineCharacterSet]] :
                        @"could not stop amneziawg"];
                    [_state release]; _state = [@"error" copy];
                    senkoClearVpnIcon();
                    [self applyState];
                    [self setToggleBusy:NO];
                    return;
                }
                _activeBackend = SenkoBackendNone;
                [_state release]; _state = [@"idle" copy];
                [self setLastErr:nil];
                senkoClearVpnIcon();
                [self applyState];
                [self setToggleBusy:NO];
            }];
            return;
        }
        [self toggleAfterAWGCheck];
    }];
}

- (void)toggleAfterAWGCheck {
    BOOL on = [_state isEqualToString:@"connected"] || [_state isEqualToString:@"connecting"];
    if (!on && _selectedBackend == SenkoBackendAmneziaWG) {
        [self startSavedAWGProfile];
        return;
    }
    [self setToggleBusy:YES];
    if (on) {
        [_ctl disconnectReply:^(NSString *reply) {
            (void)reply;
            _activeBackend = SenkoBackendNone;
            senkoClearVpnIcon();
            [self refresh];
            [self setToggleBusy:NO];
        }];
    } else {
        if (_selectedSrvIdx < 0)
            [self syncSelectionFromDaemon];
        if (_selectedSrvIdx < 0) {
            SetStatusDefault(_statusLabel, @"pick a server first");
            [self setToggleBusy:NO];
            return;
        }
        [_state release];
        _state = [@"connecting" copy];
        [self setLastErr:nil];
        [self applyState];
        [_ctl stopAWG:^(NSString *stopReply) {
            if (!stopReply || [stopReply hasPrefix:@"error"]) {
                [self setLastErr:stopReply ?
                    [stopReply stringByTrimmingCharactersInSet:
                     [NSCharacterSet whitespaceAndNewlineCharacterSet]] :
                    @"could not stop amneziawg"];
                [_state release]; _state = [@"error" copy];
                senkoClearVpnIcon();
                [self applyState];
                [self setToggleBusy:NO];
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
                        finalState = [[ln substringFromIndex:6]
                            stringByTrimmingCharactersInSet:
                            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                    }
                }
            }
/* nil, or only "connecting" (ui rcv timeout mid-failover): force full teardown */
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
                senkoClearVpnIcon();
            [self applyState];
/* catalog reply not mixed into status strip */
            [_ctl listCatalog:^(NSArray *servers, NSArray *subs) {
                if (servers) [self applyCatalog:servers subs:subs];
                [self setToggleBusy:NO];
            }];
            }];
        }];
    }
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
            if (!status || ![status hasPrefix:@"idle"]) {
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
            if (status && [status hasPrefix:@"idle"]) finish();
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

- (void)showAWGMenu {
    if (_actionSheet) {
        [_actionSheet dismissWithClickedButtonIndex:_actionSheet.cancelButtonIndex animated:NO];
        [_actionSheet release];
        _actionSheet = nil;
    }
    UIActionSheet *as = [[UIActionSheet alloc]
                         initWithTitle:@"AmneziaWG"
                         delegate:self
                         cancelButtonTitle:@"Cancel"
                         destructiveButtonTitle:@"Remove"
                         otherButtonTitles:@"Refresh now", @"Check ping", @"Edit details", nil];
    as.tag = 41;
    _actionSheet = as;
    [as showInView:self.view];
}


@end
