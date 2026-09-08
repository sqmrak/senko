#ifndef SENKO_THEME_EDIT_VC_H
#define SENKO_THEME_EDIT_VC_H

#import <UIKit/UIKit.h>

/* edits one saved custom theme in place: every change is persisted and, when the
   theme is the active one, repainted immediately */
@interface ThemeEditVC : UIViewController
- (id)initWithThemeId:(NSString *)themeId;
@end

#endif
