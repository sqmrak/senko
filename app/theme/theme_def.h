#ifndef SENKO_THEME_DEF_H
#define SENKO_THEME_DEF_H

#import <UIKit/UIKit.h>

/* capability bits; registry caches these after apply */
enum {
    SenkoThemeCapFlat      = 1u << 0, /* ios7/ios16 chrome */
    SenkoThemeCapIos16     = 1u << 1,
    SenkoThemeCapBoykisser = 1u << 2,
    SenkoThemeCapMiside    = 1u << 3,
    SenkoThemeCapAero      = 1u << 4,
    SenkoThemeCapLightOnly = 1u << 5,
    SenkoThemeCapDarkOnly  = 1u << 6,
    SenkoThemeCapIos26     = 1u << 7 /* glass; pairs with capios16 paths */
};

/* picker folders; themesvc groups by this id */
#define SENKO_THEME_GROUP_IOS    "ios"
#define SENKO_THEME_GROUP_CUSTOM "custom"

typedef void (*SenkoThemeFillFn)(void);

/* one theme family */
typedef struct SenkoThemeDef {
    const char *tid; /* stored id, e.g. senko-ios6 */
    const char *name; /* picker title */
    const char *blurb; /* picker subtitle */
    unsigned caps;
    CGFloat cardRadius;
    SenkoThemeFillFn fillLight;
    SenkoThemeFillFn fillDark; /* may equal filllight */
/* lowercase tokens matched on legacy prefs; space separated, or null */
    const char *aliases;
/* senko_theme_group_ios / custom / future */
    const char *group;
} SenkoThemeDef;

/* palette writers used only inside theme fills */
void SenkoThemeSetSlot(UIColor **slot, UIColor *color);
UIColor *SenkoThemeC(CGFloat r, CGFloat g, CGFloat b);
UIColor *SenkoThemeCA(CGFloat r, CGFloat g, CGFloat b, CGFloat a);

/* lookup; returns null if unknown */
const SenkoThemeDef *SenkoThemeFind(NSString *tid);
const SenkoThemeDef *SenkoThemeFindMigrated(NSString *raw, BOOL *outLight);
const SenkoThemeDef *const *SenkoThemeAllDefs(size_t *outCount);
const SenkoThemeDef *SenkoThemeCurrentDef(void);

#endif
