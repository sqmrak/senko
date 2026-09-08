#import "theme_custom.h"
#import "ui_theme.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* user themes live in preferences and are registered into the same table the
   builtin families use, so the picker, search, grouping and apply paths need no
   special case for them */
#define SENKO_CUSTOM_KEY   @"senko.theme.custom.v1"
#define SENKO_CUSTOM_MAX   64
#define SENKO_CUSTOM_MAGIC @"senko-theme-v1"

static NSMutableArray *gThemes;   /* array of NSMutableDictionary */
static SenkoThemeDef  *gDefs;     /* parallel structs handed to the registry */
static const SenkoThemeDef **gDefPtrs;
static size_t   gDefCount;
static unsigned gGeneration = 1;

static const struct {
    const char *key;
    const char *title;
    const char *hint;
} kSlots[SenkoSlotCount] = {
    { "bg",         "Background",      "wallpaper top" },
    { "bgBot",      "Background low",  "wallpaper bottom" },
    { "felt",       "Felt",            "list backdrop" },
    { "connOn",     "Online",          "connected button top" },
    { "connOnLo",   "Online low",      "connected button bottom" },
    { "idle",       "Offline",         "disconnected button top" },
    { "idleLo",     "Offline low",     "disconnected button bottom" },
    { "ink",        "Text",            "primary label" },
    { "inkMuted",   "Text muted",      "secondary label" },
    { "accent",     "Accent",          "links and glyphs" },
    { "accentLo",   "Accent low",      "pressed accent" },
    { "chromeHi",   "Chrome",          "bars top" },
    { "chromeLo",   "Chrome low",      "bars bottom" },
    { "cellHi",     "Cell",            "row top" },
    { "cellLo",     "Cell low",        "row bottom" },
    { "well",       "Well",            "list inset tint" }
};

NSString *SenkoThemeSlotKey(SenkoThemeSlot slot) {
    if (slot < 0 || slot >= SenkoSlotCount) return @"";
    return [NSString stringWithUTF8String:kSlots[slot].key];
}

NSString *SenkoThemeSlotTitle(SenkoThemeSlot slot) {
    if (slot < 0 || slot >= SenkoSlotCount) return @"";
    return [NSString stringWithUTF8String:kSlots[slot].title];
}

NSString *SenkoThemeSlotHint(SenkoThemeSlot slot) {
    if (slot < 0 || slot >= SenkoSlotCount) return @"";
    return [NSString stringWithUTF8String:kSlots[slot].hint];
}

static UIColor **SlotRef(SenkoThemeSlot slot) {
    switch (slot) {
        case SenkoSlotBG:         return &kBG;
        case SenkoSlotBGBot:      return &kBGBot;
        case SenkoSlotFelt:       return &kFelt;
        case SenkoSlotConnOn:     return &kConnOn;
        case SenkoSlotConnOnLo:   return &kConnOnLo;
        case SenkoSlotIdleGrey:   return &kIdleGrey;
        case SenkoSlotIdleGreyLo: return &kIdleGreyLo;
        case SenkoSlotInk:        return &kInk;
        case SenkoSlotInkMuted:   return &kInkMuted;
        case SenkoSlotAccent:     return &kAccentBlue;
        case SenkoSlotAccentLo:   return &kAccentBlueLo;
        case SenkoSlotChromeHi:   return &kChromeHi;
        case SenkoSlotChromeLo:   return &kChromeLo;
        case SenkoSlotCellHi:     return &kCellHi;
        case SenkoSlotCellLo:     return &kCellLo;
        case SenkoSlotWell:       return &kWell;
        default:                  return NULL;
    }
}

/* uikit colors can live in the white colorspace, where getRed: fails on ios 5 */
static BOOL ColorComponents(UIColor *color, CGFloat *r, CGFloat *g, CGFloat *b, CGFloat *a) {
    if (!color) return NO;
    if ([color respondsToSelector:@selector(getRed:green:blue:alpha:)] &&
        [color getRed:r green:g blue:b alpha:a])
        return YES;
    CGColorRef cg = [color CGColor];
    if (!cg) return NO;
    size_t n = CGColorGetNumberOfComponents(cg);
    const CGFloat *c = CGColorGetComponents(cg);
    if (!c) return NO;
    if (n >= 4) {
        *r = c[0]; *g = c[1]; *b = c[2]; *a = c[3];
        return YES;
    }
    if (n >= 2) {
        *r = *g = *b = c[0];
        *a = c[1];
        return YES;
    }
    return NO;
}

static unsigned ClampByte(CGFloat v) {
    long n = lroundf((float)(v * 255.0f));
    if (n < 0) n = 0;
    if (n > 255) n = 255;
    return (unsigned)n;
}

NSString *SenkoHexFromColor(UIColor *color) {
    CGFloat r = 0, g = 0, b = 0, a = 1;
    if (!ColorComponents(color, &r, &g, &b, &a)) return @"000000FF";
    return [NSString stringWithFormat:@"%02X%02X%02X%02X",
            ClampByte(r), ClampByte(g), ClampByte(b), ClampByte(a)];
}

static int HexNibble(unichar c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

UIColor *SenkoColorFromHex(NSString *hex) {
    if (![hex isKindOfClass:[NSString class]]) return nil;
    NSString *s = [hex stringByTrimmingCharactersInSet:
                   [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([s hasPrefix:@"#"]) s = [s substringFromIndex:1];
    NSUInteger len = [s length];
    if (len != 6 && len != 8) return nil;
    unsigned v[8];
    for (NSUInteger i = 0; i < len; ++i) {
        int n = HexNibble([s characterAtIndex:i]);
        if (n < 0) return nil;
        v[i] = (unsigned)n;
    }
    CGFloat r = (CGFloat)(v[0] * 16 + v[1]) / 255.0f;
    CGFloat g = (CGFloat)(v[2] * 16 + v[3]) / 255.0f;
    CGFloat b = (CGFloat)(v[4] * 16 + v[5]) / 255.0f;
    CGFloat a = (len == 8) ? (CGFloat)(v[6] * 16 + v[7]) / 255.0f : 1.0f;
    return [UIColor colorWithRed:r green:g blue:b alpha:a];
}

BOOL SenkoCustomIsCustomId(NSString *themeId) {
    return [themeId isKindOfClass:[NSString class]] && [themeId hasPrefix:@"custom-"];
}

/* ---- validation ------------------------------------------------------- */

/* remote and imported dictionaries are hostile: keep only known keys, and
   require every slot to parse before the theme is allowed into the table */
static NSMutableDictionary *SanitizePalette(id raw) {
    if (![raw isKindOfClass:[NSDictionary class]]) return nil;
    NSMutableDictionary *out =
        [NSMutableDictionary dictionaryWithCapacity:SenkoSlotCount];
    for (int i = 0; i < SenkoSlotCount; ++i) {
        NSString *key = SenkoThemeSlotKey((SenkoThemeSlot)i);
        id value = [(NSDictionary *)raw objectForKey:key];
        UIColor *c = SenkoColorFromHex(value);
        if (!c) return nil;
        [out setObject:SenkoHexFromColor(c) forKey:key];
    }
    return out;
}

static NSMutableDictionary *SanitizeTheme(id raw) {
    if (![raw isKindOfClass:[NSDictionary class]]) return nil;
    NSDictionary *d = (NSDictionary *)raw;

    id name = [d objectForKey:@"name"];
    if (![name isKindOfClass:[NSString class]] || ![(NSString *)name length])
        return nil;
    if ([(NSString *)name length] > 40)
        name = [(NSString *)name substringToIndex:40];

    NSMutableDictionary *light = SanitizePalette([d objectForKey:@"light"]);
    if (!light) return nil;
    NSMutableDictionary *dark = SanitizePalette([d objectForKey:@"dark"]);

    id styleObj = [d objectForKey:@"style"];
    int style = [styleObj isKindOfClass:[NSNumber class]] ? [styleObj intValue] : 1;
    if (style < 0 || style > 2) style = 1;

    id radiusObj = [d objectForKey:@"radius"];
    double radius = [radiusObj isKindOfClass:[NSNumber class]] ? [radiusObj doubleValue] : 12.0;
    if (radius < 0.0) radius = 0.0;
    if (radius > 32.0) radius = 32.0;

    NSMutableDictionary *out = [NSMutableDictionary dictionaryWithCapacity:6];
    [out setObject:name forKey:@"name"];
    [out setObject:light forKey:@"light"];
    if (dark) [out setObject:dark forKey:@"dark"];
    [out setObject:[NSNumber numberWithInt:style] forKey:@"style"];
    [out setObject:[NSNumber numberWithDouble:radius] forKey:@"radius"];

    id tid = [d objectForKey:@"id"];
    if (SenkoCustomIsCustomId(tid) && [(NSString *)tid length] <= 48)
        [out setObject:[[tid copy] autorelease] forKey:@"id"];
    return out;
}

static unsigned CapsForStyle(int style) {
    if (style == 0) return 0; /* classic emboss chrome */
    if (style == 2) return SenkoThemeCapFlat | SenkoThemeCapIos16 | SenkoThemeCapIos26;
    return SenkoThemeCapFlat;
}

/* ---- palette application --------------------------------------------- */

static NSDictionary *PaletteForDef(const SenkoThemeDef *def, BOOL light) {
    if (!def || !gThemes) return nil;
    for (size_t i = 0; i < gDefCount; ++i) {
        if (&gDefs[i] != def) continue;
        NSDictionary *theme = [gThemes objectAtIndex:i];
        NSDictionary *p = light ? [theme objectForKey:@"light"] : [theme objectForKey:@"dark"];
        if (!p) p = [theme objectForKey:@"light"];
        return p;
    }
    return nil;
}

static void ApplyPalette(NSDictionary *palette) {
    if (!palette) return;
    for (int i = 0; i < SenkoSlotCount; ++i) {
        UIColor **ref = SlotRef((SenkoThemeSlot)i);
        if (!ref) continue;
        UIColor *c = SenkoColorFromHex([palette objectForKey:
                                        SenkoThemeSlotKey((SenkoThemeSlot)i)]);
        if (c) SenkoThemeSetSlot(ref, c);
    }
}

/* the fill signature carries no argument, so the trampolines resolve the theme
   through the def the registry is currently applying */
static void FillCustomLight(void) {
    ApplyPalette(PaletteForDef(SenkoThemeCurrentDef(), YES));
}

static void FillCustomDark(void) {
    ApplyPalette(PaletteForDef(SenkoThemeCurrentDef(), NO));
}

/* ---- registry --------------------------------------------------------- */

static void FreeDefs(void) {
    for (size_t i = 0; i < gDefCount; ++i) {
        free((void *)gDefs[i].tid);
        free((void *)gDefs[i].name);
        free((void *)gDefs[i].blurb);
    }
    free(gDefs);
    free(gDefPtrs);
    gDefs = NULL;
    gDefPtrs = NULL;
    gDefCount = 0;
}

static char *DupUTF8(NSString *s) {
    const char *c = [s UTF8String];
    if (!c) c = "";
    size_t n = strlen(c) + 1;
    char *out = (char *)malloc(n);
    if (out) memcpy(out, c, n);
    return out;
}

static void RebuildDefs(void) {
    FreeDefs();
    size_t n = gThemes ? [gThemes count] : 0;
    if (n == 0) {
        gGeneration++;
        SenkoThemeRebindCurrent();
        return;
    }
    gDefs = (SenkoThemeDef *)calloc(n, sizeof *gDefs);
    gDefPtrs = (const SenkoThemeDef **)calloc(n, sizeof *gDefPtrs);
    if (!gDefs || !gDefPtrs) {
        FreeDefs();
        gGeneration++;
        SenkoThemeRebindCurrent();
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        NSDictionary *t = [gThemes objectAtIndex:i];
        int style = [[t objectForKey:@"style"] intValue];
        unsigned caps = CapsForStyle(style);
        if (![t objectForKey:@"dark"]) caps |= SenkoThemeCapLightOnly;
        gDefs[i].tid = DupUTF8([t objectForKey:@"id"]);
        gDefs[i].name = DupUTF8([t objectForKey:@"name"]);
        gDefs[i].blurb = DupUTF8(@"made on this device");
        gDefs[i].caps = caps;
        gDefs[i].cardRadius = (CGFloat)[[t objectForKey:@"radius"] doubleValue];
        gDefs[i].fillLight = FillCustomLight;
        gDefs[i].fillDark = [t objectForKey:@"dark"] ? FillCustomDark : NULL;
        gDefs[i].aliases = NULL;
        gDefs[i].group = SENKO_THEME_GROUP_CUSTOM;
        gDefPtrs[i] = &gDefs[i];
    }
    gDefCount = n;
    gGeneration++;
    SenkoThemeRebindCurrent();
}

static void PersistThemes(void) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setObject:(gThemes ? gThemes : [NSArray array]) forKey:SENKO_CUSTOM_KEY];
    [d synchronize];
}

void SenkoCustomLoad(void) {
    if (gThemes) return;
    gThemes = [[NSMutableArray alloc] initWithCapacity:8];
    id stored = [[NSUserDefaults standardUserDefaults] objectForKey:SENKO_CUSTOM_KEY];
    if ([stored isKindOfClass:[NSArray class]]) {
        for (id entry in (NSArray *)stored) {
            if ([gThemes count] >= SENKO_CUSTOM_MAX) break;
            NSMutableDictionary *clean = SanitizeTheme(entry);
            if (!clean) continue;
            if (![clean objectForKey:@"id"]) continue; /* stored themes keep their id */
            [gThemes addObject:clean];
        }
    }
    RebuildDefs();
}

const SenkoThemeDef *const *SenkoCustomDefs(size_t *outCount) {
    SenkoCustomLoad();
    if (outCount) *outCount = gDefCount;
    return gDefPtrs;
}

unsigned SenkoCustomGeneration(void) {
    SenkoCustomLoad();
    return gGeneration;
}

static NSUInteger IndexOfId(NSString *themeId) {
    SenkoCustomLoad();
    for (NSUInteger i = 0; i < [gThemes count]; ++i) {
        if ([[[gThemes objectAtIndex:i] objectForKey:@"id"] isEqualToString:themeId])
            return i;
    }
    return NSNotFound;
}

/* ---- drafts ----------------------------------------------------------- */

/* seeding reads a builtin family by running its fills, so the draft starts from
   real colors; the live palette is captured and restored around it */
static NSMutableDictionary *PaletteSnapshot(void) {
    NSMutableDictionary *p = [NSMutableDictionary dictionaryWithCapacity:SenkoSlotCount];
    for (int i = 0; i < SenkoSlotCount; ++i) {
        UIColor **ref = SlotRef((SenkoThemeSlot)i);
        if (!ref) continue;
        [p setObject:SenkoHexFromColor(*ref) forKey:SenkoThemeSlotKey((SenkoThemeSlot)i)];
    }
    return p;
}

NSMutableDictionary *SenkoCustomDraftFromTheme(NSString *sourceThemeId) {
    SenkoCustomLoad();
    const SenkoThemeDef *src = SenkoThemeFind(sourceThemeId);
    if (!src) src = SenkoThemeFind(SenkoThemeDefaultId());

    UIColor *saved[SenkoSlotCount];
    for (int i = 0; i < SenkoSlotCount; ++i) {
        UIColor **ref = SlotRef((SenkoThemeSlot)i);
        saved[i] = ref ? [*ref retain] : nil;
    }

    NSMutableDictionary *light = nil;
    NSMutableDictionary *dark = nil;
    if (src && src->fillLight) {
        src->fillLight();
        light = PaletteSnapshot();
    }
    if (src && src->fillDark) {
        src->fillDark();
        dark = PaletteSnapshot();
    }

    for (int i = 0; i < SenkoSlotCount; ++i) {
        UIColor **ref = SlotRef((SenkoThemeSlot)i);
        if (ref && saved[i]) SenkoThemeSetSlot(ref, saved[i]);
        [saved[i] release];
    }

    if (!light) return nil;
    NSMutableDictionary *draft = [NSMutableDictionary dictionaryWithCapacity:6];
    NSString *base = src ? [NSString stringWithUTF8String:src->name] : @"Theme";
    [draft setObject:[NSString stringWithFormat:@"%@ copy", base] forKey:@"name"];
    [draft setObject:light forKey:@"light"];
    if (dark) [draft setObject:dark forKey:@"dark"];
    [draft setObject:[NSNumber numberWithInt:(src && (src->caps & SenkoThemeCapIos26)) ? 2
                                            : ((src && (src->caps & SenkoThemeCapFlat)) ? 1 : 0)]
              forKey:@"style"];
    [draft setObject:[NSNumber numberWithDouble:src ? (double)src->cardRadius : 12.0]
              forKey:@"radius"];
    return draft;
}

NSMutableDictionary *SenkoCustomDraftForId(NSString *themeId) {
    NSUInteger idx = IndexOfId(themeId);
    if (idx == NSNotFound) return nil;
    NSDictionary *stored = [gThemes objectAtIndex:idx];
    NSMutableDictionary *draft = SanitizeTheme(stored);
    return draft;
}

NSString *SenkoCustomDraftName(NSDictionary *draft) {
    id name = [draft objectForKey:@"name"];
    return [name isKindOfClass:[NSString class]] ? name : @"";
}

void SenkoCustomDraftSetName(NSMutableDictionary *draft, NSString *name) {
    NSString *trimmed = [name stringByTrimmingCharactersInSet:
                         [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![trimmed length]) return;
    if ([trimmed length] > 40) trimmed = [trimmed substringToIndex:40];
    [draft setObject:trimmed forKey:@"name"];
}

BOOL SenkoCustomDraftAllowsDark(NSDictionary *draft) {
    return [draft objectForKey:@"dark"] != nil;
}

UIColor *SenkoCustomDraftColor(NSDictionary *draft, BOOL light, SenkoThemeSlot slot) {
    NSDictionary *p = light ? [draft objectForKey:@"light"] : [draft objectForKey:@"dark"];
    if (!p) p = [draft objectForKey:@"light"];
    return SenkoColorFromHex([p objectForKey:SenkoThemeSlotKey(slot)]);
}

void SenkoCustomDraftSetColor(NSMutableDictionary *draft, BOOL light,
                              SenkoThemeSlot slot, UIColor *color) {
    if (!draft || !color) return;
    NSString *key = light ? @"light" : @"dark";
    NSMutableDictionary *p = [draft objectForKey:key];
    if (![p isKindOfClass:[NSMutableDictionary class]]) return;
    [p setObject:SenkoHexFromColor(color) forKey:SenkoThemeSlotKey(slot)];
}

static NSString *FreshThemeId(void) {
    double stamp = [NSDate timeIntervalSinceReferenceDate] * 1000.0;
    unsigned long long base = (unsigned long long)(stamp < 0 ? 0 : stamp);
    for (unsigned bump = 0; bump < 1000; ++bump) {
        NSString *candidate = [NSString stringWithFormat:@"custom-%llx", base + bump];
        if (IndexOfId(candidate) == NSNotFound && !SenkoThemeFind(candidate))
            return candidate;
    }
    return nil;
}

NSString *SenkoCustomSaveDraft(NSMutableDictionary *draft) {
    SenkoCustomLoad();
    NSMutableDictionary *clean = SanitizeTheme(draft);
    if (!clean) return nil;

    NSString *tid = [clean objectForKey:@"id"];
    NSUInteger existing = tid ? IndexOfId(tid) : NSNotFound;
    if (existing == NSNotFound) {
        if ([gThemes count] >= SENKO_CUSTOM_MAX) return nil;
        tid = FreshThemeId();
        if (!tid) return nil;
        [clean setObject:tid forKey:@"id"];
        [gThemes addObject:clean];
    } else {
        [gThemes replaceObjectAtIndex:existing withObject:clean];
    }
    PersistThemes();
    RebuildDefs();
    /* the def struct moved, so the live theme must be resolved again */
    SenkoThemeReapplyCurrent();
    return tid;
}

BOOL SenkoCustomDelete(NSString *themeId) {
    NSUInteger idx = IndexOfId(themeId);
    if (idx == NSNotFound) return NO;
    /* leave the theme before its def is freed, or the registry keeps a dangling
       pointer to the current family */
    if ([SenkoThemeCurrentId() isEqualToString:themeId])
        SenkoThemeApplyId(SenkoThemeDefaultId());
    [gThemes removeObjectAtIndex:idx];
    PersistThemes();
    RebuildDefs();
    SenkoThemeReapplyCurrent();
    return YES;
}

/* ---- export and import ------------------------------------------------ */

NSString *SenkoCustomExportDirectory(void) {
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                         NSUserDomainMask, YES);
    return [paths count] ? [paths objectAtIndex:0] : NSTemporaryDirectory();
}

static NSString *SafeFileName(NSString *name) {
    NSMutableString *out = [NSMutableString stringWithCapacity:[name length]];
    for (NSUInteger i = 0; i < [name length] && [out length] < 32; ++i) {
        unichar c = [name characterAtIndex:i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            [out appendFormat:@"%C", c];
        else if (c == ' ')
            [out appendString:@"-"];
    }
    return [out length] ? out : @"theme";
}

NSString *SenkoCustomExport(NSString *themeId, NSString **outError) {
    NSUInteger idx = IndexOfId(themeId);
    if (idx == NSNotFound) {
        if (outError) *outError = @"Theme is not a custom theme";
        return nil;
    }
    NSMutableDictionary *payload =
        [NSMutableDictionary dictionaryWithDictionary:[gThemes objectAtIndex:idx]];
    [payload setObject:SENKO_CUSTOM_MAGIC forKey:@"format"];

    NSString *err = nil;
    NSData *data = [NSPropertyListSerialization
        dataFromPropertyList:payload
                      format:NSPropertyListXMLFormat_v1_0
            errorDescription:&err];
    if (!data) {
        if (outError) *outError = err ? err : @"Could not serialize theme";
        [err release];
        return nil;
    }
    NSString *path = [[SenkoCustomExportDirectory()
        stringByAppendingPathComponent:SafeFileName(SenkoCustomDraftName(payload))]
        stringByAppendingPathExtension:@"senkotheme"];
    if (![data writeToFile:path atomically:YES]) {
        if (outError) *outError = @"Could not write to Documents";
        return nil;
    }
    return path;
}

NSString *SenkoCustomImportFile(NSString *path, NSString **outError) {
    SenkoCustomLoad();
    /* a theme file is small; refuse anything that is not */
    NSDictionary *attrs = [[NSFileManager defaultManager]
        attributesOfItemAtPath:path error:NULL];
    unsigned long long size = [[attrs objectForKey:NSFileSize] unsignedLongLongValue];
    if (!attrs || size == 0 || size > 65536) {
        if (outError) *outError = @"Not a Senko theme file";
        return nil;
    }
    NSData *data = [NSData dataWithContentsOfFile:path];
    if (!data) {
        if (outError) *outError = @"Could not read the file";
        return nil;
    }
    NSString *err = nil;
    NSPropertyListFormat fmt = NSPropertyListXMLFormat_v1_0;
    id plist = [NSPropertyListSerialization propertyListFromData:data
                                               mutabilityOption:NSPropertyListImmutable
                                                         format:&fmt
                                               errorDescription:&err];
    [err release];
    if (![plist isKindOfClass:[NSDictionary class]]) {
        if (outError) *outError = @"Not a Senko theme file";
        return nil;
    }
    if (![SENKO_CUSTOM_MAGIC isEqualToString:[(NSDictionary *)plist objectForKey:@"format"]]) {
        if (outError) *outError = @"Unsupported theme format";
        return nil;
    }
    NSMutableDictionary *clean = SanitizeTheme(plist);
    if (!clean) {
        if (outError) *outError = @"Theme file is incomplete";
        return nil;
    }
    if ([gThemes count] >= SENKO_CUSTOM_MAX) {
        if (outError) *outError = @"Custom theme limit reached";
        return nil;
    }
    /* an imported theme never overwrites a local one with the same id */
    [clean removeObjectForKey:@"id"];
    return SenkoCustomSaveDraft(clean);
}
