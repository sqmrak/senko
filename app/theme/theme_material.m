#import "theme_def.h"
#import "ui_theme.h"

/* material design baseline: a #fafafa page, white surfaces that rise on
   shadows, one primary colour and a teal secondary */
static void FillLight(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.980, 0.980, 0.980)); /* #fafafa */
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.980, 0.980, 0.980));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.961, 0.961, 0.961)); /* #f5f5f5 */
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.012, 0.855, 0.776)); /* #03dac6 */
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.000, 0.737, 0.647));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.741, 0.741, 0.741)); /* #bdbdbd */
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.620, 0.620, 0.620)); /* #9e9e9e */
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.129, 0.129, 0.129)); /* #212121 */
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.459, 0.459, 0.459)); /* #757575 */
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.384, 0.000, 0.933)); /* #6200ee */
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.216, 0.000, 0.702)); /* #3700b3 */
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.384, 0.000, 0.933));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.216, 0.000, 0.702));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeC(1.000, 1.000, 1.000));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeC(1.000, 1.000, 1.000));
    SenkoThemeSetSlot(&kWell,       SenkoThemeC(0.933, 0.933, 0.933)); /* #eeeeee */
}

/* the dark baseline: #121212 and surfaces a step lighter, the lighter 200 tint
   of the primary, because a saturated purple vibrates on near black */
static void FillDark(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.071, 0.071, 0.071)); /* #121212 */
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.071, 0.071, 0.071));
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.118, 0.118, 0.118)); /* #1e1e1e */
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.012, 0.855, 0.776));
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.004, 0.529, 0.525)); /* #018786 */
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.300, 0.300, 0.300));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.220, 0.220, 0.220));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.930, 0.930, 0.930)); /* 87% white */
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.620, 0.620, 0.620)); /* 60% white */
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.733, 0.525, 0.988)); /* #bb86fc */
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.596, 0.369, 0.988));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.122, 0.122, 0.122)); /* #1f1f1f bar */
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.098, 0.098, 0.098));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeC(0.133, 0.133, 0.133)); /* surface + 5% white */
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeC(0.118, 0.118, 0.118));
    SenkoThemeSetSlot(&kWell,       SenkoThemeC(0.075, 0.075, 0.075));
}

const SenkoThemeDef kSenkoThemeMaterial = {
    "senko-material",
    "Senko-Material",
    "google pixel aesthetic",
    SenkoThemeCapFlat | SenkoThemeCapMaterial,
    8.0f,
    FillLight,
    FillDark,
    "material",
    SENKO_THEME_GROUP_CUSTOM
};
