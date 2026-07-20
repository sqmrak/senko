#ifndef SENKO_MEOW_H
#define SENKO_MEOW_H

#import <Foundation/Foundation.h>

/* boykisser meowmeowmeow; defaults key; missing means on */
extern NSString * const SenkoMeowKey;
/* miside ooouch; defaults key; missing means on */
extern NSString * const SenkoOuchKey;

BOOL SenkoMeowEnabled(void);
void SenkoMeowSetEnabled(BOOL on);
void SenkoMeowPrepare(void);
/* no-op unless boykisser + toggle on */
void SenkoMeowPlay(void);

BOOL SenkoOuchEnabled(void);
void SenkoOuchSetEnabled(BOOL on);
void SenkoOuchPrepare(void);
/* no-op unless miside + toggle on */
void SenkoOuchPlay(void);

/* play theme sfx for current style (meow and/or ouch) */
void SenkoThemeSfxPlay(void);
void SenkoThemeSfxPrepare(void);

/* swizzle once at launch */
void SenkoMeowInstallHooks(void);

#endif
