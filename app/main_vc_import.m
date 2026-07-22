#import "main_vc_priv.h"

@implementation MainVC (Import)

- (void)addPressed {
    if (self.presentedViewController) return;
    if (_actionSheet) {
        [_actionSheet dismissWithClickedButtonIndex:_actionSheet.cancelButtonIndex animated:YES];
        [_actionSheet release];
        _actionSheet = nil;
    }
    UIActionSheet *sheet = [[[UIActionSheet alloc]
        initWithTitle:@"Add"
             delegate:self
        cancelButtonTitle:@"Cancel"
        destructiveButtonTitle:nil
        otherButtonTitles:@"Paste link", @"Scan QR", @"Subscription", @"Type link", @"Import file", nil] autorelease];
    _actionSheet = [sheet retain];
    [sheet showInView:self.view];
}

- (void)actionSheet:(UIActionSheet *)sheet didDismissWithButtonIndex:(NSInteger)idx {
    (void)idx;
    if (sheet == _actionSheet) {
        [_actionSheet release];
        _actionSheet = nil;
    }
}

- (SenkoSub *)subscriptionByIndex:(int)subIdx {
    for (SenkoSub *s in _subs)
        if (s->index == subIdx) return s;
    return nil;
}

- (void)editSubscriptionIndex:(int)subIdx {
    if ([self isServerSelectionLocked]) {
        SetStatusDefault(_statusLabel, @"disconnect to edit");
        return;
    }
    SenkoSub *sub = [self subscriptionByIndex:subIdx];
    if (!sub) {
        SetStatusDefault(_statusLabel, @"subscription not found");
        return;
    }
    EditSubscriptionVC *edit = [[[EditSubscriptionVC alloc] initWithSub:sub delegate:self] autorelease];
    UINavigationController *nav = [[[UINavigationController alloc]
                                    initWithRootViewController:edit] autorelease];
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    [self presentViewController:nav animated:YES completion:nil];
}

- (void)pinSubscriptionIndex:(int)subIdx {
    if ([self isListMutationLocked]) {
        SetStatusDefault(_statusLabel, @"disconnect to reorder");
        return;
    }
    SenkoSub *sub = [self subscriptionByIndex:subIdx];
    if (!sub || ![sub->url length]) {
        SetStatusDefault(_statusLabel, @"subscription not found");
        return;
    }
    [[NSUserDefaults standardUserDefaults] setObject:sub->url forKey:SENKO_PINNED_SUB_URL_KEY];
    [[NSUserDefaults standardUserDefaults] synchronize];
    NSInteger fromSection = NSNotFound;
    for (NSInteger i = 0; i < (NSInteger)[_sections count]; ++i) {
        if ([[[_sections objectAtIndex:i] objectForKey:@"subIdx"] intValue] == subIdx) {
            fromSection = i;
            break;
        }
    }
    if (fromSection != NSNotFound) {
        if (fromSection != 0)
            [self moveSectionAtIndex:fromSection toIndex:0];
        else {
            [self rebuildSections];
            [_table reloadData];
        }
    }
    SetStatusRefresh(_statusLabel, @"subscription pinned");
}

- (void)editSubscriptionVC:(EditSubscriptionVC *)vc
          saveSubWithIndex:(int)idx
                      name:(NSString *)name
                       url:(NSString *)url {
    if ([self isServerSelectionLocked]) {
        SetStatusDefault(_statusLabel, @"disconnect to edit");
        return;
    }
    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    name = [name stringByTrimmingCharactersInSet:ws];
    url = [url stringByTrimmingCharactersInSet:ws];
    if (![name length] || ![url length]) {
        SetStatusDefault(_statusLabel, @"name and url required");
        return;
    }
    if ([url rangeOfCharacterFromSet:ws].location != NSNotFound) {
        SetStatusDefault(_statusLabel, @"subscription url has spaces");
        return;
    }

    SetStatusRefresh(_statusLabel, @"saving subscription...");
    [_ctl deleteSubIndex:idx reply:^(NSString *delReply) {
        if (!delReply || [delReply hasPrefix:@"ERR"]) {
            [self setLastErr:delReply ? [delReply stringByTrimmingCharactersInSet:ws]
                                      : @"daemon offline: cannot edit"];
            [self applyState];
            return;
        }
        [_ctl addSubscriptionURL:url name:name reply:^(NSString *addReply) {
            if (!addReply || ![addReply hasPrefix:@"OK"]) {
                [self setLastErr:addReply ? [addReply stringByTrimmingCharactersInSet:ws]
                                          : @"daemon offline: cannot edit"];
                [self applyState];
                [self refresh];
                return;
            }
            int newIdx = [self trailingIntOf:addReply];
            if (newIdx >= 0) {
                [_ctl refreshSubIndex:newIdx reply:^(NSString *refreshReply) {
                    (void)refreshReply;
                    [[NSUserDefaults standardUserDefaults] setObject:url forKey:SENKO_PINNED_SUB_URL_KEY];
                    [[NSUserDefaults standardUserDefaults] synchronize];
                    [vc dismissViewControllerAnimated:YES completion:nil];
                    SetStatusRefresh(_statusLabel, @"subscription saved");
                    [self refresh];
                }];
            } else {
                [vc dismissViewControllerAnimated:YES completion:nil];
                SetStatusRefresh(_statusLabel, @"subscription saved");
                [self refresh];
            }
        }];
    }];
}

- (void)actionSheet:(UIActionSheet *)sheet clickedButtonAtIndex:(NSInteger)idx {
    if (idx == sheet.cancelButtonIndex) return;
    NSString *t = [sheet buttonTitleAtIndex:idx];
    if (sheet.tag == 41) {
        if ([t isEqualToString:@"Refresh now"]) {
            [self awgRefreshTapped:nil];
        } else if ([t isEqualToString:@"Check ping"]) {
            [self awgPingTapped:nil];
        } else if ([t isEqualToString:@"Edit details"]) {
            [self editAWGProfile];
        } else if ([t isEqualToString:@"Remove"]) {
            [self removeSavedAWGProfile];
        }
        return;
    }
    if (sheet.tag == 40) {
        int sub = _menuSubIdx;
        if ([t isEqualToString:@"Refresh now"]) {
            SetStatusRefresh(_statusLabel, @"refreshing subscription...");
            [_ctl refreshSubIndex:sub reply:^(NSString *reply) {
                if (reply && [reply hasPrefix:@"ERR"])
                    [self setLastErr:[reply stringByTrimmingCharactersInSet:
                          [NSCharacterSet whitespaceAndNewlineCharacterSet]]];
                else
                SetStatusRefresh(_statusLabel, @"subscription updated");
                [self refresh];
            }];
        } else if ([t isEqualToString:@"Check ping"]) {
            [self pingServersInSub:sub];
        } else if ([t isEqualToString:@"Edit details"]) {
            [self editSubscriptionIndex:sub];
        } else if ([t isEqualToString:@"Remove"]) {
            if ([self isServerSelectionLocked]) {
                SetStatusDefault(_statusLabel, @"disconnect to remove");
                return;
            }
            [_ctl deleteSubIndex:sub reply:^(NSString *reply) {
                (void)reply;
                [self refresh];
                SetStatusRefresh(_statusLabel, @"subscription removed");
            }];
        }
        return;
    }
    if ([t isEqualToString:@"Paste link"]) {
        NSString *s = [[UIPasteboard generalPasteboard] string];
        if ([s length]) [self importText:s];
        else SetStatusDefault(_statusLabel, @"clipboard empty");
    } else if ([t isEqualToString:@"Scan QR"]) {
        [self openScanner];
    } else if ([t isEqualToString:@"Subscription"]) {
        [self promptSubscription];
    } else if ([t isEqualToString:@"Type link"]) {
        [self promptManualLink];
    } else if ([t isEqualToString:@"Import file"]) {
        [self promptImportFile];
    }
}

- (BOOL)isNativeAWGText:(NSString *)s {
    return [s rangeOfString:@"[Interface]" options:NSCaseInsensitiveSearch].location != NSNotFound &&
           [s rangeOfString:@"[Peer]" options:NSCaseInsensitiveSearch].location != NSNotFound;
}

- (void)importAWGText:(NSString *)text {
    if ([text hasPrefix:@"\ufeff"]) text = [text substringFromIndex:1];
    NSString *config = [text stringByAppendingString:[text hasSuffix:@"\n"] ? @"" : @"\n"];
    NSString *path = [self awgProfilePath];
    NSString *old = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    NSError *writeErr = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:
          [path stringByDeletingLastPathComponent]
                         withIntermediateDirectories:YES attributes:nil error:&writeErr] ||
        ![config writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:&writeErr]) {
        [self setLastErr:@"could not save native AmneziaWG config"];
        [self applyState];
        return;
    }
    SetStatusDefault(_statusLabel, @"validating native config...");
    [_ctl validateAWGAtPath:path reply:^(NSString *reply) {
        NSString *result = [reply stringByTrimmingCharactersInSet:
                            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (![result hasPrefix:@"VALID"]) {
            if (old) [old writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
            else [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
            [self setLastErr:[result length] ? result : @"invalid native AmneziaWG config"];
            [_state release]; _state = [@"error" copy];
            [self applyState];
            return;
        }
        [[NSUserDefaults standardUserDefaults] setObject:path forKey:SENKO_AWG_PROFILE_KEY];
        [[NSUserDefaults standardUserDefaults] setInteger:SenkoBackendAmneziaWG
                                                    forKey:SENKO_SELECTED_BACKEND_KEY];
        [[NSUserDefaults standardUserDefaults] synchronize];
        _selectedBackend = SenkoBackendAmneziaWG;
        [_table reloadData];
        SetStatusRefresh(_statusLabel, @"native AmneziaWG config added");
        [self startSavedAWGProfile];
    }];
}

- (void)importText:(NSString *)s {
    s = [s stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([self isNativeAWGText:s]) {
        [self importAWGText:s];
        return;
    }
    if ([s hasPrefix:@"vpn://"]) {
        [self setLastErr:@"Amnezia VPN bundle detected. Export a native AmneziaWG .conf from Share"];
        [self applyState];
        return;
    }
    BOOL isServer = [s hasPrefix:@"vless://"] || [s hasPrefix:@"socks5://"];
    if (!isServer && ([s hasPrefix:@"http://"] || [s hasPrefix:@"https://"])) {
        NSURL *u = [NSURL URLWithString:s];
        if (u) {
            BOOL hasCreds = [u user] || [u password];
            BOOL hasFragment = [u fragment] != nil;
            BOOL hasPortNoPath = [u port] && ([[u path] length] == 0 || [[u path] isEqualToString:@"/"]);
            if (hasCreds || hasFragment || hasPortNoPath) {
                isServer = YES;
            }
        }
    }

    if (isServer) {
        [self addServerLink:s];
    } else if ([s hasPrefix:@"http://"] || [s hasPrefix:@"https://"]) {
        [self addSubscriptionURL:s name:[self nameFromURL:s]];
    } else {
        SetStatusDefault(_statusLabel, @"not a valid server or subscription link");
    }
}- (void)addServerLink:(NSString *)link {
    [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
        if (!up) {
            [self setLastErr:detail ? detail : @"daemon offline: cannot add"];
            [self applyState];
            return;
        }
        [_ctl addServerLink:link reply:^(NSString *reply) {
            if (!reply) {
                [self setLastErr:@"daemon offline: cannot add"];
                [self applyState];
                return;
            }
            if ([reply hasPrefix:@"OK"]) {
                [self setLastErr:nil];
                [self refresh];
            } else {
                [self setLastErr:[reply stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]]];
                [self applyState];
            }
        }];
    }];
}

- (void)importFileAtPath:(NSString *)path {
    if ([[path pathExtension] caseInsensitiveCompare:@"deb"] == NSOrderedSame) {
        [_pendingUpdatePath release];
        _pendingUpdatePath = [path copy];
        UIAlertView *av = [[[UIAlertView alloc] initWithTitle:@"Update Senko"
                                                       message:@"install this package over the current version? settings and subscriptions stay in place"
                                                      delegate:self
                                             cancelButtonTitle:@"Cancel"
                                             otherButtonTitles:@"Update", nil] autorelease];
        av.tag = 3;
        [av show];
        return;
    }
    NSError *err = nil;
    NSString *body = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&err];
    if (!body) {
        SetStatusDefault(_statusLabel, @"file import failed");
        return;
    }
    BOOL awg = [self isNativeAWGText:body];
    if (awg) {
        [self importAWGText:body];
        return;
    }
    if ([body rangeOfString:@"vpn://"].location != NSNotFound) {
        [self setLastErr:@"Amnezia VPN bundle detected. Import a native AmneziaWG .conf file"];
        [self applyState];
        return;
    }
    NSArray *lines = [body componentsSeparatedByCharactersInSet:
                      [NSCharacterSet newlineCharacterSet]];
    NSMutableArray *links = [NSMutableArray array];
    for (NSString *line in lines) {
        NSString *s = [line stringByTrimmingCharactersInSet:
                       [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([s length] == 0 || [s hasPrefix:@"#"]) continue;
        if ([s hasPrefix:@"vless://"] || [s hasPrefix:@"socks5://"] ||
            [s hasPrefix:@"http://"] || [s hasPrefix:@"https://"])
            [links addObject:s];
    }
    if ([links count] == 0) {
        SetStatusDefault(_statusLabel, @"no links in file");
        return;
    }

    __block int left = (int)[links count];
    __block int okCount = 0;
    SetStatusDefault(_statusLabel, [NSString stringWithFormat:@"importing %d link(s)...", left]);
    for (NSString *link in links) {
        BOOL isServer = [link hasPrefix:@"vless://"] || [link hasPrefix:@"socks5://"];
        if (!isServer && ([link hasPrefix:@"http://"] || [link hasPrefix:@"https://"])) {
            NSURL *u = [NSURL URLWithString:link];
            isServer = ([u user] || [u password] || [u fragment] != nil || [u port]) ? YES : NO;
        }
        if (isServer) {
            [_ctl addServerLink:link reply:^(NSString *reply) {
                if ([reply hasPrefix:@"OK"]) okCount++;
                left--;
                if (left == 0) {
                    SetStatusDefault(_statusLabel, [NSString stringWithFormat:@"imported %d link(s)", okCount]);
                    [self refresh];
                }
            }];
        } else {
            [self addSubscriptionURL:link name:[self nameFromURL:link]];
            left--;
            if (left == 0) [self refresh];
        }
    }
}

- (void)addSubscriptionURL:(NSString *)url name:(NSString *)name {
    SetStatusDefault(_statusLabel, @"checking daemon...");
    [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
        if (!up) {
            [self setLastErr:detail ? detail : @"daemon offline: cannot import"];
            [_state release];
            _state = [@"error" copy];
            [self applyState];
            return;
        }
        [_ctl addSubscriptionURL:url name:name reply:^(NSString *reply) {
            if (!reply) {
                [self setLastErr:@"daemon offline: cannot import"];
                [self applyState];
                return;
            }
            if (![reply hasPrefix:@"OK"]) {
                [self setLastErr:[reply stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]]];
                [self applyState];
                return;
            }
            int subIdx = [self trailingIntOf:reply];
            if (subIdx < 0) { [self refresh]; return; }
            SetStatusDefault(_statusLabel, @"fetching subscription...");
            [_ctl sendCommand:[NSString stringWithFormat:@"REFRESH %d", subIdx]
                        timeoutMs:20000
                        reply:^(NSString *r2) {
                if (!r2) {
                    [self setLastErr:@"fetch failed: daemon offline"];
                    [self applyState];
                    return;
                }
                if ([r2 hasPrefix:@"OK"]) {
                    [self setLastErr:nil];
                    SetStatusRefresh(_statusLabel, @"subscription added");
                } else {
                    [self setLastErr:[r2 stringByTrimmingCharactersInSet:
                          [NSCharacterSet whitespaceAndNewlineCharacterSet]]];
                    [self applyState];
                }
                [self refresh];
            }];
        }];
    }];
}

- (int)trailingIntOf:(NSString *)reply {
    NSArray *t = [[reply stringByTrimmingCharactersInSet:
                   [NSCharacterSet whitespaceAndNewlineCharacterSet]]
                  componentsSeparatedByString:@" "];
    if (![t count]) return -1;
    NSString *last = [t lastObject];
    NSScanner *sc = [NSScanner scannerWithString:last];
    int v = -1;
    return [sc scanInt:&v] ? v : -1;
}

- (NSString *)nameFromURL:(NSString *)url {
    NSURL *u = [NSURL URLWithString:url];
    NSString *h = [u host];
    return [h length] ? h : @"subscription";
}

- (void)promptManualLink {
    UIAlertView *av = [[[UIAlertView alloc] initWithTitle:@"Add server"
                                                   message:@"paste a link here"
                                                  delegate:self
                                         cancelButtonTitle:@"Cancel"
                                         otherButtonTitles:@"Add", nil] autorelease];
    av.alertViewStyle = UIAlertViewStylePlainTextInput;
    av.tag = 1;
    [av show];
}

- (void)promptSubscription {
    UIAlertView *av = [[[UIAlertView alloc] initWithTitle:@"Subscription"
                                                   message:@"paste a subscription URL"
                                                  delegate:self
                                         cancelButtonTitle:@"Cancel"
                                         otherButtonTitles:@"Add", nil] autorelease];
    av.alertViewStyle = UIAlertViewStylePlainTextInput;
    av.tag = 2;
    [av show];
}

- (void)promptImportFile {
    FileImportVC *files = [[[FileImportVC alloc] initWithPath:nil delegate:self] autorelease];
    UINavigationController *nav = [[[UINavigationController alloc]
                                    initWithRootViewController:files] autorelease];
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    [self presentViewController:nav animated:YES completion:nil];
}

- (void)fileImportVCDidCancel:(FileImportVC *)vc {
    (void)vc;
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)fileImportVC:(FileImportVC *)vc didPickPath:(NSString *)path {
    (void)vc;
    [self dismissViewControllerAnimated:YES completion:^{
        [self importFileAtPath:path];
    }];
}

- (void)presentUpdateForPath:(NSString *)path {
    if (![path length]) return;
    UpdateInstallVC *vc = [[[UpdateInstallVC alloc] initWithControl:_ctl
                                                        packagePath:path] autorelease];
    vc.modalTransitionStyle = UIModalTransitionStyleCoverVertical;
    [self presentViewController:vc animated:YES completion:nil];
}

- (void)alertView:(UIAlertView *)av clickedButtonAtIndex:(NSInteger)idx {
    if (idx == av.cancelButtonIndex) return;
    if (av.tag == 3) {
        NSString *path = [[_pendingUpdatePath retain] autorelease];
        [_pendingUpdatePath release];
        _pendingUpdatePath = nil;
        [self presentUpdateForPath:path];
        return;
    }
    NSString *text = [[av textFieldAtIndex:0] text];
    if ([text length] == 0) return;
    if (av.tag == 2) [self addSubscriptionURL:
        [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]
                                         name:[self nameFromURL:text]];
    else [self importText:text];
}

- (void)openScanner {
    QRScanVC *scan = [[[QRScanVC alloc] init] autorelease];
    scan.delegate = self;
    UINavigationController *nav = [[[UINavigationController alloc]
                                    initWithRootViewController:scan] autorelease];
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    [self presentViewController:nav animated:YES completion:nil];
}

- (void)qrScanner:(QRScanVC *)s didDecode:(NSString *)text {
    [self dismissViewControllerAnimated:YES completion:nil];
    if ([text length]) [self importText:text];
    else {
        [self setLastErr:@"QR code is empty or unreadable"];
        [self applyState];
    }
}

- (void)qrScannerDidCancel:(QRScanVC *)s {
    [self dismissViewControllerAnimated:YES completion:nil];
}


@end
