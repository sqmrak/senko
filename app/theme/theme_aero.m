#import "theme_def.h"
#import "ui_theme.h"

/* sky gloss + soft glass */
static void FillLight(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.720, 0.900, 0.980)); /* sky */
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.900, 0.970, 0.920)); /* soft green haze */
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.850, 0.940, 0.990));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.000, 0.720, 0.920)); /* cyan on */
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.000, 0.520, 0.780));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.700, 0.820, 0.900));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.520, 0.640, 0.740));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.050, 0.220, 0.360)); /* navy ink */
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.280, 0.450, 0.560));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.000, 0.660, 0.920)); /* aero cyan */
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.000, 0.480, 0.780));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.550, 0.880, 1.000));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.150, 0.620, 0.880));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeCA(1.000, 1.000, 1.000, 0.92));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeCA(0.920, 0.970, 1.000, 0.90));
    SenkoThemeSetSlot(&kWell,       SenkoThemeCA(1.000, 1.000, 1.000, 0.28)); /* soft glass well */
}

/* the same glass at night: deep navy sky, cold blue glow, translucent plates */
static void FillDark(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.020, 0.050, 0.200)); /* night sky */
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.008, 0.026, 0.110));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.040, 0.090, 0.280));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.200, 0.640, 1.000)); /* moonlit blue on */
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.080, 0.440, 0.880));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.240, 0.340, 0.540));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.120, 0.200, 0.380));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.880, 0.940, 1.000));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.560, 0.690, 0.880));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.320, 0.700, 1.000));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.120, 0.480, 0.900));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.220, 0.460, 0.860));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.080, 0.240, 0.580));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeCA(0.070, 0.130, 0.320, 0.82));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeCA(0.040, 0.090, 0.240, 0.80));
    SenkoThemeSetSlot(&kWell,       SenkoThemeCA(0.000, 0.020, 0.100, 0.45));
}

const SenkoThemeDef kSenkoThemeAero = {
    "senko-frutigeraero",
    "Senko-Aero",
    "futuristic maximalism of the past",
    SenkoThemeCapAero,
    14.0f,
    FillLight,
    FillDark,
    "frutigeraero frutiger aero",
    SENKO_THEME_GROUP_CUSTOM

};
