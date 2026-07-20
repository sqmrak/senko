#import "theme_def.h"
#import "ui_theme.h"

static void FillLight(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.870, 0.820, 0.960)); /* lavender */
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.720, 0.880, 0.990)); /* sky */
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.980, 0.860, 0.920)); /* blush mid (wallpaper) */
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.204, 0.780, 0.349)); /* 34c759 */
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.140, 0.640, 0.280));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.780, 0.800, 0.840));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.620, 0.650, 0.700));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(0.050, 0.050, 0.070));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.380, 0.380, 0.430));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.000, 0.478, 1.000)); /* 007aff */
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.000, 0.360, 0.880));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.000, 0.478, 1.000));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.000, 0.360, 0.880));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeCA(1.000, 1.000, 1.000, 0.94));
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeCA(0.980, 0.985, 1.000, 0.90));
    SenkoThemeSetSlot(&kWell,       SenkoThemeCA(1.000, 1.000, 1.000, 0.00)); /* list floats on wallpaper */
}

static void FillDark(void) {
    SenkoThemeSetSlot(&kBG,         SenkoThemeC(0.040, 0.020, 0.100)); /* deep purple night */
    SenkoThemeSetSlot(&kBGBot,      SenkoThemeC(0.000, 0.000, 0.020)); /* oled black */
    SenkoThemeSetSlot(&kFelt,       SenkoThemeC(0.100, 0.060, 0.180));
    SenkoThemeSetSlot(&kConnOn,     SenkoThemeC(0.188, 0.820, 0.345)); /* 30d158 */
    SenkoThemeSetSlot(&kConnOnLo,   SenkoThemeC(0.120, 0.620, 0.260));
    SenkoThemeSetSlot(&kIdleGrey,   SenkoThemeC(0.320, 0.330, 0.380));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.200, 0.210, 0.250));
    SenkoThemeSetSlot(&kInk,        SenkoThemeC(1.000, 1.000, 1.000));
    SenkoThemeSetSlot(&kInkMuted,   SenkoThemeC(0.620, 0.620, 0.680));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.040, 0.520, 1.000)); /* 0a84ff */
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.000, 0.400, 0.880));
    SenkoThemeSetSlot(&kChromeHi,   SenkoThemeC(0.040, 0.520, 1.000));
    SenkoThemeSetSlot(&kChromeLo,   SenkoThemeC(0.000, 0.400, 0.880));
    SenkoThemeSetSlot(&kCellHi,     SenkoThemeCA(0.140, 0.140, 0.160, 0.92)); /* elevated material */
    SenkoThemeSetSlot(&kCellLo,     SenkoThemeCA(0.100, 0.100, 0.120, 0.90));
    SenkoThemeSetSlot(&kWell,       SenkoThemeCA(0.000, 0.000, 0.000, 0.00));
}

const SenkoThemeDef kSenkoThemeIos16 = {
    "senko-ios16",
    "Senko-iOS16",
    "modern theme",
    SenkoThemeCapFlat | SenkoThemeCapIos16,
    16.0f,
    FillLight,
    FillDark,
    "ios16",
    SENKO_THEME_GROUP_IOS
};
