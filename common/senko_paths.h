#ifndef SENKO_PATHS_H
#define SENKO_PATHS_H

/* /var/jb keeps the universal payload away from the sealed root filesystem */
#if defined(SENKO_ROOTLESS)
#define SENKO_JBROOT "/var/jb"
#else
#define SENKO_JBROOT ""
#endif

#define SENKO_USR_BIN SENKO_JBROOT "/usr/bin"
#define SENKO_USR_LIB SENKO_JBROOT "/usr/lib"
#define SENKO_LAUNCH_DAEMONS SENKO_JBROOT "/Library/LaunchDaemons"
#define SENKO_SUBSTRATE_DIR SENKO_JBROOT "/Library/MobileSubstrate/DynamicLibraries"

/* the device id panels bind a subscription to. it stays outside the jailbreak
   root so the daemon and the ui, which run as different users, agree on one
   value and a jailbreak change does not hand the panel a new device */
#define SENKO_HWID_PATH "/var/mobile/Library/Preferences/com.senko.hwid"

/* launchd redirects both daemon streams here, and the ui reads the same file */
#define SENKO_SYSTEM_LOG "/var/log/senko-system.log"

/* the ui stages pasted or picked content here and the daemon consumes it. a
   fixed path keeps the privileged reader free of any caller supplied path */
#define SENKO_IMPORT_STAGE "/var/mobile/Library/Preferences/Senko/import.dat"

/* a launch that dies before the first frame leaves nothing a user can reach,
   because the only reader of the report used to be the app that will not
   start. these paths are fixed so senkoctl can print them over ssh instead */
#define SENKO_CRASH_DIR    "/var/mobile/Library/Preferences/Senko"
#define SENKO_CRASH_LAST   SENKO_CRASH_DIR "/last-crash.log"
#define SENKO_CRASH_PREV   SENKO_CRASH_DIR "/previous-crash.log"
#define SENKO_CRASH_STAGE  SENKO_CRASH_DIR "/launch-stage.log"
#define SENKO_CRASH_SCREEN SENKO_CRASH_DIR "/screen.log"
/* consecutive launches that never reached the first frame; two of them turn
   the next launch into safe mode */
#define SENKO_CRASH_FAILS  SENKO_CRASH_DIR "/launch-fails.log"

#endif
