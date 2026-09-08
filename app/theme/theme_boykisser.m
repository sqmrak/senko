#import "theme_def.h"
#import "ui_theme.h"

static void FillLight(void) {
/* boykisser palette: pink paper */
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

/* same pink at the same hue, laid on rose ink instead of paper. the ground
   stays warm rather than purple so the family does not read as miside, and the
   white sprite keeps its contrast without a second artwork */
static void FillDark(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.115, 0.070, 0.095));
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.055, 0.028, 0.048));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.155, 0.095, 0.130));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(1.000, 0.420, 0.700));
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.860, 0.230, 0.520));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.430, 0.310, 0.380));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.250, 0.165, 0.215));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(1.000, 0.930, 0.955));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.780, 0.630, 0.700));
/* the accent is lifted a little: the light pink loses its edge on rose ink */
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(1.000, 0.460, 0.720));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.890, 0.280, 0.580));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(1.000, 0.500, 0.760));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.870, 0.260, 0.560));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeC(0.200, 0.125, 0.165));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeC(0.135, 0.080, 0.115));
    SenkoThemeSetSlot(&kWell,       SenkoThemeC(0.075, 0.040, 0.062));
}

const SenkoThemeDef kSenkoThemeBoykisser = {
    "senko-boykisser",
    "Senko-Boykisser",
    "meeeeeow :3",
    SenkoThemeCapBoykisser,
    12.0f,
    FillLight,
    FillDark,
    "boykisser",
    SENKO_THEME_GROUP_CUSTOM

};
