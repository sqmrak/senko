#import "theme_def.h"

/* themes picker order. new theme checklist: 1 */
extern const SenkoThemeDef kSenkoThemeIos6;
extern const SenkoThemeDef kSenkoThemeIos7;
extern const SenkoThemeDef kSenkoThemeIos16;
extern const SenkoThemeDef kSenkoThemeIos26;
extern const SenkoThemeDef kSenkoThemeBoykisser;
extern const SenkoThemeDef kSenkoThemeMiside;
extern const SenkoThemeDef kSenkoThemeAero;

static const SenkoThemeDef *const kTable[] = {
    &kSenkoThemeIos6,
    &kSenkoThemeIos7,
    &kSenkoThemeIos16,
    &kSenkoThemeIos26,
    &kSenkoThemeBoykisser,
    &kSenkoThemeMiside,
    &kSenkoThemeAero,
};

const SenkoThemeDef *const *SenkoThemeAllDefs(size_t *outCount) {
    if (outCount) *outCount = sizeof kTable / sizeof kTable[0];
    return kTable;
}
