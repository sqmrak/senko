#import "theme_def.h"
#import "theme_custom.h"

#include <stdlib.h>
#include <string.h>

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

/* user themes join the builtin families here, so lookup, grouping, search and
   apply stay one code path */
const SenkoThemeDef *const *SenkoThemeAllDefs(size_t *outCount) {
    static const SenkoThemeDef **merged;
    static size_t mergedCount;
    static unsigned mergedGen;

    const size_t base = sizeof kTable / sizeof kTable[0];
    size_t custom = 0;
    const SenkoThemeDef *const *customDefs = SenkoCustomDefs(&custom);
    unsigned gen = SenkoCustomGeneration();

    if (custom == 0) {
        if (outCount) *outCount = base;
        return kTable;
    }
    if (!merged || gen != mergedGen) {
        const SenkoThemeDef **next =
            (const SenkoThemeDef **)malloc((base + custom) * sizeof *next);
        if (!next) {
            if (outCount) *outCount = base;
            return kTable;
        }
        memcpy(next, kTable, base * sizeof *next);
        memcpy(next + base, customDefs, custom * sizeof *next);
        free(merged);
        merged = next;
        mergedCount = base + custom;
        mergedGen = gen;
    }
    if (outCount) *outCount = mergedCount;
    return merged;
}
