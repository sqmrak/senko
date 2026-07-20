#import "theme_def.h"
#import "ui_theme.h"

#include <string.h>

UIColor *kBG;
UIColor *kBGBot;
UIColor *kFelt;
UIColor *kConnOn;
UIColor *kConnOnLo;
UIColor *kIdleGrey;
UIColor *kIdleGreyLo;
UIColor *kInk;
UIColor *kInkMuted;
UIColor *kAccentBlue;
UIColor *kAccentBlueLo;
UIColor *kChromeHi;
UIColor *kChromeLo;
UIColor *kCellHi;
UIColor *kCellLo;
UIColor *kWell;

NSString * const SenkoThemeDidChangeNotification = @"SenkoThemeDidChangeNotification";
NSString * const SenkoThemeIdKey = @"senko.theme.id";
NSString * const SenkoThemeLightKey = @"senko.theme.light";

static NSString *gThemeId;
static BOOL gLight;
static const SenkoThemeDef *gCur;

/* cached caps so scroll paths skip table lookup */
static unsigned gCaps;
static CGFloat gCardRadius;

UIColor *SenkoThemeC(CGFloat r, CGFloat g, CGFloat b) {
    return [UIColor colorWithRed:r green:g blue:b alpha:1.0];
}

UIColor *SenkoThemeCA(CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
    return [UIColor colorWithRed:r green:g blue:b alpha:a];
}

void SenkoThemeSetSlot(UIColor **slot, UIColor *color) {
    if (*slot == color) return;
    [*slot release];
    *slot = [color retain];
}

NSString *SenkoThemeDefaultId(void) {
    return @"senko-ios6";
}

static BOOL TokenInAliases(NSString *s, const char *aliases) {
    if (!aliases || ![s length]) return NO;
    NSString *a = [[NSString stringWithUTF8String:aliases] lowercaseString];
    NSArray *parts = [a componentsSeparatedByCharactersInSet:
                      [NSCharacterSet whitespaceCharacterSet]];
    for (NSString *p in parts) {
        if (![p length]) continue;
        if ([s rangeOfString:p].location != NSNotFound)
            return YES;
    }
    return NO;
}

const SenkoThemeDef *SenkoThemeFind(NSString *tid) {
    if (![tid length]) return NULL;
    size_t n = 0;
    const SenkoThemeDef *const *tab = SenkoThemeAllDefs(&n);
    for (size_t i = 0; i < n; ++i) {
        if ([tid isEqualToString:[NSString stringWithUTF8String:tab[i]->tid]])
            return tab[i];
    }
    return NULL;
}

/* map old combined ids and loose names to a family */
const SenkoThemeDef *SenkoThemeFindMigrated(NSString *raw, BOOL *outLight) {
    size_t n = 0;
    const SenkoThemeDef *const *tab = SenkoThemeAllDefs(&n);
    NSString *s = [raw lowercaseString];
    if (!s) {
        if (outLight) *outLight = NO;
        return SenkoThemeFind(SenkoThemeDefaultId());
    }

/* exact id first */
    const SenkoThemeDef *exact = SenkoThemeFind(raw);
    if (exact) {
        if (outLight) {
            if (exact->caps & SenkoThemeCapLightOnly)
                *outLight = YES;
            else if (exact->caps & SenkoThemeCapDarkOnly)
                *outLight = NO;
            else if ([s rangeOfString:@"dark"].location != NSNotFound)
                *outLight = NO;
            else if ([s rangeOfString:@"white"].location != NSNotFound ||
                     [s rangeOfString:@"light"].location != NSNotFound)
                *outLight = YES;
            else
                *outLight = NO;
        }
        return exact;
    }

    for (size_t i = 0; i < n; ++i) {
        if (!TokenInAliases(s, tab[i]->aliases))
            continue;
        if (outLight) {
            if (tab[i]->caps & SenkoThemeCapLightOnly)
                *outLight = YES;
            else if (tab[i]->caps & SenkoThemeCapDarkOnly)
                *outLight = NO;
            else
                *outLight = ([s rangeOfString:@"dark"].location == NSNotFound);
        }
        return tab[i];
    }

    if (outLight) *outLight = NO;
    return SenkoThemeFind(SenkoThemeDefaultId());
}

const SenkoThemeDef *SenkoThemeCurrentDef(void) {
    return gCur;
}

NSString *SenkoThemeCurrentId(void) {
    if (gThemeId) return gThemeId;
    return SenkoThemeDefaultId();
}

NSArray *SenkoThemeAllIds(void) {
    size_t n = 0;
    const SenkoThemeDef *const *tab = SenkoThemeAllDefs(&n);
    NSMutableArray *a = [NSMutableArray arrayWithCapacity:n];
    for (size_t i = 0; i < n; ++i)
        [a addObject:[NSString stringWithUTF8String:tab[i]->tid]];
    return a;
}

/* fixed folder order; themes without group fall into custom */
static const char *const kGroupOrder[] = {
    SENKO_THEME_GROUP_IOS,
    SENKO_THEME_GROUP_CUSTOM
};

NSArray *SenkoThemeGroupIds(void) {
    size_t n = 0;
    const SenkoThemeDef *const *tab = SenkoThemeAllDefs(&n);
    NSMutableArray *out = [NSMutableArray arrayWithCapacity:4];
    for (size_t g = 0; g < sizeof kGroupOrder / sizeof kGroupOrder[0]; ++g) {
        NSString *gid = [NSString stringWithUTF8String:kGroupOrder[g]];
        for (size_t i = 0; i < n; ++i) {
            const char *gg = tab[i]->group ? tab[i]->group : SENKO_THEME_GROUP_CUSTOM;
            if (strcmp(gg, kGroupOrder[g]) == 0) {
                [out addObject:gid];
                break;
            }
        }
    }
/* any unknown group ids appear after known folders */
    for (size_t i = 0; i < n; ++i) {
        const char *gg = tab[i]->group ? tab[i]->group : SENKO_THEME_GROUP_CUSTOM;
        NSString *gid = [NSString stringWithUTF8String:gg];
        if (![out containsObject:gid])
            [out addObject:gid];
    }
    return out;
}

NSString *SenkoThemeGroupTitle(NSString *groupId) {
    if ([groupId isEqualToString:@SENKO_THEME_GROUP_IOS])
        return @"iOS";
    if ([groupId isEqualToString:@SENKO_THEME_GROUP_CUSTOM])
        return @"Custom";
    if (![groupId length]) return @"Custom";
/* fallback: capitalized id */
    return [groupId capitalizedString];
}

NSArray *SenkoThemeIdsInGroup(NSString *groupId) {
    size_t n = 0;
    const SenkoThemeDef *const *tab = SenkoThemeAllDefs(&n);
    NSString *want = groupId ? groupId : @SENKO_THEME_GROUP_CUSTOM;
    NSMutableArray *a = [NSMutableArray array];
    for (size_t i = 0; i < n; ++i) {
        const char *gg = tab[i]->group ? tab[i]->group : SENKO_THEME_GROUP_CUSTOM;
        if ([want isEqualToString:[NSString stringWithUTF8String:gg]])
            [a addObject:[NSString stringWithUTF8String:tab[i]->tid]];
    }
    return a;
}

NSString *SenkoThemeGroupOfId(NSString *themeId) {
    const SenkoThemeDef *d = SenkoThemeFind(themeId);
    if (!d || !d->group) return @SENKO_THEME_GROUP_CUSTOM;
    return [NSString stringWithUTF8String:d->group];
}

NSString *SenkoThemeDisplayName(NSString *themeId) {
    const SenkoThemeDef *d = SenkoThemeFind(themeId);
    if (!d) {
        BOOL light = NO;
        d = SenkoThemeFindMigrated(themeId, &light);
    }
    if (!d) return @"";
    return [NSString stringWithUTF8String:d->name];
}

NSString *SenkoThemeBlurb(NSString *themeId) {
    const SenkoThemeDef *d = SenkoThemeFind(themeId);
    if (!d) {
        BOOL light = NO;
        d = SenkoThemeFindMigrated(themeId, &light);
    }
    if (!d || !d->blurb)
        return @"the legacy theme for the legacy community";
    return [NSString stringWithUTF8String:d->blurb];
}

NSString *SenkoThemeStatusLine(void) {
    NSString *name = SenkoThemeDisplayName(SenkoThemeCurrentId());
    if (gCaps & SenkoThemeCapDarkOnly)
        return [NSString stringWithFormat:@"%@    Dark", name];
    if (gCaps & SenkoThemeCapLightOnly)
        return [NSString stringWithFormat:@"%@    White", name];
    if (!SenkoThemeAllowsDark())
        return [NSString stringWithFormat:@"%@    White", name];
    return [NSString stringWithFormat:@"%@    %@", name, gLight ? @"White" : @"Dark"];
}

static void RefreshCaps(void) {
    gCaps = gCur ? gCur->caps : 0;
    gCardRadius = gCur ? gCur->cardRadius : 10.0f;
}

static void ApplyCurrentPalette(void) {
    if (!gCur) {
        gCur = SenkoThemeFind(SenkoThemeDefaultId());
        RefreshCaps();
    }
    if (gCaps & SenkoThemeCapLightOnly)
        gLight = YES;
    else if (gCaps & SenkoThemeCapDarkOnly)
        gLight = NO;

    if (gLight) {
        if (gCur->fillLight) gCur->fillLight();
    } else {
        if (gCur->fillDark) gCur->fillDark();
        else if (gCur->fillLight) gCur->fillLight();
    }
    RefreshCaps();
    SenkoThemeFlushImageCaches();
}

static void PersistAndNotify(void) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setObject:gThemeId forKey:SenkoThemeIdKey];
    [d setBool:gLight forKey:SenkoThemeLightKey];
    [d synchronize];
    [[NSNotificationCenter defaultCenter]
        postNotificationName:SenkoThemeDidChangeNotification object:gThemeId];
}

void SenkoThemeApplyId(NSString *themeId) {
    const SenkoThemeDef *def = SenkoThemeFind(themeId);
    if (!def) def = SenkoThemeFind(SenkoThemeDefaultId());
    NSString *tid = [NSString stringWithUTF8String:def->tid];
    if (gThemeId && [gThemeId isEqualToString:tid]) return;
    [gThemeId release];
    gThemeId = [tid copy];
    gCur = def;
    RefreshCaps();
    if (gCaps & SenkoThemeCapLightOnly)
        gLight = YES;
    else if (gCaps & SenkoThemeCapDarkOnly)
        gLight = NO;
    ApplyCurrentPalette();
    PersistAndNotify();
}

void SenkoThemeSetLight(BOOL light) {
    if (!SenkoThemeAllowsDark()) {
        BOOL fixed = (gCaps & SenkoThemeCapDarkOnly) ? NO : YES;
        if (gLight != fixed) {
            gLight = fixed;
            ApplyCurrentPalette();
            PersistAndNotify();
        }
        return;
    }
    BOOL want = light ? YES : NO;
    if (gLight == want) return;
    gLight = want;
    ApplyCurrentPalette();
    PersistAndNotify();
}

void InitPalette(void) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    NSString *saved = [d stringForKey:SenkoThemeIdKey];
    const SenkoThemeDef *def = NULL;
    BOOL light = NO;

    if (SenkoThemeFind(saved)) {
        def = SenkoThemeFind(saved);
        if ([d objectForKey:SenkoThemeLightKey])
            light = [d boolForKey:SenkoThemeLightKey];
    } else if ([saved length]) {
        def = SenkoThemeFindMigrated(saved, &light);
    } else {
        def = SenkoThemeFind(SenkoThemeDefaultId());
        if ([d objectForKey:SenkoThemeLightKey])
            light = [d boolForKey:SenkoThemeLightKey];
    }
    if (!def) def = SenkoThemeFind(SenkoThemeDefaultId());

    [gThemeId release];
    gThemeId = [[NSString stringWithUTF8String:def->tid] copy];
    gCur = def;
    RefreshCaps();
    gLight = light;
    if (gCaps & SenkoThemeCapLightOnly) gLight = YES;
    if (gCaps & SenkoThemeCapDarkOnly) gLight = NO;
    ApplyCurrentPalette();
    [d setObject:gThemeId forKey:SenkoThemeIdKey];
    [d setBool:gLight forKey:SenkoThemeLightKey];
    [d synchronize];
}

void SenkoBeginSilentLayers(void) {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
}

void SenkoEndSilentLayers(void) {
    [CATransaction commit];
}

void SenkoSetLayerFrame(CALayer *layer, CGRect frame) {
    if (!layer) return;
    if (CGRectEqualToRect(layer.frame, frame)) return;
    SenkoBeginSilentLayers();
    layer.frame = frame;
    SenkoEndSilentLayers();
}

BOOL SenkoThemeIsIos16(void) {
    return (gCaps & SenkoThemeCapIos16) != 0;
}

BOOL SenkoThemeIsIos26(void) {
    return (gCaps & SenkoThemeCapIos26) != 0;
}

BOOL SenkoThemeIsFlat(void) {
    return (gCaps & SenkoThemeCapFlat) != 0;
}

BOOL SenkoThemeUsesFrost(void) {
/* solid frostlite only; live blur is heavy on armv7 */
    return (gCaps & SenkoThemeCapFlat) != 0;
}

BOOL SenkoThemeIsBoykisser(void) {
    return (gCaps & SenkoThemeCapBoykisser) != 0;
}

BOOL SenkoThemeIsMiside(void) {
    return (gCaps & SenkoThemeCapMiside) != 0;
}

BOOL SenkoThemeIsFrutigeraero(void) {
    return (gCaps & SenkoThemeCapAero) != 0;
}

BOOL SenkoThemeAllowsDark(void) {
    return (gCaps & (SenkoThemeCapLightOnly | SenkoThemeCapDarkOnly)) == 0;
}

BOOL SenkoThemeIsLight(void) {
    return gLight;
}

CGFloat SenkoThemeCardRadius(void) {
    return gCardRadius > 0.5f ? gCardRadius : 10.0f;
}
