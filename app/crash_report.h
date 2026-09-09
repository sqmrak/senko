#ifndef SENKO_CRASH_REPORT_H
#define SENKO_CRASH_REPORT_H

#import <Foundation/Foundation.h>

/* a jailbroken app has no crash reporting service the user can reach, and a
   launch that dies before the first frame leaves nothing behind at all. these
   write the last fatal error and the last launch stage next to the profile
   store, where the logs screen reads them back on the following launch */

/* call first in main, before any framework work */
void SenkoCrashInstall(void);

/* name the launch step about to run; the last one written survives a kill that
   no handler can catch, including a jetsam or an AMFI kill */
void SenkoCrashStage(const char *stage);

/* stop recording launch stages once the ui is live */
void SenkoCrashLaunchComplete(void);

/* name the screen that just became visible; unlike the launch stage this
   keeps recording for the life of the process, so a fault in a uikit render
   pass says which screen was on top instead of only naming system frames */
void SenkoCrashScreen(const char *name);

/* the active theme decides whether the ui asks uikit for live glass, which
   is the one thing senko does that reaches coreui and core image */
void SenkoCrashTheme(const char *identifier);

/* two launches in a row that never reached the first frame put the next one in
   safe mode: the stock theme, no live glass and no decor, so a device that
   cannot open senko at all can still open it far enough to read the report
   below and send it. the user's own theme is left untouched on disk */
BOOL SenkoCrashSafeMode(void);

/* how many launches in a row died before the ui came up */
int SenkoCrashFailedLaunches(void);

/* leave safe mode once the user has read the report */
void SenkoCrashClearSafeMode(void);

/* the previous launch's fatal report, or nil when the app shut down cleanly */
NSString *SenkoCrashLastReport(void);

/* drop the stored report after the user has seen it */
void SenkoCrashClearLastReport(void);

#endif
