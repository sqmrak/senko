#import "theme_def.h"
#import "ui_theme.h"

static void FillMaterialYouLight(void) {
    SenkoThemeSetSlot(&kBG, SenkoThemeC(0.996f, 0.969f, 1.000f));
    SenkoThemeSetSlot(&kBGBot, SenkoThemeC(0.969f, 0.949f, 0.980f));
    SenkoThemeSetSlot(&kFelt, SenkoThemeC(0.953f, 0.929f, 0.969f));
    SenkoThemeSetSlot(&kConnOn, SenkoThemeC(0.231f, 0.710f, 0.369f));
    SenkoThemeSetSlot(&kConnOnLo, SenkoThemeC(0.165f, 0.561f, 0.278f));
    SenkoThemeSetSlot(&kIdleGrey, SenkoThemeC(0.475f, 0.455f, 0.494f));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.384f, 0.357f, 0.443f));
    SenkoThemeSetSlot(&kInk, SenkoThemeC(0.114f, 0.106f, 0.125f));
    SenkoThemeSetSlot(&kInkMuted, SenkoThemeC(0.286f, 0.271f, 0.310f));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.404f, 0.314f, 0.643f));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.310f, 0.216f, 0.545f));
    SenkoThemeSetSlot(&kChromeHi, SenkoThemeC(0.918f, 0.867f, 1.000f));
    SenkoThemeSetSlot(&kChromeLo, SenkoThemeC(0.910f, 0.871f, 0.973f));
    SenkoThemeSetSlot(&kCellHi, SenkoThemeC(0.925f, 0.902f, 0.941f));
    SenkoThemeSetSlot(&kCellLo, SenkoThemeC(0.906f, 0.878f, 0.925f));
    SenkoThemeSetSlot(&kWell, SenkoThemeC(0.902f, 0.878f, 0.914f));
}

static void FillMaterialYouDark(void) {
    SenkoThemeSetSlot(&kBG, SenkoThemeC(0.078f, 0.071f, 0.094f));
    SenkoThemeSetSlot(&kBGBot, SenkoThemeC(0.059f, 0.051f, 0.075f));
    SenkoThemeSetSlot(&kFelt, SenkoThemeC(0.129f, 0.122f, 0.149f));
    SenkoThemeSetSlot(&kConnOn, SenkoThemeC(0.435f, 0.863f, 0.549f));
    SenkoThemeSetSlot(&kConnOnLo, SenkoThemeC(0.247f, 0.682f, 0.376f));
    SenkoThemeSetSlot(&kIdleGrey, SenkoThemeC(0.576f, 0.561f, 0.600f));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.478f, 0.459f, 0.498f));
    SenkoThemeSetSlot(&kInk, SenkoThemeC(0.902f, 0.878f, 0.914f));
    SenkoThemeSetSlot(&kInkMuted, SenkoThemeC(0.792f, 0.769f, 0.816f));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.816f, 0.737f, 1.000f));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.714f, 0.616f, 0.973f));
    SenkoThemeSetSlot(&kChromeHi, SenkoThemeC(0.310f, 0.216f, 0.545f));
    SenkoThemeSetSlot(&kChromeLo, SenkoThemeC(0.220f, 0.118f, 0.447f));
    SenkoThemeSetSlot(&kCellHi, SenkoThemeC(0.169f, 0.161f, 0.188f));
    SenkoThemeSetSlot(&kCellLo, SenkoThemeC(0.145f, 0.137f, 0.161f));
    SenkoThemeSetSlot(&kWell, SenkoThemeC(0.212f, 0.204f, 0.231f));
}

const SenkoThemeDef kSenkoThemeMaterialYou = {
    "senko-material-you",
    "Senko-Material You",
    "the colours of material you",
    SenkoThemeCapFlat | SenkoThemeCapMaterial,
    28.0f,
    FillMaterialYouLight,
    FillMaterialYouDark,
    "material you",
    SENKO_THEME_GROUP_ANDROID
};

static void FillMaterial5Light(void) {
    SenkoThemeSetSlot(&kBG, SenkoThemeC(0.980f, 0.980f, 0.980f));
    SenkoThemeSetSlot(&kBGBot, SenkoThemeC(0.980f, 0.980f, 0.980f));
    SenkoThemeSetSlot(&kFelt, SenkoThemeC(0.933f, 0.933f, 0.933f));
    SenkoThemeSetSlot(&kConnOn, SenkoThemeC(0.298f, 0.686f, 0.314f));
    SenkoThemeSetSlot(&kConnOnLo, SenkoThemeC(0.220f, 0.557f, 0.235f));
    SenkoThemeSetSlot(&kIdleGrey, SenkoThemeC(0.620f, 0.620f, 0.620f));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.459f, 0.459f, 0.459f));
    SenkoThemeSetSlot(&kInk, SenkoThemeC(0.129f, 0.129f, 0.129f));
    SenkoThemeSetSlot(&kInkMuted, SenkoThemeC(0.459f, 0.459f, 0.459f));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.000f, 0.588f, 0.533f));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.000f, 0.475f, 0.420f));
    SenkoThemeSetSlot(&kChromeHi, SenkoThemeC(0.000f, 0.588f, 0.533f));
    SenkoThemeSetSlot(&kChromeLo, SenkoThemeC(0.000f, 0.537f, 0.482f));
    SenkoThemeSetSlot(&kCellHi, SenkoThemeC(1.000f, 1.000f, 1.000f));
    SenkoThemeSetSlot(&kCellLo, SenkoThemeC(1.000f, 1.000f, 1.000f));
    SenkoThemeSetSlot(&kWell, SenkoThemeC(0.878f, 0.878f, 0.878f));
}

static void FillMaterial5Dark(void) {
    SenkoThemeSetSlot(&kBG, SenkoThemeC(0.188f, 0.188f, 0.188f));
    SenkoThemeSetSlot(&kBGBot, SenkoThemeC(0.188f, 0.188f, 0.188f));
    SenkoThemeSetSlot(&kFelt, SenkoThemeC(0.259f, 0.259f, 0.259f));
    SenkoThemeSetSlot(&kConnOn, SenkoThemeC(0.400f, 0.733f, 0.416f));
    SenkoThemeSetSlot(&kConnOnLo, SenkoThemeC(0.263f, 0.627f, 0.278f));
    SenkoThemeSetSlot(&kIdleGrey, SenkoThemeC(0.620f, 0.620f, 0.620f));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.459f, 0.459f, 0.459f));
    SenkoThemeSetSlot(&kInk, SenkoThemeC(1.000f, 1.000f, 1.000f));
    SenkoThemeSetSlot(&kInkMuted, SenkoThemeC(0.702f, 0.702f, 0.702f));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.302f, 0.714f, 0.675f));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.000f, 0.588f, 0.533f));
    SenkoThemeSetSlot(&kChromeHi, SenkoThemeC(0.129f, 0.129f, 0.129f));
    SenkoThemeSetSlot(&kChromeLo, SenkoThemeC(0.102f, 0.102f, 0.102f));
    SenkoThemeSetSlot(&kCellHi, SenkoThemeC(0.259f, 0.259f, 0.259f));
    SenkoThemeSetSlot(&kCellLo, SenkoThemeC(0.259f, 0.259f, 0.259f));
    SenkoThemeSetSlot(&kWell, SenkoThemeC(0.169f, 0.169f, 0.169f));
}

const SenkoThemeDef kSenkoThemeMaterial5 = {
    "senko-material5",
    "Senko-Material Design",
    "the look of android 5 and 6",
    SenkoThemeCapFlat | SenkoThemeCapMaterial,
    2.0f,
    FillMaterial5Light,
    FillMaterial5Dark,
    "material design lollipop marshmallow",
    SENKO_THEME_GROUP_ANDROID
};

static void FillHoloLight(void) {
    SenkoThemeSetSlot(&kBG, SenkoThemeC(1.000f, 1.000f, 1.000f));
    SenkoThemeSetSlot(&kBGBot, SenkoThemeC(1.000f, 1.000f, 1.000f));
    SenkoThemeSetSlot(&kFelt, SenkoThemeC(0.953f, 0.953f, 0.953f));
    SenkoThemeSetSlot(&kConnOn, SenkoThemeC(0.600f, 0.800f, 0.000f));
    SenkoThemeSetSlot(&kConnOnLo, SenkoThemeC(0.400f, 0.600f, 0.000f));
    SenkoThemeSetSlot(&kIdleGrey, SenkoThemeC(0.733f, 0.733f, 0.733f));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.600f, 0.600f, 0.600f));
    SenkoThemeSetSlot(&kInk, SenkoThemeC(0.200f, 0.200f, 0.200f));
    SenkoThemeSetSlot(&kInkMuted, SenkoThemeC(0.467f, 0.467f, 0.467f));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.200f, 0.710f, 0.898f));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.000f, 0.600f, 0.800f));
    SenkoThemeSetSlot(&kChromeHi, SenkoThemeC(0.953f, 0.953f, 0.953f));
    SenkoThemeSetSlot(&kChromeLo, SenkoThemeC(0.863f, 0.863f, 0.863f));
    SenkoThemeSetSlot(&kCellHi, SenkoThemeC(0.984f, 0.984f, 0.984f));
    SenkoThemeSetSlot(&kCellLo, SenkoThemeC(0.941f, 0.941f, 0.941f));
    SenkoThemeSetSlot(&kWell, SenkoThemeC(0.878f, 0.878f, 0.878f));
}

static void FillHoloDark(void) {
    SenkoThemeSetSlot(&kBG, SenkoThemeC(0.000f, 0.000f, 0.000f));
    SenkoThemeSetSlot(&kBGBot, SenkoThemeC(0.000f, 0.000f, 0.000f));
    SenkoThemeSetSlot(&kFelt, SenkoThemeC(0.067f, 0.067f, 0.067f));
    SenkoThemeSetSlot(&kConnOn, SenkoThemeC(0.600f, 0.800f, 0.000f));
    SenkoThemeSetSlot(&kConnOnLo, SenkoThemeC(0.400f, 0.600f, 0.000f));
    SenkoThemeSetSlot(&kIdleGrey, SenkoThemeC(0.333f, 0.333f, 0.333f));
    SenkoThemeSetSlot(&kIdleGreyLo, SenkoThemeC(0.200f, 0.200f, 0.200f));
    SenkoThemeSetSlot(&kInk, SenkoThemeC(1.000f, 1.000f, 1.000f));
    SenkoThemeSetSlot(&kInkMuted, SenkoThemeC(0.600f, 0.600f, 0.600f));
    SenkoThemeSetSlot(&kAccentBlue, SenkoThemeC(0.200f, 0.710f, 0.898f));
    SenkoThemeSetSlot(&kAccentBlueLo, SenkoThemeC(0.000f, 0.600f, 0.800f));
    SenkoThemeSetSlot(&kChromeHi, SenkoThemeC(0.227f, 0.227f, 0.227f));
    SenkoThemeSetSlot(&kChromeLo, SenkoThemeC(0.149f, 0.149f, 0.149f));
    SenkoThemeSetSlot(&kCellHi, SenkoThemeC(0.133f, 0.133f, 0.133f));
    SenkoThemeSetSlot(&kCellLo, SenkoThemeC(0.102f, 0.102f, 0.102f));
    SenkoThemeSetSlot(&kWell, SenkoThemeC(0.200f, 0.200f, 0.200f));
}

const SenkoThemeDef kSenkoThemeHolo = {
    "senko-holo",
    "Senko-Holo",
    "the look of android 4",
    SenkoThemeCapFlat,
    2.0f,
    FillHoloLight,
    FillHoloDark,
    "holo ics jelly bean kitkat",
    SENKO_THEME_GROUP_ANDROID
};
