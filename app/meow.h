#ifndef SENKO_MEOW_H
#define SENKO_MEOW_H

#import <Foundation/Foundation.h>

extern NSString * const SenkoMeowKey;
extern NSString * const SenkoOuchKey;

BOOL SenkoMeowEnabled(void);
void SenkoMeowSetEnabled(BOOL on);
void SenkoMeowPrepare(void);
void SenkoMeowPlay(void);

BOOL SenkoOuchEnabled(void);
void SenkoOuchSetEnabled(BOOL on);
void SenkoOuchPrepare(void);
void SenkoOuchPlay(void);

void SenkoThemeSfxPlay(void);
void SenkoThemeSfxPrepare(void);

void SenkoMeowInstallHooks(void);

#endif
