#import "main_vc_priv.h"
#import "premium_country_vc.h"

#include <string.h>
#include <time.h>

#include "../daemon/core/amnezia_bundle.h"

/* the key and the install id stay in senkod, root only. the app keeps what the
   screen needs: the country now in use, the countries offered, and the expiry
   of the profile the gateway issued */
@implementation MainVC (Premium)

- (BOOL)hasPremiumProfile {
    return [[NSUserDefaults standardUserDefaults] objectForKey:SENKO_PREMIUM_COUNTRIES_KEY] != nil &&
           [self hasAWGProfile];
}

- (NSArray *)premiumCountries {
    NSArray *list = [[NSUserDefaults standardUserDefaults] arrayForKey:SENKO_PREMIUM_COUNTRIES_KEY];
    return list ? list : [NSArray array];
}

- (NSString *)premiumCountryCode {
    return [[NSUserDefaults standardUserDefaults] stringForKey:SENKO_PREMIUM_COUNTRY_KEY];
}

- (NSString *)premiumCountryName {
    NSString *code = [self premiumCountryCode];
    for (NSDictionary *country in [self premiumCountries])
        if ([[country objectForKey:@"code"] isEqualToString:code])
            return [country objectForKey:@"name"];
    return code;
}

/* the gateway profile lives about two hours. a tunnel started on an expired
   one handshakes against a peer the gateway already dropped */
- (BOOL)premiumNeedsRefresh {
    if (![self hasPremiumProfile]) return NO;
    long long expires = [[[NSUserDefaults standardUserDefaults]
                          objectForKey:SENKO_PREMIUM_EXPIRES_KEY] longLongValue];
    return expires > 0 && (long long)time(NULL) + 120 >= expires;
}

- (void)storePremiumResult:(NSDictionary *)result {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSString *country = [result objectForKey:@"country"];
    if (country) [defaults setObject:country forKey:SENKO_PREMIUM_COUNTRY_KEY];
    else [defaults removeObjectForKey:SENKO_PREMIUM_COUNTRY_KEY];
    [defaults setObject:[result objectForKey:@"countries"] forKey:SENKO_PREMIUM_COUNTRIES_KEY];
    [defaults setObject:[result objectForKey:@"expires"] forKey:SENKO_PREMIUM_EXPIRES_KEY];
    [defaults synchronize];
}

/* the profile is no longer a gateway profile: it was replaced by another one
   or removed. senkod drops the key, so no later refresh can overwrite it */
- (void)forgetPremiumKey {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    BOOL had = [defaults objectForKey:SENKO_PREMIUM_COUNTRIES_KEY] != nil;
    [defaults removeObjectForKey:SENKO_PREMIUM_COUNTRY_KEY];
    [defaults removeObjectForKey:SENKO_PREMIUM_COUNTRIES_KEY];
    [defaults removeObjectForKey:SENKO_PREMIUM_EXPIRES_KEY];
    [defaults synchronize];
    if (!had) return;
    [_ctl premiumRequest:@"FORGET" argument:nil reply:^(NSDictionary *result) {
        NSString *message = [result objectForKey:@"message"];
        if (![[result objectForKey:@"ok"] boolValue]) {
            NSLog(@"senko: premium key was not removed from senkod: %@", message ? message : @"no answer");
            [self setLastErr:@"the premium key could not be removed from senkod"];
            [self applyState];
        } else if ([message rangeOfString:@"slot not released"].location != NSNotFound) {
            /* the key is gone, but the gateway still counts this phone as a device */
            NSLog(@"senko: %@", message);
            [self setLastErr:@"the premium device slot was not released, remove this device in another Amnezia client"];
            [self applyState];
        }
    }];
}

- (void)premiumFailed:(NSString *)message {
    [self setLastErr:[message length] ? message : @"premium request failed"];
    [_state release]; _state = [@"error" copy];
    [self applyState];
}

/* the new profile is already on disk, written by senkod. a tunnel running on
   the old one is stopped first, as an edit of the profile does */
- (void)useFreshPremiumProfile:(NSString *)statusText {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:[self awgProfilePath] forKey:SENKO_AWG_PROFILE_KEY];
    [defaults setInteger:SenkoBackendAmneziaWG forKey:SENKO_SELECTED_BACKEND_KEY];
    [defaults synchronize];
    _selectedBackend = SenkoBackendAmneziaWG;
    [_table reloadData];
    void (^start)(void) = ^{
        /* the expiry just came from the gateway: if the phone clock disagrees,
           another refresh cannot fix it, so the start must not loop back */
        _premiumFresh = YES;
        SetStatusRefresh(_statusLabel, statusText);
        [self startSavedAWGProfile];
    };
    if (_activeBackend != SenkoBackendAmneziaWG) {
        start();
        return;
    }
    [_ctl stopAWG:^(NSString *status) {
        NSString *clean = [status stringByTrimmingCharactersInSet:
                           [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (![clean hasPrefix:@"idle"]) {
            [self premiumFailed:@"could not stop amneziawg"];
            return;
        }
        _activeBackend = SenkoBackendNone;
        start();
    }];
}

- (void)requestPremium:(NSString *)verb argument:(NSString *)arg
                status:(NSString *)status done:(NSString *)doneText {
    if (_premiumBusy) {
        SetStatusDefault(_statusLabel, @"a premium request is already running");
        return;
    }
    _premiumBusy = YES;
    [self showBusyOverlay:SenkoLocalizedText(status)];
    [_ctl ensureDaemon:^(BOOL up, NSString *detail) {
        if (!up) {
            _premiumBusy = NO;
            [self hideBusyOverlay];
            [self premiumFailed:detail ? detail : @"daemon offline: cannot reach the premium gateway"];
            return;
        }
        [_ctl premiumRequest:verb argument:arg reply:^(NSDictionary *result) {
            _premiumBusy = NO;
            [self hideBusyOverlay];
            if (!result) {
                [self premiumFailed:@"daemon offline: cannot reach the premium gateway"];
                return;
            }
            if (![[result objectForKey:@"ok"] boolValue]) {
                [self premiumFailed:[result objectForKey:@"message"]];
                return;
            }
            [self setLastErr:nil];
            [self storePremiumResult:result];
            [self useFreshPremiumProfile:doneText];
        }];
    }];
}

/* YES when body is a premium key and the import is under way, or when it is a
   premium key senkod cannot use and the user has been told why */
- (BOOL)importPremiumKeyData:(NSData *)body {
    if (![body length]) return NO;
    amz_premium_key_t key;
    char reason[192] = "";
    amz_status_t r = amz_premium_parse_key((const char *)[body bytes], [body length],
                                           &key, reason, sizeof reason);
    memset(&key, 0, sizeof key);
    if (r == AMZ_ERR_JSON && strcmp(reason, AMZ_PREMIUM_INVALID_REASON) == 0) {
        [self setLastErr:[NSString stringWithUTF8String:reason]];
        [self applyState];
        return YES;
    }
    if (r != AMZ_OK) return NO;
    NSString *text = [[[NSString alloc] initWithData:body encoding:NSUTF8StringEncoding] autorelease];
    text = [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    /* the control socket carries one line of at most 1023 bytes, minus the verb */
    if (![text length] || [text length] > 1000 ||
        [text rangeOfString:@"\n"].location != NSNotFound) {
        [self setLastErr:@"the premium key does not fit one control line"];
        [self applyState];
        return YES;
    }
    [self requestPremium:@"KEY" argument:text status:@"Requesting the premium profile..."
                    done:@"premium profile added"];
    return YES;
}

- (void)showPremiumCountries {
    NSArray *countries = [self premiumCountries];
    if (![countries count]) {
        SetStatusDefault(_statusLabel, @"no premium countries known, refresh the profile");
        return;
    }
    PremiumCountryVC *picker = [[[PremiumCountryVC alloc]
        initWithCountries:countries current:[self premiumCountryCode] delegate:self] autorelease];
    UINavigationController *nav = [[[SenkoNavigationController alloc]
                                    initWithRootViewController:picker] autorelease];
    StyleNavBarClassic(nav);
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
    SenkoPresentModal(self, nav);
}

- (void)premiumCountryVC:(PremiumCountryVC *)vc didPickCode:(NSString *)code {
    (void)vc;
    [self requestPremium:@"COUNTRY" argument:code status:@"Requesting the premium profile..."
                    done:@"premium country changed"];
}

- (void)refreshPremiumProfile {
    [self requestPremium:@"REFRESH" argument:nil status:@"Requesting the premium profile..."
                    done:@"premium profile refreshed"];
}

@end
