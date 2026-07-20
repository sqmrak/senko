#import "theme_def.h"
#import "ui_theme.h"

static void FillLight(void) {
/* ios7 light palette */
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.890, 0.900, 0.930));
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.820, 0.840, 0.890));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.910, 0.920, 0.945));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.298, 0.851, 0.392));
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.220, 0.720, 0.320));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.700, 0.720, 0.760));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.520, 0.540, 0.580));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.110, 0.130, 0.180));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.420, 0.450, 0.520));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.000, 0.478, 1.000)); /* system blue */
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.000, 0.380, 0.850));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.000, 0.478, 1.000));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.000, 0.400, 0.900));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeC(0.965, 0.970, 0.985));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeC(0.930, 0.940, 0.965));
    SenkoThemeSetSlot(&kWell,       SenkoThemeC(0.860, 0.880, 0.920));
}

static void FillDark(void) {
/* ios7 dark palette */
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.090, 0.100, 0.140));
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.050, 0.055, 0.090));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.130, 0.140, 0.185));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.188, 0.820, 0.345));
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.140, 0.650, 0.270));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.400, 0.420, 0.480));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.280, 0.300, 0.360));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.930, 0.940, 0.970));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.580, 0.600, 0.680));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.250, 0.580, 1.000));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.100, 0.420, 0.900));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.250, 0.580, 1.000));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.100, 0.420, 0.900));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeC(0.160, 0.175, 0.230));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeC(0.120, 0.130, 0.180));
    SenkoThemeSetSlot(&kWell,       SenkoThemeC(0.080, 0.090, 0.130));
}

const SenkoThemeDef kSenkoThemeIos7 = {
    "senko-ios7",
    "Senko-iOS7",
    "flat and transparent",
    SenkoThemeCapFlat,
    12.0f,
    FillLight,
    FillDark,
    "ios7",
    SENKO_THEME_GROUP_IOS

};
