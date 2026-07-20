#import "theme_def.h"
#import "ui_theme.h"

static void Fill(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.160, 0.040, 0.160)); /* deep purple */
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.060, 0.010, 0.070)); /* near-black plum */
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.220, 0.070, 0.220));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(1.000, 0.360, 0.700)); /* hot pink heart */
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.880, 0.160, 0.520));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.480, 0.280, 0.480));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.320, 0.160, 0.340));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(1.000, 0.960, 0.980));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.780, 0.620, 0.760));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(1.000, 0.380, 0.720));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.900, 0.220, 0.560));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(1.000, 0.480, 0.760));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.860, 0.220, 0.560));
/* dark slate cards like in-game ui */
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeC(0.140, 0.120, 0.220));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeC(0.090, 0.070, 0.160));
    SenkoThemeSetSlot(&kWell,       SenkoThemeC(0.080, 0.030, 0.100));
}

const SenkoThemeDef kSenkoThemeMiside = {
    "senko-miside",
    "Senko-Miside",
    "hehehe mita hehehe miside",
    SenkoThemeCapMiside | SenkoThemeCapDarkOnly,
    14.0f,
    Fill,
    Fill,
    "miside mita",
    SENKO_THEME_GROUP_CUSTOM

};
