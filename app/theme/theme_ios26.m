#import "theme_def.h"
#import "ui_theme.h"

/* liquid glass; wallpapers: ios26-bg-light/dark.jpg */

static void FillLight(void) {
/* pure neutral grey - no blue cast under silver wallpaper */
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.940, 0.940, 0.940));
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.900, 0.900, 0.900));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.920, 0.920, 0.920));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.200, 0.850, 0.480));
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.100, 0.680, 0.360));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeCA(1.000, 1.000, 1.000, 0.30));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeCA(1.000, 1.000, 1.000, 0.10));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.080, 0.080, 0.080));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.400, 0.400, 0.400));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.000, 0.478, 1.000));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.000, 0.360, 0.900));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeCA(1.000, 1.000, 1.000, 0.90));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeCA(1.000, 1.000, 1.000, 0.55));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeCA(1.000, 1.000, 1.000, 0.42));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeCA(1.000, 1.000, 1.000, 0.24));
    SenkoThemeSetSlot(&kWell,       SenkoThemeCA(1.000, 1.000, 1.000, 0.00));
}

static void FillDark(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.060, 0.060, 0.070));
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.020, 0.020, 0.025));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.100, 0.100, 0.110));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.220, 0.900, 0.500));
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.100, 0.700, 0.360));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeCA(1.000, 1.000, 1.000, 0.12));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeCA(1.000, 1.000, 1.000, 0.03));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(1.000, 1.000, 1.000));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.650, 0.660, 0.700));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.300, 0.640, 1.000));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.140, 0.440, 0.940));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeCA(1.000, 1.000, 1.000, 0.28));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeCA(1.000, 1.000, 1.000, 0.10));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeCA(1.000, 1.000, 1.000, 0.14));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeCA(1.000, 1.000, 1.000, 0.06));
    SenkoThemeSetSlot(&kWell,       SenkoThemeCA(0.000, 0.000, 0.000, 0.00));
}

const SenkoThemeDef kSenkoThemeIos26 = {
    "senko-ios26",
    "Senko-iOS26",
    "liquid ass... nah, glass",
    SenkoThemeCapFlat | SenkoThemeCapIos16 | SenkoThemeCapIos26,
    22.0f,
    FillLight,
    FillDark,
    "ios26 liquid glass liquidglass",
    SENKO_THEME_GROUP_IOS
};
