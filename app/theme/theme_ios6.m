#import "theme_def.h"
#import "ui_theme.h"

#define SENKO_IOS6_ACCENT_KEY @"senko.ios6.accent"

/* index 0 is orange, the colour the theme always had */
typedef struct {
    CGFloat connOn[3], connOnLo[3], idle[3], idleLo[3], accent[3], accentLo[3];
} Ios6Accent;

static const Ios6Accent kLight[SENKO_IOS6_ACCENT_COUNT] = {
    { {1.000f,0.620f,0.220f}, {0.900f,0.480f,0.140f}, {0.980f,0.780f,0.580f}, {0.920f,0.660f,0.420f}, {0.950f,0.480f,0.120f}, {0.820f,0.380f,0.080f} },
    { {0.950f,0.300f,0.260f}, {0.800f,0.180f,0.160f}, {0.980f,0.680f,0.650f}, {0.900f,0.520f,0.500f}, {0.880f,0.220f,0.200f}, {0.720f,0.140f,0.120f} },
    { {0.250f,0.550f,0.980f}, {0.140f,0.400f,0.850f}, {0.660f,0.800f,0.980f}, {0.500f,0.660f,0.900f}, {0.150f,0.450f,0.920f}, {0.080f,0.330f,0.780f} },
    { {0.300f,0.780f,0.340f}, {0.180f,0.620f,0.240f}, {0.700f,0.900f,0.700f}, {0.540f,0.780f,0.560f}, {0.200f,0.660f,0.280f}, {0.120f,0.520f,0.200f} },
    { {0.980f,0.820f,0.180f}, {0.880f,0.680f,0.080f}, {0.990f,0.920f,0.620f}, {0.920f,0.820f,0.420f}, {0.900f,0.700f,0.060f}, {0.760f,0.560f,0.040f} },
    { {0.660f,0.400f,0.920f}, {0.520f,0.260f,0.800f}, {0.800f,0.700f,0.940f}, {0.680f,0.550f,0.860f}, {0.580f,0.320f,0.860f}, {0.460f,0.220f,0.720f} }
};

static const Ios6Accent kDark[SENKO_IOS6_ACCENT_COUNT] = {
    { {1.00f,0.58f,0.12f}, {0.82f,0.36f,0.04f}, {0}, {0}, {1.00f,0.55f,0.10f}, {0.78f,0.32f,0.04f} },
    { {1.00f,0.36f,0.32f}, {0.80f,0.16f,0.14f}, {0}, {0}, {1.00f,0.34f,0.30f}, {0.74f,0.14f,0.12f} },
    { {0.30f,0.60f,1.00f}, {0.10f,0.36f,0.80f}, {0}, {0}, {0.28f,0.58f,1.00f}, {0.08f,0.32f,0.76f} },
    { {0.32f,0.82f,0.38f}, {0.14f,0.58f,0.22f}, {0}, {0}, {0.30f,0.80f,0.36f}, {0.12f,0.54f,0.20f} },
    { {1.00f,0.86f,0.22f}, {0.80f,0.62f,0.06f}, {0}, {0}, {1.00f,0.84f,0.20f}, {0.78f,0.60f,0.05f} },
    { {0.70f,0.46f,0.96f}, {0.50f,0.26f,0.78f}, {0}, {0}, {0.68f,0.44f,0.95f}, {0.46f,0.24f,0.74f} }
};

static UIColor *Pick(const CGFloat *c) {
    return SenkoThemeC(c[0], c[1], c[2]);
}

NSInteger SenkoIos6Accent(void) {
    NSInteger value = [[NSUserDefaults standardUserDefaults] integerForKey:SENKO_IOS6_ACCENT_KEY];
    return value >= 0 && value < SENKO_IOS6_ACCENT_COUNT ? value : 0;
}

void SenkoIos6SetAccent(NSInteger index) {
    if (index < 0 || index >= SENKO_IOS6_ACCENT_COUNT || index == SenkoIos6Accent()) return;
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:index forKey:SENKO_IOS6_ACCENT_KEY];
    [defaults synchronize];
    SenkoThemeReapplyCurrent();
}

UIColor *SenkoIos6AccentSwatch(NSInteger index) {
    if (index < 0 || index >= SENKO_IOS6_ACCENT_COUNT) index = 0;
    const CGFloat *c = kLight[index].accent;
    return [UIColor colorWithRed:c[0] green:c[1] blue:c[2] alpha:1.0f];
}

static void FillLight(void) {
/* ios6 light: clean linen, not muddy beige */
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.965, 0.960, 0.945));
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.900, 0.890, 0.860));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.940, 0.930, 0.910));
    const Ios6Accent *a = &kLight[SenkoIos6Accent()];
    SenkoThemeSetSlot(&kConnOn,     Pick(a->connOn));
    SenkoThemeSetSlot(&kConnOnLo,   Pick(a->connOnLo));
    SenkoThemeSetSlot(&kIdleGrey,   Pick(a->idle)); /* tinted, not the flat grey disc */
    SenkoThemeSetSlot(&kIdleGreyLo, Pick(a->idleLo));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.120, 0.100, 0.080));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.450, 0.400, 0.350));
    SenkoThemeSetSlot(&kAccentBlue, Pick(a->accent));
    SenkoThemeSetSlot(&kAccentBlueLo, Pick(a->accentLo));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.940, 0.920, 0.880));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.780, 0.750, 0.700));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeC(1.000, 0.995, 0.985));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeC(0.955, 0.945, 0.920));
    SenkoThemeSetSlot(&kWell,       SenkoThemeC(0.920, 0.910, 0.885));
}

static void FillDark(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.08, 0.08, 0.09));
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.03, 0.03, 0.04));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.12, 0.12, 0.13));
    const Ios6Accent *a = &kDark[SenkoIos6Accent()];
    SenkoThemeSetSlot(&kConnOn,     Pick(a->connOn));
    SenkoThemeSetSlot(&kConnOnLo,   Pick(a->connOnLo));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.42, 0.42, 0.45));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.22, 0.22, 0.24));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(1.00, 0.92, 0.82));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.72, 0.66, 0.58));
    SenkoThemeSetSlot(&kAccentBlue, Pick(a->accent));
    SenkoThemeSetSlot(&kAccentBlueLo, Pick(a->accentLo));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.38, 0.36, 0.34));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.16, 0.15, 0.14));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeC(0.28, 0.26, 0.24));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeC(0.14, 0.13, 0.12));
    SenkoThemeSetSlot(&kWell,       SenkoThemeC(0.06, 0.06, 0.07));
}

const SenkoThemeDef kSenkoThemeIos6 = {
    "senko-ios6",
    "Senko-iOS6",
    "the legacy theme for the legacy community",
    0,
    10.0f,
    FillLight,
    FillDark,
    "ios6 white dark light",
    SENKO_THEME_GROUP_IOS

};
