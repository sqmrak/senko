#import "theme_def.h"
#import "ui_theme.h"

static void FillLight(void) {
/* ios6 light: clean linen, not muddy beige */
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.965, 0.960, 0.945));
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.900, 0.890, 0.860));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.940, 0.930, 0.910));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(1.000, 0.620, 0.220));
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.900, 0.480, 0.140));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.780, 0.770, 0.750)); /* off disc readable on white */
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.620, 0.600, 0.580));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.120, 0.100, 0.080));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.450, 0.400, 0.350));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.950, 0.480, 0.120));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.820, 0.380, 0.080));
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
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(1.00, 0.58, 0.12));
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.82, 0.36, 0.04));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.42, 0.42, 0.45));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.22, 0.22, 0.24));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(1.00, 0.92, 0.82));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.72, 0.66, 0.58));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(1.00, 0.55, 0.10));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.78, 0.32, 0.04));
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
