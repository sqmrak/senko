#import "theme_def.h"
#import "ui_theme.h"

/* sky gloss + soft glass; white only */
static void Fill(void) {
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

const SenkoThemeDef kSenkoThemeAero = {
    "senko-frutigeraero",
    "Senko-Aero",
    "futuristic maximalism of the past",
    SenkoThemeCapAero | SenkoThemeCapLightOnly,
    14.0f,
    Fill,
    Fill,
    "frutigeraero frutiger aero",
    SENKO_THEME_GROUP_CUSTOM

};
