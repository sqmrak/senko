#ifndef SENKO_NATIVE_CORE_H
#define SENKO_NATIVE_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

int SenkoNativeStart(char *config, int configLen, int tunFD,
                     char *errorOut, int errorCap);
int SenkoNativeStop(char *errorOut, int errorCap);
int SenkoNativeIsRunning(void);

#ifdef __cplusplus
}
#endif

#endif
