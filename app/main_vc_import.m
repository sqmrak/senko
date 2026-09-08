#import "main_vc_priv.h"

@implementation MainVC (Import)

- (void)addPressed {
    if (self.presentedViewController) return;
    [self dismissCurrentActionSheetAnimated:YES];
/* one entry per source, not per format: the daemon parser decides what the
   content is, so there is no reason to make the user classify it first */
    UIActionSheet *sheet = [[[UIActionSheet alloc]
        initWithTitle:SenkoLocalizedText(@"Add")
             delegate:self
        cancelButtonTitle:SenkoLocalizedText(@"Cancel")
        destructiveButtonTitle:nil
        otherButtonTitles:SenkoLocalizedText(@"Add subscription"),
                          SenkoLocalizedText(@"Paste from clipboard"),
                          SenkoLocalizedText(@"QR code"),
                          SenkoLocalizedText(@"Import from file"), nil] autorelease];
    _actionSheet = [sheet retain];
    [sheet showInView:self.view];
}

- (void)pasteFromClipboard {
    NSString *s = [[UIPasteboard generalPasteboard] string];
    if ([s length]) [self importText:s];
    else {
        [self setLastErr:@"the clipboard is empty"];
        [self applyState];
    }
}

- (void)dismissCurrentActionSheetAnimated:(BOOL)animated {
    UIActionSheet *sheet = _actionSheet;
    if (!sheet) return;
    _actionSheet = nil;
    sheet.delegate = nil;
    [sheet dismissWithClickedButtonIndex:sheet.cancelButtonIndex animated:animated];
    [sheet release];
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
                      url:(NSString *)url
                   header:(NSString *)header {
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
                [_ctl setSubscriptionHeader:newIdx header:header reply:^(NSString *headerReply) {
                    if (!headerReply || [headerReply hasPrefix:@"ERR"]) {
                        [self setLastErr:headerReply ? [headerReply stringByTrimmingCharactersInSet:ws]
                                                   : @"daemon offline: cannot save header"];
                        [self applyState];
                        return;
                    }
                    [_ctl refreshSubIndex:newIdx reply:^(NSString *refreshReply) {
                        (void)refreshReply;
                        [[NSUserDefaults standardUserDefaults] setObject:url forKey:SENKO_PINNED_SUB_URL_KEY];
                        [[NSUserDefaults standardUserDefaults] synchronize];
                        [vc dismissViewControllerAnimated:YES completion:nil];
                        SetStatusRefresh(_statusLabel, @"subscription saved");
                        [self refresh];
                    }];
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
    NSInteger first = sheet.firstOtherButtonIndex;
    if (sheet.tag == 42) {
        if (idx == sheet.destructiveButtonIndex) {
            [self confirmClearManual];
        } else if (idx == first) {
            [self awgRefreshTapped:nil];
        } else if (idx == first + 1) {
            [self awgPingTapped:nil];
        } else if (idx == first + 2) {
            [self editAWGProfile];
        } else if (idx == first + 3) {
            [self removeSavedAWGProfile];
        }
        return;
    }
    if (sheet.tag >= 500000 && sheet.tag < 600000) {
        NSArray *modes = [NSArray arrayWithObjects:@"tcp", @"proxy", @"tunnel", @"handshake", nil];
        NSInteger choice = idx - first;
        if (choice >= 0 && choice < (NSInteger)[modes count]) {
            [_pingMode release];
            _pingMode = [[modes objectAtIndex:choice] copy];
            [self pingServersInSub:(int)(sheet.tag - 500000)];
        }
        return;
    }
    if (sheet.tag >= 400000 && sheet.tag < 500000) {
        int sub = (int)(sheet.tag - 400000);
        if (![self subscriptionByIndex:sub]) {
            SetStatusDefault(_statusLabel, @"subscription not found");
            return;
        }
        if (idx == first) {
            SetStatusRefresh(_statusLabel, @"refreshing subscription...");
            [_ctl refreshSubIndex:sub reply:^(NSString *reply) {
                if (reply && [reply hasPrefix:@"ERR"])
                    [self setLastErr:[reply stringByTrimmingCharactersInSet:
                          [NSCharacterSet whitespaceAndNewlineCharacterSet]]];
                else
                SetStatusRefresh(_statusLabel, @"subscription updated");
                [self refresh];
            }];
        } else if (idx == first + 1) {
            UIActionSheet *checks = [[[UIActionSheet alloc]
                initWithTitle:SenkoLocalizedText(@"Check type") delegate:self
                cancelButtonTitle:SenkoLocalizedText(@"Cancel") destructiveButtonTitle:nil
                otherButtonTitles:SenkoLocalizedText(@"TCP port only"),
                                  SenkoLocalizedText(@"Through current local proxy"),
                                  SenkoLocalizedText(@"Current tunnel internet access"),
                                  SenkoLocalizedText(@"Full profile check"), nil] autorelease];
            checks.tag = 500000 + sub;
            [checks showInView:self.view];
        } else if (idx == first + 2) {
            SenkoSub *entry = [self subscriptionByIndex:sub];
            if (entry) {
                SubscriptionInfoVC *info = [[[SubscriptionInfoVC alloc]
                    initWithSubscription:entry] autorelease];
                UINavigationController *nav = [[[UINavigationController alloc]
                    initWithRootViewController:info] autorelease];
                [self presentViewController:nav animated:YES completion:nil];
            }
        } else if (idx == first + 3) {
            [self editSubscriptionIndex:sub];
        } else if (idx == sheet.destructiveButtonIndex) {
            if ([self isListMutationLocked]) {
                SetStatusDefault(_statusLabel, @"disconnect to remove");
                return;
            }
            _subscriptionMutationBusy = YES;
            _checkGeneration++;
            NSSet *pinging = [_pingingSubs copy];
            [_pingingSubs removeAllObjects];
            [self updateSubscriptionPingButtons:pinging];
            [pinging release];
            [_pingQueue release];
            _pingQueue = nil;
            _pingPending = 0;
            [self applyServerListLock];
            SetStatusRefresh(_statusLabel, @"removing subscription...");
            [_ctl deleteSubIndex:sub reply:^(NSString *reply) {
                _subscriptionMutationBusy = NO;
                [self applyServerListLock];
                if (!reply || [reply hasPrefix:@"ERR"]) {
                    [self setLastErr:reply ? [reply stringByTrimmingCharactersInSet:
                         [NSCharacterSet whitespaceAndNewlineCharacterSet]]
                                             : @"daemon offline: cannot remove subscription"];
                    [self applyState];
                } else {
                    SetStatusRefresh(_statusLabel, @"subscription removed");
                }
                [self refresh];
            }];
        }
        return;
    }
    if (idx == first) {
        [self promptSubscription];
    } else if (idx == first + 1) {
        [self pasteFromClipboard];
    } else if (idx == first + 2) {
        [self openScanner];
    } else if (idx == first + 3) {
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

/* one line and a scheme senko can dial: the single-link path keeps its precise
   per-link error text instead of the bulk import summary */
static BOOL SenkoLooksLikeSingleServerLink(NSString *s) {
    if ([s rangeOfString:@"\n"].location != NSNotFound) return NO;
    if ([s hasPrefix:@"vless://"] || [s hasPrefix:@"socks5://"]) return YES;
    if (![s hasPrefix:@"http://"] && ![s hasPrefix:@"https://"]) return NO;
    NSURL *u = [NSURL URLWithString:s];
    if (!u) return NO;
    if ([u user] || [u password]) return YES;
    if ([u fragment] != nil) return YES;
    return [u port] != nil &&
           ([[u path] length] == 0 || [[u path] isEqualToString:@"/"]);
}

static BOOL SenkoLooksLikeSubscriptionURL(NSString *s) {
    return [s rangeOfString:@"\n"].location == NSNotFound &&
           ([s hasPrefix:@"http://"] || [s hasPrefix:@"https://"]);
}

/* everything the app cannot classify from one line goes to the daemon, which
   owns the parsers for base64 feeds, xray json, clash yaml and surge profiles */
- (void)importContentData:(NSData *)data {
    if (![data length]) {
        [self setLastErr:@"unknown content type"];
        [self applyState];
        return;
    }
    SetStatusDefault(_statusLabel, @"reading content...");
    [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
        if (!up) {
            [self setLastErr:detail ? detail : @"daemon offline: cannot import"];
            [self applyState];
            return;
        }
        [_ctl importContent:data reply:^(NSString *reply) {
            NSString *clean = [reply stringByTrimmingCharactersInSet:
                               [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (![clean length]) {
                [self setLastErr:@"daemon offline: cannot import"];
                [self applyState];
                return;
            }
            if ([clean hasPrefix:@"OK "]) {
                [self setLastErr:nil];
                SetStatusRefresh(_statusLabel, [clean substringFromIndex:3]);
                [self refresh];
                return;
            }
            [self setLastErr:[clean hasPrefix:@"ERR "]
                             ? [clean substringFromIndex:4] : clean];
            [self applyState];
        }];
    }];
}

- (void)importText:(NSString *)s {
    s = [s stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![s length]) {
        [self setLastErr:@"unknown content type"];
        [self applyState];
        return;
    }
    if ([self isNativeAWGText:s]) {
        [self importAWGText:s];
        return;
    }
    if ([s hasPrefix:@"vpn://"]) {
        [self setLastErr:@"Amnezia VPN bundle detected. Export a native AmneziaWG .conf from Share"];
        [self applyState];
        return;
    }

    if (SenkoLooksLikeSingleServerLink(s)) {
        [self addServerLink:s];
        return;
    }
    if (SenkoLooksLikeSubscriptionURL(s)) {
        if ([s hasPrefix:@"http://"])
            [self confirmInsecureSubscriptionURL:s];
        else
            [self addSubscriptionURL:s name:[self nameFromURL:s]];
        return;
    }
    [self importContentData:[s dataUsingEncoding:NSUTF8StringEncoding]];
}

- (void)confirmInsecureSubscriptionURL:(NSString *)url {
    if (![url length]) return;
    [_pendingInsecureURL release];
    _pendingInsecureURL = [url copy];
    UIAlertView *alert = [[[UIAlertView alloc]
        initWithTitle:SenkoLocalizedText(@"Unencrypted subscription")
              message:SenkoLocalizedText(@"This URL sends the subscription without TLS. Import it only if you trust this network and provider.")
             delegate:self cancelButtonTitle:SenkoLocalizedText(@"Cancel")
     otherButtonTitles:SenkoLocalizedText(@"Import"), nil] autorelease];
    alert.tag = 4;
    [alert show];
}

- (void)addServerLink:(NSString *)link {
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
    NSData *body = [NSData dataWithContentsOfFile:path];
    if (![body length]) {
        [self setLastErr:@"the file is empty or could not be read"];
        [self applyState];
        return;
    }
/* the text tests only make sense on a decodable file; a clash yaml or an xray
   json is text too, and the daemon parser sorts those out */
    NSString *text = [[[NSString alloc] initWithData:body
                                            encoding:NSUTF8StringEncoding] autorelease];
    if ([text length]) {
        if ([self isNativeAWGText:text]) {
            [self importAWGText:text];
            return;
        }
        if ([text rangeOfString:@"vpn://"].location != NSNotFound) {
            [self setLastErr:@"Amnezia VPN bundle detected. Import a native AmneziaWG .conf file"];
            [self applyState];
            return;
        }
        NSString *trimmed = [text stringByTrimmingCharactersInSet:
                             [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (SenkoLooksLikeSubscriptionURL(trimmed) &&
            !SenkoLooksLikeSingleServerLink(trimmed)) {
            [self importText:trimmed];
            return;
        }
    }
    [self importContentData:body];
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

/* uialertview became a uialertcontroller shim in ios 9, and the shim never
   brings up the edit menu over its own text field, so a link could not be
   pasted into either prompt. the real controller is used where it exists */
- (BOOL)promptTextWithTitle:(NSString *)title
                    message:(NSString *)message
                   keyboard:(UIKeyboardType)keyboard
                    handler:(void (^)(NSString *text))handler {
    Class controllerCls = NSClassFromString(@"UIAlertController");
    Class actionCls = NSClassFromString(@"UIAlertAction");
    if (!controllerCls || !actionCls) return NO;
    SEL make = @selector(alertControllerWithTitle:message:preferredStyle:);
    SEL addField = @selector(addTextFieldWithConfigurationHandler:);
    SEL makeAction = @selector(actionWithTitle:style:handler:);
    if (![controllerCls respondsToSelector:make] ||
        ![actionCls respondsToSelector:makeAction])
        return NO;
/* __block keeps the controller out of the action block's retain set, which
   would otherwise hold the alert alive after it is dismissed */
    __block id alert = ((id (*)(id, SEL, id, id, NSInteger))objc_msgSend)
        (controllerCls, make, title, message, 1 /* UIAlertControllerStyleAlert */);
    if (!alert || ![alert respondsToSelector:addField]) return NO;
    ((void (*)(id, SEL, id))objc_msgSend)(alert, addField, ^(UITextField *field) {
        field.keyboardType = keyboard;
        field.autocorrectionType = UITextAutocorrectionTypeNo;
        field.autocapitalizationType = UITextAutocapitalizationTypeNone;
        field.clearButtonMode = UITextFieldViewModeWhileEditing;
    });
    id cancel = ((id (*)(id, SEL, id, NSInteger, id))objc_msgSend)
        (actionCls, makeAction, SenkoLocalizedText(@"Cancel"),
         1 /* UIAlertActionStyleCancel */, nil);
    id confirm = ((id (*)(id, SEL, id, NSInteger, id))objc_msgSend)
        (actionCls, makeAction, SenkoLocalizedText(@"Add"), 0, ^(id action) {
            (void)action;
            NSArray *fields = ((id (*)(id, SEL))objc_msgSend)(alert, @selector(textFields));
            UITextField *field = [fields count] ? [fields objectAtIndex:0] : nil;
            NSString *text = field.text ? field.text : @"";
            if ([text length] && handler) handler(text);
        });
    if (!cancel || !confirm) return NO;
    ((void (*)(id, SEL, id))objc_msgSend)(alert, @selector(addAction:), cancel);
    ((void (*)(id, SEL, id))objc_msgSend)(alert, @selector(addAction:), confirm);
    [self presentViewController:alert animated:YES completion:nil];
    return YES;
}

- (void)promptSubscription {
    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    if ([self promptTextWithTitle:@"Subscription"
                          message:@"paste a subscription URL or a server link"
                         keyboard:UIKeyboardTypeURL
                          handler:^(NSString *text) {
            [self importText:[text stringByTrimmingCharactersInSet:ws]];
        }])
        return;
    UIAlertView *av = [[[UIAlertView alloc] initWithTitle:@"Subscription"
                                                   message:@"paste a subscription URL or a server link"
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
    vc.modalPresentationStyle = UIModalPresentationFullScreen;
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
    if (av.tag == 5) {
        [self clearManualServers];
        return;
    }
    if (av.tag == 4) {
        NSString *url = [[_pendingInsecureURL retain] autorelease];
        [_pendingInsecureURL release];
        _pendingInsecureURL = nil;
        if (idx != av.cancelButtonIndex && [url length])
            [self addSubscriptionURL:url name:[self nameFromURL:url]];
        return;
    }
    if (av.tag != 2) return;
    NSString *text = [[av textFieldAtIndex:0] text];
    if ([text length] == 0) return;
    [self importText:[text stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]]];
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
