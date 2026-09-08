#ifndef SENKO_THEME_CUSTOM_H
#define SENKO_THEME_CUSTOM_H

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#import "theme_def.h"

/* palette slots shared by storage, the editor and the export file. the order is
   the editor row order; the string keys are the stored schema and must not be
   renamed once a user has saved a theme */
typedef enum {
    SenkoSlotBG = 0,
    SenkoSlotBGBot,
    SenkoSlotFelt,
    SenkoSlotConnOn,
    SenkoSlotConnOnLo,
    SenkoSlotIdleGrey,
    SenkoSlotIdleGreyLo,
    SenkoSlotInk,
    SenkoSlotInkMuted,
    SenkoSlotAccent,
    SenkoSlotAccentLo,
    SenkoSlotChromeHi,
    SenkoSlotChromeLo,
    SenkoSlotCellHi,
    SenkoSlotCellLo,
    SenkoSlotWell,
    SenkoSlotCount
} SenkoThemeSlot;

NSString *SenkoThemeSlotKey(SenkoThemeSlot slot);
NSString *SenkoThemeSlotTitle(SenkoThemeSlot slot);
NSString *SenkoThemeSlotHint(SenkoThemeSlot slot);

/* registry: these defs are appended to the builtin table by SenkoThemeAllDefs.
   the generation changes whenever the set or its contents change, so callers
   that cache a merged table can tell when to rebuild it */
void     SenkoCustomLoad(void);
const SenkoThemeDef *const *SenkoCustomDefs(size_t *outCount);
unsigned SenkoCustomGeneration(void);
BOOL     SenkoCustomIsCustomId(NSString *themeId);

/* a draft is a mutable dictionary in the stored schema; the editor mutates one
   and either saves it or throws it away */
NSMutableDictionary *SenkoCustomDraftFromTheme(NSString *sourceThemeId);
NSMutableDictionary *SenkoCustomDraftForId(NSString *themeId);
NSString *SenkoCustomDraftName(NSDictionary *draft);
void      SenkoCustomDraftSetName(NSMutableDictionary *draft, NSString *name);
UIColor  *SenkoCustomDraftColor(NSDictionary *draft, BOOL light, SenkoThemeSlot slot);
void      SenkoCustomDraftSetColor(NSMutableDictionary *draft, BOOL light,
                                   SenkoThemeSlot slot, UIColor *color);
BOOL      SenkoCustomDraftAllowsDark(NSDictionary *draft);

/* save returns the stored theme id, or nil when the draft is unusable */
NSString *SenkoCustomSaveDraft(NSMutableDictionary *draft);
BOOL      SenkoCustomDelete(NSString *themeId);

/* export writes Documents/<name>.senkotheme and returns its path */
NSString *SenkoCustomExport(NSString *themeId, NSString **outError);
/* import validates a staged file and returns the new theme id */
NSString *SenkoCustomImportFile(NSString *path, NSString **outError);
NSString *SenkoCustomExportDirectory(void);

NSString *SenkoHexFromColor(UIColor *color);
UIColor  *SenkoColorFromHex(NSString *hex);

#endif
