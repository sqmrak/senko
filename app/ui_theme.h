#ifndef SENKO_UI_THEME_H
#define SENKO_UI_THEME_H

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

/* filled by initpalette / applycurrentpalette */
extern UIColor *kBG;
extern UIColor *kBGBot;
extern UIColor *kFelt;
extern UIColor *kConnOn;
extern UIColor *kConnOnLo;
extern UIColor *kIdleGrey;
extern UIColor *kIdleGreyLo;
extern UIColor *kInk;
extern UIColor *kInkMuted;
extern UIColor *kAccentBlue;
extern UIColor *kAccentBlueLo;
extern UIColor *kChromeHi;
extern UIColor *kChromeLo;
extern UIColor *kCellHi;
extern UIColor *kCellLo;
extern UIColor *kWell;

/* boot defaults before prefs load */
void InitPalette(void);

/* family id stored in nsuserdefaults */
NSString *SenkoThemeDefaultId(void);
NSString *SenkoThemeCurrentId(void);
NSArray  *SenkoThemeAllIds(void);
/* picker folders (ios, custom,...); stable order */
NSArray  *SenkoThemeGroupIds(void);
NSString *SenkoThemeGroupTitle(NSString *groupId);
NSArray  *SenkoThemeIdsInGroup(NSString *groupId);
NSString *SenkoThemeGroupOfId(NSString *themeId);
NSString *SenkoThemeDisplayName(NSString *themeId);
/* one-line subtitle under style name */
NSString *SenkoThemeBlurb(NSString *themeId);
/* settings row: style + light/dark */
NSString *SenkoThemeStatusLine(void);
/* apply family; posts senkothemedidchangenotification */
void SenkoThemeApplyId(NSString *themeId);
/* re-resolve the current id and repaint: a custom theme's def is rebuilt when
   its colors are saved, so the cached pointer has to be refreshed */
void SenkoThemeReapplyCurrent(void);
/* freeze the current screen and dissolve it, so a theme switch reads as one
   transition instead of every view repainting on its own */
void SenkoThemeCrossfadeWindows(void);
/* re-resolve the active theme after the custom theme table is rebuilt */
void SenkoThemeRebindCurrent(void);
void SenkoThemeSetLight(BOOL light);
BOOL SenkoThemeIsLight(void);
/* 0 if theme cannot go dark */
BOOL SenkoThemeAllowsDark(void);
/* 1 for ios7/ios16 (no emboss chrome) */
BOOL SenkoThemeIsFlat(void);
/* 1 for senko-ios16 (also set on ios26 for chrome reuse) */
BOOL SenkoThemeIsIos16(void);
/* 1 for senko-ios26 liquid glass approx */
BOOL SenkoThemeIsIos26(void);
/* 1 when solid frost fills are used (no uitoolbar blur) */
BOOL SenkoThemeUsesFrost(void);
BOOL SenkoThemeIsBoykisser(void);
/* 1 for senko-miside (heart on, purple) */
BOOL SenkoThemeIsMiside(void);
/* 1 for senko-frutigeraero / senko-aero (sky gloss, light only) */
BOOL SenkoThemeIsFrutigeraero(void);
/* card corner; larger on ios16 */
CGFloat SenkoThemeCardRadius(void);
/* frost subview on host */
void SenkoInstallFrost(UIView *host);
/* solid frost fill; scroll-safe on armv7 */
void SenkoInstallFrostLite(UIView *host);
void SenkoRemoveFrost(UIView *host);
/* wallpaper gradient (theme only; ios16 is 3-stop diagonal) */
void SenkoApplyBackgroundGradient(CAGradientLayer *g);
/* state ignored - wallpaper stays pure theme; glow is around connect */
void SenkoApplyBackgroundGradientForState(CAGradientLayer *g, NSString *state, BOOL animated);
/* soft radial glow around connect button (layer.contents). state: idle|connecting|connected|error */
void SenkoApplyStatusWash(CALayer *layer, NSString *state, CGFloat side, BOOL animated);
/* transparent list well for the flat themes */
void SenkoStyleIos16ListWell(UIView *well);
/* light title / body fonts for modern themes */
UIFont *SenkoFontTitle(CGFloat size);
UIFont *SenkoFontBody(CGFloat size, BOOL semibold);
/* geometric bold wordmark face, closest available to rubik bold */
UIFont *SenkoFontDisplay(CGFloat size);
/* rendered width of one line, so a control can size itself to its own text */
CGFloat SenkoTextWidth(NSString *text, UIFont *font);
/* wrapped size of text held to one width, measured through a label so no screen
   depends on the deprecated string drawing category */
CGSize SenkoTextSize(NSString *text, UIFont *font, CGFloat width);
/* blend a palette colour toward white (delta > 0) or black (delta < 0), so a
   two stop fill has relief whatever the theme supplies */
UIColor *SenkoShadeColor(UIColor *base, CGFloat delta);

extern NSString * const SenkoThemeDidChangeNotification;
extern NSString * const SenkoThemeIdKey;
extern NSString * const SenkoThemeLightKey;

/* body text for current theme */
void SenkoStyleInkLabel(UILabel *label);
void SenkoStyleMutedLabel(UILabel *label);
void SenkoStyleAccentLabel(UILabel *label);
/* light text on fixed dark plates */
void SenkoStyleInkOnDark(UILabel *label);
void SenkoStyleMutedOnDark(UILabel *label);
void SenkoStyleAccentOnDark(UILabel *label);
/* accent title color for + and similar */
void SenkoStylePaperGlyph(UIButton *button);
/* white title on filled controls */
void SenkoStyleChromeTitle(UIButton *button);
/* light title on dark section buttons */
void SenkoStyleGlyphOnDark(UIButton *button);

/* section header plate styling */
void SenkoFillSectionGradient(CAGradientLayer *g);
void SenkoStyleSectionPlate(UIView *plate);
void SenkoStyleSectionTitle(UILabel *label);
void SenkoStyleSectionMeta(UILabel *label);
void SenkoStyleSectionGlyph(UIButton *button);
/* terminal / config editor colors */
void SenkoStyleTerminalPlate(UIView *plate);
void SenkoStyleTerminalText(UITextView *tv);
/* translucent field / segment chrome for liquid glass */
void SenkoStyleGlassField(UITextField *field);
void SenkoStyleGlassSegmented(UISegmentedControl *seg);
/* quiet selection shared by lists on old and new UIKit */
void SenkoStyleSelectableCell(UITableViewCell *cell);
/* ios 6 rotation: view.bounds can stay portrait-shaped while device is landscape (and the reverse) */
CGRect SenkoViewBounds(UIView *view);
/* solid theme fill, or ios26 wallpaper under translucent chrome */
void SenkoApplyScreenChrome(UIView *root);

void SetStatusDefault(UILabel *label, NSString *text);
void SetStatusRefresh(UILabel *label, NSString *text);
UIImage *TintedIconNamed(NSString *name, CGFloat side, UIColor *tint);
/* glyphs drawn on a shared 24pt grid, cached per size and tint. drawing them
   keeps one weight across every screen and removes the png bundling step that
   silently dropped icon-refresh from the app */
UIImage *GaugeIcon(CGFloat side, UIColor *tint);
UIImage *SenkoGearIcon(CGFloat side, UIColor *tint);
UIImage *SenkoPlusIcon(CGFloat side, UIColor *tint);
UIImage *SenkoIconRefresh(CGFloat side, UIColor *tint);
UIImage *SenkoIconPower(CGFloat side, UIColor *tint);
UIImage *SenkoIconShield(CGFloat side, UIColor *tint);
UIImage *SenkoIconGlobe(CGFloat side, UIColor *tint);
UIImage *SenkoIconChevron(CGFloat side, UIColor *tint);
UIImage *SenkoIconCopy(CGFloat side, UIColor *tint);
UIImage *SenkoIconPencil(CGFloat side, UIColor *tint);
UIImage *SenkoIconTrash(CGFloat side, UIColor *tint);
UIImage *SenkoIconClose(CGFloat side, UIColor *tint);
void StyleNavBarClassic(UINavigationController *nav);
/* capsule button; skips rebuild if size/colors match */
void StyleGlossyCapsule(UIButton *button, UIColor *top, UIColor *bottom);
/* resize capsule layers without color rebuild */
void StyleGlossyCapsuleLayout(UIButton *button);
CGFloat GetTopOffset(void);
UIEdgeInsets SenkoSafeAreaInsets(UIView *view);
CAGradientLayer *AddVGradient(UIView *view, UIColor *top, UIColor *bottom);
void SenkoSetLayerFrame(CALayer *layer, CGRect frame);

/* motion. uikit gained spring animation in ios 7, so the helpers fall back to
   a timed overshoot rather than dropping the movement on older systems */
void SenkoAnimate(NSTimeInterval duration, void (^animations)(void),
                  void (^completion)(BOOL finished));
void SenkoAnimateSpring(NSTimeInterval duration, NSTimeInterval delay,
                        void (^animations)(void),
                        void (^completion)(BOOL finished));
/* scale a view briefly, used for press feedback on cells and pills */
void SenkoPressPop(UIView *view, BOOL pressed);
/* fade and lift a view into place; index staggers rows in a list */
void SenkoRevealView(UIView *view, NSUInteger index);
/* catransaction: disable implicit animations */
void SenkoBeginSilentLayers(void);
void SenkoEndSilentLayers(void);
/* drop icon cache after theme switch */
void SenkoThemeFlushImageCaches(void);

#endif
