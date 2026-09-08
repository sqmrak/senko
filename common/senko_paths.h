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

#endif
