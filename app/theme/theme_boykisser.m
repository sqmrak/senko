#import "theme_def.h"
#import "ui_theme.h"

static void Fill(void) {
/* boykisser palette (light only) */
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(1.000, 0.940, 0.960));
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(1.000, 0.860, 0.910));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(1.000, 0.920, 0.945));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(1.000, 0.420, 0.700));
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.900, 0.280, 0.560));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.820, 0.720, 0.780));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.680, 0.560, 0.640));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.280, 0.140, 0.220));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.620, 0.420, 0.520));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(1.000, 0.380, 0.680));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.900, 0.250, 0.560));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(1.000, 0.550, 0.780));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.950, 0.350, 0.620));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeC(1.000, 0.980, 0.990));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeC(1.000, 0.930, 0.960));
    SenkoThemeSetSlot(&kWell,       SenkoThemeC(1.000, 0.900, 0.935));
}

const SenkoThemeDef kSenkoThemeBoykisser = {
    "senko-boykisser",
    "Senko-Boykisser",
    "meeeeeow :3",
    SenkoThemeCapBoykisser | SenkoThemeCapLightOnly,
    12.0f,
    Fill,
    Fill,
    "boykisser",
    SENKO_THEME_GROUP_CUSTOM

};
