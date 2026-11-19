#import "main_vc_priv.h"

#include <stdlib.h>
#include <objc/message.h>

#include "../daemon/core/amnezia_bundle.h"

@implementation MainVC (Import)

- (void)addPressed {
    if (SenkoPresentedController(self)) return;
    [self dismissCurrentDialog];
/* one entry per source, not per format: the daemon parser decides what the
   content is, so there is no reason to make the user classify it first */
    SenkoDialog *dialog = [SenkoDialog dialogWithTitle:@"Add" message:nil];
    [dialog addItemWithTitle:@"Add subscription" handler:^(SenkoDialog *d) {
        (void)d;
        [self promptSubscription];
    }];
    [dialog addItemWithTitle:@"Paste from clipboard" handler:^(SenkoDialog *d) {
        (void)d;
        [self pasteFromClipboard];
    }];
    [dialog addItemWithTitle:@"QR code" handler:^(SenkoDialog *d) {
        (void)d;
        [self openScanner];
    }];
    [dialog addItemWithTitle:@"Import from file" handler:^(SenkoDialog *d) {
        (void)d;
        [self promptImportFile];
    }];
    [dialog addButtonWithTitle:@"Cancel" style:SenkoDialogButtonCancel handler:nil];
    [self presentDialog:dialog];
}

/* an item that carries only public.url, rtf or a text type under another uti gives
   nil from -string even though the paste banner already fired; read the pasteboard
   once and try each representation from it */
static NSString *ClipboardTextOfValue(id value) {
    if ([value isKindOfClass:[NSString class]]) return value;
    if ([value isKindOfClass:[NSURL class]]) return [value absoluteString];
    if ([value isKindOfClass:[NSAttributedString class]]) return [value string];
    if ([value isKindOfClass:[NSData class]] && [value length]) return [[[NSString alloc] initWithData:value encoding:NSUTF8StringEncoding] autorelease];
    return nil;
}

static NSString *ClipboardText(void) {
    UIPasteboard *pb = [UIPasteboard generalPasteboard];
    NSString *s = [pb string];
    if ([s length]) return s;
    for (NSString *item in [pb strings])
        if ([item length]) return item;
    NSString *url = [[pb URL] absoluteString];
    if ([url length]) return url;
    for (NSDictionary *item in [pb items]) {
        for (id value in [item allValues]) {
            NSString *t = ClipboardTextOfValue(value);
            if ([t length]) return t;
        }
    }
    return nil;
}

/* has* and numberOfItems (ios 10) answer without the ios 16 paste prompt, so they
   say whether the pasteboard holds anything even when every read came back nil */
static BOOL ClipboardHoldsContent(UIPasteboard *pb) {
    /* the armv7 sdk predates these three, so each goes through a typed send */
    SEL count = @selector(numberOfItems);
    SEL strings = @selector(hasStrings);
    SEL urls = @selector(hasURLs);
    if ([pb respondsToSelector:count] &&
        ((NSInteger (*)(id, SEL))objc_msgSend)(pb, count) > 0) return YES;
    if ([pb respondsToSelector:strings] &&
        ((BOOL (*)(id, SEL))objc_msgSend)(pb, strings)) return YES;
    if ([pb respondsToSelector:urls] &&
        ((BOOL (*)(id, SEL))objc_msgSend)(pb, urls)) return YES;
    return NO;
}

- (void)pasteFromClipboard {
    NSString *s = ClipboardText();
    if ([s length]) {
        [self importText:s];
        return;
    }
    UIPasteboard *pb = [UIPasteboard generalPasteboard];
    BOOL holds = ClipboardHoldsContent(pb);
    /* a read that fails on a pasteboard that has items is a denied paste, not an
       empty clipboard, and the logs need to tell the two apart */
    NSLog(@"senko paste: unreadable=%d changeCount=%ld", holds ? 1 : 0, (long)[pb changeCount]);
    [self setLastErr:holds ? @"the clipboard is unreadable" : @"the clipboard is empty"];
    [self applyState];
}

/* one card at a time: a menu opened over another would stack two windows */
- (void)dismissCurrentDialog {
    [_dialog dismiss];
    [_dialog release];
    _dialog = nil;
}

- (void)presentDialog:(SenkoDialog *)dialog {
    [self dismissCurrentDialog];
    _dialog = [dialog retain];
    [dialog show];
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
    UINavigationController *nav = [[[SenkoNavigationController alloc]
                                    initWithRootViewController:edit] autorelease];
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    SenkoPresentModal(self, nav);
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
    [_ctl replaceSubscriptionIndex:idx name:name url:url header:header
                             reply:^(NSString *savedReply) {
        if (!savedReply || [savedReply hasPrefix:@"ERR"]) {
            [self setLastErr:savedReply ? [savedReply stringByTrimmingCharactersInSet:ws]
                                        : @"daemon offline: cannot edit"];
            [self applyState];
            return;
        }
        [_ctl refreshSubIndex:idx reply:^(NSString *refreshReply) {
            if (!refreshReply || [refreshReply hasPrefix:@"ERR"]) {
                [self setLastErr:refreshReply ?
                    [refreshReply stringByTrimmingCharactersInSet:ws] :
                    @"daemon offline: cannot refresh subscription"];
                [self applyState];
                [self refresh];
                return;
            }
            [[NSUserDefaults standardUserDefaults] setObject:url forKey:SENKO_PINNED_SUB_URL_KEY];
            [[NSUserDefaults standardUserDefaults] synchronize];
            SenkoDismissModal(vc, nil);
            SetStatusRefresh(_statusLabel, @"subscription saved");
            [self refresh];
        }];
    }];
}

/* what the subscription menu runs, one case per row. the subscription is looked
   up again because the list can change while the card is open */
- (void)runSubscriptionAction:(int)action sub:(int)sub {
    SenkoSub *entry = [self subscriptionByIndex:sub];
    if (!entry) {
        SetStatusDefault(_statusLabel, @"subscription not found");
        return;
    }
    if (action == 0) {
        SetStatusRefresh(_statusLabel, @"refreshing subscription...");
        [_ctl refreshSubIndex:sub reply:^(NSString *reply) {
            if (!reply || [reply hasPrefix:@"ERR"]) {
                [self setLastErr:reply ? [reply stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]] :
                      @"daemon offline: cannot refresh subscription"];
            } else {
                SetStatusRefresh(_statusLabel, @"subscription updated");
            }
            [self refresh];
        }];
    } else if (action == 1) {
        [self pingServersInSub:sub];
    } else if (action == 2) {
        SubscriptionInfoVC *info = [[[SubscriptionInfoVC alloc]
            initWithSubscription:entry] autorelease];
        UINavigationController *nav = [[[SenkoNavigationController alloc]
            initWithRootViewController:info] autorelease];
        SenkoPresentModal(self, nav);
    } else if (action == 3) {
        [self editSubscriptionIndex:sub];
    } else if (action == 4) {
        [self removeSubscriptionIndex:sub];
    }
}

- (void)removeSubscriptionIndex:(int)sub {
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

- (BOOL)isNativeAWGText:(NSString *)s {
    return [s rangeOfString:@"[Interface]" options:NSCaseInsensitiveSearch].location != NSNotFound &&
           [s rangeOfString:@"[Peer]" options:NSCaseInsensitiveSearch].location != NSNotFound;
}

/* the amnezia client shares a profile as vpn://, as a .vpn file, or as the
   plain json behind both. the amneziawg config inside is what senko dials, so
   it goes through the same validate and save path a native .conf takes */
- (BOOL)importAmneziaBundleData:(NSData *)body {
    if (![body length]) return NO;
    /* a premium key is a vpn:// link too, but it holds no tunnel config: it is a
       credential the gateway trades for one */
    if ([self importPremiumKeyData:body]) return YES;
    static const size_t kConfCap = 64 * 1024;
    char *conf = malloc(kConfCap);
    if (!conf) return NO;
    /* a decoder that runs out of memory returns before it can name a reason,
       and an uninitialised buffer is not something to hand to NSString */
    char reason[192] = "amnezia bundle could not be read";
    size_t len = 0;
    amz_status_t r = amz_bundle_extract_conf((const char *)[body bytes], [body length],
                                             conf, kConfCap, &len, reason, sizeof reason);
    if (r != AMZ_OK) {
        free(conf);
        if (r == AMZ_ERR_NOT_BUNDLE) return NO;
        NSString *why = [NSString stringWithUTF8String:reason];
        [self setLastErr:[why length] ? why : @"amnezia bundle could not be read"];
        [self applyState];
        return YES;
    }
    NSString *text = [[[NSString alloc] initWithBytes:conf
                                               length:len
                                             encoding:NSUTF8StringEncoding] autorelease];
    free(conf);
    if (![text length]) {
        [self setLastErr:@"amnezia bundle carries no readable config"];
        [self applyState];
        return YES;
    }
    [self importAWGText:text];
    return YES;
}

- (BOOL)looksLikeAmneziaBundle:(NSData *)body {
    return [body length] &&
           amz_bundle_looks_like((const char *)[body bytes], [body length]) != 0;
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
        /* a plain profile replaces a premium one, and a kept key would refresh over it */
        [self forgetPremiumKey];
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
    if ([s hasPrefix:@"vless://"] || [s hasPrefix:@"vmess://"] || [s hasPrefix:@"socks5://"] ||
        [s hasPrefix:@"trojan://"] || [s hasPrefix:@"ss://"]) return YES;
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
    NSData *utf8 = [s dataUsingEncoding:NSUTF8StringEncoding];
    if ([self looksLikeAmneziaBundle:utf8] && [self importAmneziaBundleData:utf8])
        return;

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
    NSString *target = [[url copy] autorelease];
    SenkoDialogConfirm(@"Unencrypted subscription",
                       @"This URL sends the subscription without TLS. Import it only if you trust this network and provider.",
                       @"Import", NO, ^{
        [self addSubscriptionURL:target name:[self nameFromURL:target]];
    });
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
        NSString *package = [[path copy] autorelease];
        SenkoDialogConfirm(@"Update Senko",
                           @"install this package over the current version? settings and subscriptions stay in place",
                           @"Update", NO, ^{
            [self presentUpdateForPath:package];
        });
        return;
    }
    NSDictionary *attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:NULL];
    if ([[attributes objectForKey:NSFileSize] unsignedLongLongValue] > 16ULL * 1024ULL * 1024ULL) {
        [self setLastErr:@"the import file exceeds 16 MB"];
        [self applyState];
        return;
    }
    NSData *body = [NSData dataWithContentsOfFile:path];
    if (![body length]) {
        [self setLastErr:@"the file is empty or could not be read"];
        [self applyState];
        return;
    }
/* a .vpn file holds the bare base64 body the vpn:// link carries, which no
   content sniffer can tell from a base64 subscription feed, so the extension is
   what offers the file to the amnezia decoder. a file that turns out not to be
   a bundle keeps going through the ordinary import path */
    BOOL amneziaExtension =
        [[path pathExtension] caseInsensitiveCompare:@"vpn"] == NSOrderedSame;
    if ((amneziaExtension || [self looksLikeAmneziaBundle:body]) &&
        [self importAmneziaBundleData:body])
        return;
/* the text tests only make sense on a decodable file; a clash yaml or an xray
   json is text too, and the daemon parser sorts those out */
    NSString *text = [[[NSString alloc] initWithData:body
                                            encoding:NSUTF8StringEncoding] autorelease];
    if ([text length]) {
        if ([self isNativeAWGText:text]) {
            [self importAWGText:text];
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
                    NSString *why = [r2 stringByTrimmingCharactersInSet:
                                     [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                    if ([why hasPrefix:@"ERR "]) why = [why substringFromIndex:4];
                    [self setLastErr:why];
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

- (void)promptSubscription {
    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    SenkoDialogPrompt(@"Subscription", @"paste a subscription URL or a server link", nil,
                      UIKeyboardTypeURL, @"Add", ^(NSString *text) {
        NSString *trimmed = [text stringByTrimmingCharactersInSet:ws];
        if ([trimmed length]) [self importText:trimmed];
    });
}

- (void)promptImportFile {
    FileImportVC *files = [[[FileImportVC alloc] initWithPath:nil delegate:self] autorelease];
    UINavigationController *nav = [[[SenkoNavigationController alloc]
                                    initWithRootViewController:files] autorelease];
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    SenkoPresentModal(self, nav);
}

- (void)fileImportVCDidCancel:(FileImportVC *)vc {
    (void)vc;
    SenkoDismissModal(self, nil);
}

- (void)fileImportVC:(FileImportVC *)vc didPickPath:(NSString *)path {
    (void)vc;
    SenkoDismissModal(self, ^{
        [self importFileAtPath:path];
    });
}

- (void)presentUpdateForPath:(NSString *)path {
    if (![path length]) return;
    UpdateInstallVC *vc = [[[UpdateInstallVC alloc] initWithControl:_ctl
                                                        packagePath:path] autorelease];
    vc.modalTransitionStyle = UIModalTransitionStyleCoverVertical;
    vc.modalPresentationStyle = UIModalPresentationFullScreen;
    SenkoPresentModal(self, vc);
}

- (void)openScanner {
    QRScanVC *scan = [[[QRScanVC alloc] init] autorelease];
    scan.delegate = self;
    UINavigationController *nav = [[[SenkoNavigationController alloc]
                                    initWithRootViewController:scan] autorelease];
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    SenkoPresentModal(self, nav);
}

- (void)qrScanner:(QRScanVC *)s didDecode:(NSString *)text {
    SenkoDismissModal(self, nil);
    if ([text length]) [self importText:text];
    else {
        [self setLastErr:@"QR code is empty or unreadable"];
        [self applyState];
    }
}

- (void)qrScannerDidCancel:(QRScanVC *)s {
    SenkoDismissModal(self, nil);
}


@end
