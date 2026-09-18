#import "crash_report.h"

#import <UIKit/UIKit.h>

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#import "app_common.h"
#include "../common/senko_paths.h"

#define kSenkoCrashDir   SENKO_CRASH_DIR
#define kSenkoCrashPath  SENKO_CRASH_LAST
/* the live file is rotated at launch so the report the ui shows always
   belongs to a previous run, never to the one reading it */
#define kSenkoPrevPath   SENKO_CRASH_PREV
#define kSenkoStagePath  SENKO_CRASH_STAGE
#define kSenkoScreenPath SENKO_CRASH_SCREEN
#define kSenkoFailsPath  SENKO_CRASH_FAILS
#define kSenkoSafePath   SENKO_CRASH_SAFE

/* one bad launch is as likely to be a jetsam or a springboard restart as a
   defect, so safe mode waits for the second one in a row */
#define kSenkoSafeModeThreshold 2

static const int kSenkoFatalSignals[] = {
    SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP
};

static char gStage[64] = "start";
static char gPreviousStage[64];
static char gScreen[64] = "none";
static char gPreviousScreen[64];
static char gTheme[48] = "unknown";
static char gVersion[32] = "senko";
static int  gLaunchDone;
static int  gInstalled;
static int  gFailedLaunches;
static int  gSafeMode;
/* an uncaught objc exception ends in abort(), so the signal handler runs right
   after the exception handler. truncating the file there threw away the name
   and the reason and left nothing but "SIGABRT", which says nothing at all */
static volatile sig_atomic_t gReportWritten;
static struct sigaction gPrevious[sizeof kSenkoFatalSignals / sizeof kSenkoFatalSignals[0]];
static NSUncaughtExceptionHandler *gPreviousExceptionHandler;

static void write_all(int fd, const char *text, size_t len) {
    while (len) {
        ssize_t n = write(fd, text, len);
        if (n > 0) {
            text += (size_t)n;
            len -= (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return;
    }
}

static void write_str(int fd, const char *text) {
    if (text) write_all(fd, text, strlen(text));
}

/* the signal path may not call snprintf, so integers are formatted by hand */
static void write_int(int fd, long value) {
    char buf[24];
    size_t at = sizeof buf;
    int negative = value < 0;
    unsigned long magnitude = negative ? (unsigned long)(-value) : (unsigned long)value;
    if (magnitude == 0) buf[--at] = '0';
    while (magnitude) {
        buf[--at] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    }
    if (negative) buf[--at] = '-';
    write_all(fd, buf + at, sizeof buf - at);
}

/* the counter is one digit of text so the signal path never has to parse it */
static int read_fail_count(void) {
    int fd = open(kSenkoFailsPath, O_RDONLY);
    if (fd < 0) return 0;
    char buf[8];
    ssize_t got = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (got <= 0) return 0;
    buf[got] = '\0';
    int value = 0;
    for (ssize_t i = 0; i < got; ++i) {
        if (buf[i] < '0' || buf[i] > '9') break;
        value = value * 10 + (buf[i] - '0');
        if (value > 99) { value = 99; break; }
    }
    return value;
}

static void write_fail_count(int value) {
    mkdir(kSenkoCrashDir, 0755);
    int fd = open(kSenkoFailsPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    write_int(fd, value);
    write_str(fd, "\n");
    close(fd);
}

static int open_report(void) {
    mkdir(kSenkoCrashDir, 0755);
    int flags = O_WRONLY | O_CREAT | (gReportWritten ? O_APPEND : O_TRUNC);
    int fd = open(kSenkoCrashPath, flags, 0644);
    if (fd >= 0) gReportWritten = 1;
    return fd;
}

static const char *signal_name(int number) {
    switch (number) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS:  return "SIGBUS";
        case SIGILL:  return "SIGILL";
        case SIGFPE:  return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        case SIGTRAP: return "SIGTRAP";
        default:      return "signal";
    }
}

static void senko_signal_handler(int number, siginfo_t *info, void *context) {
    (void)context;
    int fd = open_report();
    if (fd >= 0) {
        write_str(fd, "senko ");
        write_str(fd, gVersion);
        write_str(fd, " crashed\nfatal: ");
        write_str(fd, signal_name(number));
        write_str(fd, " (");
        write_int(fd, number);
        write_str(fd, ")\nfault address: ");
        write_int(fd, info ? (long)(uintptr_t)info->si_addr : 0);
        write_str(fd, "\nlaunch stage: ");
        write_str(fd, gStage);
        write_str(fd, "\nscreen: ");
        write_str(fd, gScreen);
        write_str(fd, "\ntheme: ");
        write_str(fd, gTheme);
        write_str(fd, "\nbacktrace:\n");
        void *frames[48];
        int count = backtrace(frames, (int)(sizeof frames / sizeof frames[0]));
        if (count > 0) backtrace_symbols_fd(frames, count, fd);
        close(fd);
    }
    /* hand the signal back so the system still records what it would have */
    size_t total = sizeof kSenkoFatalSignals / sizeof kSenkoFatalSignals[0];
    for (size_t i = 0; i < total; ++i) {
        if (kSenkoFatalSignals[i] != number) continue;
        sigaction(number, &gPrevious[i], NULL);
        break;
    }
    raise(number);
}

static void senko_exception_handler(NSException *exception) {
    int fd = open_report();
    if (fd >= 0) {
        write_str(fd, "senko ");
        write_str(fd, gVersion);
        write_str(fd, " crashed\nfatal: ");
        write_str(fd, [[exception name] UTF8String]);
        write_str(fd, "\nreason: ");
        write_str(fd, [[exception reason] UTF8String]);
        {
            NSDictionary *info = [exception userInfo];
            if ([info count]) {
                write_str(fd, "\nuser info: ");
                write_str(fd, [[info description] UTF8String]);
            }
        }
        write_str(fd, "\nlaunch stage: ");
        write_str(fd, gStage);
        write_str(fd, "\nscreen: ");
        write_str(fd, gScreen);
        write_str(fd, "\ntheme: ");
        write_str(fd, gTheme);
        write_str(fd, "\nbacktrace:\n");
        for (NSString *frame in [exception callStackSymbols]) {
            write_str(fd, [frame UTF8String]);
            write_str(fd, "\n");
        }
        close(fd);
    }
    if (gPreviousExceptionHandler) gPreviousExceptionHandler(exception);
}

void SenkoCrashInstall(void) {
    if (gInstalled) return;
    gInstalled = 1;
    /* the signal path cannot build a string, so the version is copied out of
       the objc constant once, here */
    const char *version = [SENKO_VERSION UTF8String];
    if (version) {
        size_t n = strlen(version);
        if (n >= sizeof gVersion) n = sizeof gVersion - 1;
        memcpy(gVersion, version, n);
        gVersion[n] = '\0';
    }
    /* the stage file is rewritten from here on, so the one the last launch
       left has to be read before the first stage is recorded */
    int stage_fd = open(kSenkoStagePath, O_RDONLY);
    if (stage_fd >= 0) {
        ssize_t got = read(stage_fd, gPreviousStage, sizeof gPreviousStage - 1);
        close(stage_fd);
        if (got > 0) {
            gPreviousStage[got] = '\0';
            char *nl = strchr(gPreviousStage, '\n');
            if (nl) *nl = '\0';
        }
    }
    int screen_fd = open(kSenkoScreenPath, O_RDONLY);
    if (screen_fd >= 0) {
        ssize_t got = read(screen_fd, gPreviousScreen, sizeof gPreviousScreen - 1);
        close(screen_fd);
        if (got > 0) {
            gPreviousScreen[got] = '\0';
            char *nl = strchr(gPreviousScreen, '\n');
            if (nl) *nl = '\0';
        }
    }
    /* a launch that recorded a stage but never reached "ready" never made it to
       the first frame. the count is updated here, at the start of the launch
       that follows, because nothing can run inside the one that was killed */
    gFailedLaunches = read_fail_count();
    if (gPreviousStage[0] && strcmp(gPreviousStage, "ready") != 0) {
        if (gFailedLaunches < 99) gFailedLaunches++;
    } else {
        gFailedLaunches = 0;
    }
    write_fail_count(gFailedLaunches);
/* a safe mode asked for by hand is not earned by dead launches, so it has its
   own marker and outlives the count that gets zeroed by a good launch */
    gSafeMode = gFailedLaunches >= kSenkoSafeModeThreshold ||
                access(kSenkoSafePath, F_OK) == 0;

    mkdir(kSenkoCrashDir, 0755);
    rename(kSenkoCrashPath, kSenkoPrevPath);

    gPreviousExceptionHandler = NSGetUncaughtExceptionHandler();
    NSSetUncaughtExceptionHandler(&senko_exception_handler);

    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = senko_signal_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    size_t total = sizeof kSenkoFatalSignals / sizeof kSenkoFatalSignals[0];
    for (size_t i = 0; i < total; ++i)
        sigaction(kSenkoFatalSignals[i], &action, &gPrevious[i]);
    SenkoCrashStage("install");
}

void SenkoCrashStage(const char *stage) {
    if (!stage || gLaunchDone) return;
    size_t len = strlen(stage);
    if (len >= sizeof gStage) len = sizeof gStage - 1;
    memcpy(gStage, stage, len);
    gStage[len] = '\0';
    mkdir(kSenkoCrashDir, 0755);
    int fd = open(kSenkoStagePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    write_str(fd, gStage);
    write_str(fd, "\n");
    close(fd);
}

void SenkoCrashTheme(const char *identifier) {
    if (!identifier) return;
    size_t len = strlen(identifier);
    if (len >= sizeof gTheme) len = sizeof gTheme - 1;
    memcpy(gTheme, identifier, len);
    gTheme[len] = '\0';
}

void SenkoCrashScreen(const char *name) {
    if (!name || !gInstalled) return;
    size_t len = strlen(name);
    if (len >= sizeof gScreen) len = sizeof gScreen - 1;
    if (memcmp(gScreen, name, len) == 0 && gScreen[len] == '\0') return;
    memcpy(gScreen, name, len);
    gScreen[len] = '\0';
    int fd = open(kSenkoScreenPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    write_str(fd, gScreen);
    write_str(fd, "\n");
    close(fd);
}

/* a report outlives the build that wrote it. one left by an older version says
   nothing about the build now running, and to whoever opens the log it reads
   exactly like a fresh crash. it is retired as soon as this build has proved
   it can reach the first frame; a report from this same build is kept, because
   that is the one worth reading */
static void retire_foreign_report(void) {
    int fd = open(kSenkoPrevPath, O_RDONLY);
    if (fd < 0) return;
    char head[256];
    ssize_t got = read(fd, head, sizeof head - 1);
    close(fd);
    if (got <= 0) return;
    head[got] = '\0';
    if (gVersion[0] && strstr(head, gVersion)) return;
    unlink(kSenkoPrevPath);
}

void SenkoCrashLaunchComplete(void) {
    if (gLaunchDone) return;
    SenkoCrashStage("ready");
    gLaunchDone = 1;
    /* the ui is up. whatever killed the previous launches is behind us, so the
       next one starts normally again; this run stays in safe mode because the
       views were already built that way, and the count stays in memory so the
       report can still say how many launches it took */
    if (gFailedLaunches) write_fail_count(0);
    retire_foreign_report();
}

BOOL SenkoCrashSafeMode(void) {
    return gSafeMode ? YES : NO;
}

int SenkoCrashFailedLaunches(void) {
    return gFailedLaunches;
}

void SenkoCrashClearSafeMode(void) {
    gSafeMode = 0;
    write_fail_count(0);
    unlink(kSenkoSafePath);
}

void SenkoCrashEnterSafeMode(void) {
    mkdir(kSenkoCrashDir, 0755);
    int fd = open(kSenkoSafePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
}

NSString *SenkoCrashLastReport(void) {
    NSString *report = [NSString stringWithContentsOfFile:@kSenkoPrevPath
                                                 encoding:NSUTF8StringEncoding
                                                    error:NULL];
    if ([report length]) return report;
    /* a launch killed from outside leaves the stage without a report, and that
       is exactly what a jetsam or an amfi kill produces */
    if (!gPreviousStage[0] || strcmp(gPreviousStage, "ready") == 0) return nil;
    return [NSString stringWithFormat:
            @"the previous launch was killed at stage \"%s\" (screen \"%s\") "
             "without a signal",
            gPreviousStage, gPreviousScreen[0] ? gPreviousScreen : "none"];
}

void SenkoCrashClearLastReport(void) {
    unlink(kSenkoPrevPath);
    gPreviousStage[0] = '\0';
}
