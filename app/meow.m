#import "meow.h"
#import "ui_theme.h"

#import <AudioToolbox/AudioToolbox.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import <objc/message.h>

/* sfx via avaudioplayer when present, else systemsound */

NSString * const SenkoMeowKey = @"senko.meowmeowmeow";
NSString * const SenkoOuchKey = @"senko.ooouch";

static SystemSoundID gMeowSound = 0;
static id gMeowPlayer = nil; /* avaudioplayer*, runtime only */
static BOOL gMeowReady = NO;

static SystemSoundID gOuchSound = 0;
static id gOuchPlayer = nil;
static BOOL gOuchReady = NO;

static BOOL gHooksOn = NO;
static BOOL gSessionOk = NO;
static CFTimeInterval gLastPlay = 0;
static const CFTimeInterval kSfxMinGap = 0.05;

static void SenkoSfxActivateSession(void) {
    if (gSessionOk) return;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    static BOOL inited = NO;
    if (!inited) {
        AudioSessionInitialize(NULL, NULL, NULL, NULL);
        inited = YES;
    }
    UInt32 cat = kAudioSessionCategory_MediaPlayback;
    AudioSessionSetProperty(kAudioSessionProperty_AudioCategory,
                            sizeof(cat), &cat);
    UInt32 mix = 1;
    AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryMixWithOthers,
                            sizeof(mix), &mix);
    AudioSessionSetActive(YES);
#pragma clang diagnostic pop

    Class sesCls = NSClassFromString(@"AVAudioSession");
    if (sesCls) {
        id session = ((id (*)(id, SEL))objc_msgSend)(sesCls, @selector(sharedInstance));
        if (session) {
            SEL setCat = NSSelectorFromString(@"setCategory:error:");
            SEL setAct = NSSelectorFromString(@"setActive:error:");
            if ([session respondsToSelector:setCat]) {
                NSError *err = nil;
                BOOL ok = ((BOOL (*)(id, SEL, id, id *))objc_msgSend)(
                    session, setCat, @"AVAudioSessionCategoryPlayback", &err);
                if (!ok)
                    ((BOOL (*)(id, SEL, id, id *))objc_msgSend)(
                        session, setCat, @"AVAudioSessionCategoryAmbient", &err);
            }
            if ([session respondsToSelector:setAct]) {
                NSError *err = nil;
                ((BOOL (*)(id, SEL, BOOL, id *))objc_msgSend)(session, setAct, YES, &err);
            }
        }
    }
    gSessionOk = YES;
}

static NSString *SenkoSfxFindPath(NSString *base) {
    NSString *path = [[NSBundle mainBundle] pathForResource:base ofType:@"wav"];
    if (!path)
        path = [[NSBundle mainBundle] pathForResource:base ofType:@"caf"];
    if (!path) {
        NSString *bundle = [[NSBundle mainBundle] bundlePath];
        NSString *try1 = [bundle stringByAppendingPathComponent:
                          [base stringByAppendingPathExtension:@"wav"]];
        NSString *try2 = [bundle stringByAppendingPathComponent:
                          [base stringByAppendingPathExtension:@"caf"]];
        if ([[NSFileManager defaultManager] fileExistsAtPath:try1])
            path = try1;
        else if ([[NSFileManager defaultManager] fileExistsAtPath:try2])
            path = try2;
    }
    return path;
}

/* create avaudioplayer without linking the class */
static id SenkoSfxMakePlayer(NSURL *url) {
    Class cls = NSClassFromString(@"AVAudioPlayer");
    if (!cls) return nil;
    SEL initSel = @selector(initWithContentsOfURL:error:);
    if (![cls instancesRespondToSelector:initSel]) return nil;

    NSError *err = nil;
    id raw = [cls alloc];
    id p = ((id (*)(id, SEL, id, id *))objc_msgSend)(raw, initSel, url, &err);
    if (!p) {
        NSLog(@"senko sfx: AVAudioPlayer fail: %@", err);
        return nil;
    }
    if ([p respondsToSelector:@selector(setVolume:)])
        ((void (*)(id, SEL, float))objc_msgSend)(p, @selector(setVolume:), 1.0f);
    if ([p respondsToSelector:@selector(setNumberOfLoops:)])
        ((void (*)(id, SEL, NSInteger))objc_msgSend)(p, @selector(setNumberOfLoops:), 0);
    if ([p respondsToSelector:@selector(prepareToPlay)])
        ((void (*)(id, SEL))objc_msgSend)(p, @selector(prepareToPlay));
    return p;
}

static BOOL SenkoSfxPlayerPlay(id player) {
    if (!player) return NO;
    if ([player respondsToSelector:@selector(isPlaying)]) {
        BOOL playing = ((BOOL (*)(id, SEL))objc_msgSend)(player, @selector(isPlaying));
        if (playing && [player respondsToSelector:@selector(setCurrentTime:)])
            ((void (*)(id, SEL, NSTimeInterval))objc_msgSend)(
                player, @selector(setCurrentTime:), 0.0);
    }
    if (![player respondsToSelector:@selector(play)]) return NO;
    return ((BOOL (*)(id, SEL))objc_msgSend)(player, @selector(play));
}

static BOOL SenkoSfxLoad(NSString *base,
                         id *outPlayer,
                         SystemSoundID *outSound,
                         BOOL *outReady,
                         const char *tag) {
    if (*outReady && (*outPlayer || *outSound))
        return YES;

    if (*outSound) {
        AudioServicesDisposeSystemSoundID(*outSound);
        *outSound = 0;
    }
    if (*outPlayer) {
        [*outPlayer release];
        *outPlayer = nil;
    }
    *outReady = NO;

    NSString *path = SenkoSfxFindPath(base);
    if (!path) {
        NSLog(@"senko %s: sound file missing in bundle", tag);
        return NO;
    }

    NSURL *url = [NSURL fileURLWithPath:path];
    SenkoSfxActivateSession();

    id p = SenkoSfxMakePlayer(url);
    if (p) {
        *outPlayer = p;
        *outReady = YES;
        NSLog(@"senko %s: AVAudioPlayer ready (%@)", tag, path);
        return YES;
    }

    OSStatus st = AudioServicesCreateSystemSoundID((CFURLRef)url, outSound);
    if (st == noErr && *outSound != 0) {
        *outReady = YES;
        NSLog(@"senko %s: SystemSound ready id=%u", tag, (unsigned)*outSound);
    } else {
        NSLog(@"senko %s: SystemSound create failed status=%d path=%@",
              tag, (int)st, path);
        *outSound = 0;
    }
    return *outReady;
}

static void SenkoSfxPlayLoaded(id *player,
                               SystemSoundID sound,
                               BOOL *ready,
                               void (*prepare)(void)) {
    CFTimeInterval now = CFAbsoluteTimeGetCurrent();
    if (now - gLastPlay < kSfxMinGap)
        return;
    gLastPlay = now;

    if (*player) {
        if (SenkoSfxPlayerPlay(*player))
            return;
/* reopen session after interrupt */
        gSessionOk = NO;
        SenkoSfxActivateSession();
        *ready = NO;
        prepare();
        if (*player && SenkoSfxPlayerPlay(*player))
            return;
    }
    if (sound)
        AudioServicesPlayAlertSound(sound);
}

BOOL SenkoMeowEnabled(void) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    if (![d objectForKey:SenkoMeowKey])
        return YES;
    return [d boolForKey:SenkoMeowKey];
}

void SenkoMeowSetEnabled(BOOL on) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:on ? YES : NO forKey:SenkoMeowKey];
    [d synchronize];
    if (on)
        SenkoMeowPrepare();
}

void SenkoMeowPrepare(void) {
    SenkoSfxLoad(@"meow", &gMeowPlayer, &gMeowSound, &gMeowReady, "meow");
}

void SenkoMeowPlay(void) {
    if (!SenkoThemeIsBoykisser()) return;
    if (!SenkoMeowEnabled()) return;
    if (!gMeowReady)
        SenkoMeowPrepare();
    if (!gMeowReady) return;
    SenkoSfxPlayLoaded(&gMeowPlayer, gMeowSound, &gMeowReady, SenkoMeowPrepare);
}

BOOL SenkoOuchEnabled(void) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    if (![d objectForKey:SenkoOuchKey])
        return YES;
    return [d boolForKey:SenkoOuchKey];
}

void SenkoOuchSetEnabled(BOOL on) {
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    [d setBool:on ? YES : NO forKey:SenkoOuchKey];
    [d synchronize];
    if (on)
        SenkoOuchPrepare();
}

void SenkoOuchPrepare(void) {
    SenkoSfxLoad(@"ouch", &gOuchPlayer, &gOuchSound, &gOuchReady, "ouch");
}

void SenkoOuchPlay(void) {
    if (!SenkoThemeIsMiside()) return;
    if (!SenkoOuchEnabled()) return;
    if (!gOuchReady)
        SenkoOuchPrepare();
    if (!gOuchReady) return;
    SenkoSfxPlayLoaded(&gOuchPlayer, gOuchSound, &gOuchReady, SenkoOuchPrepare);
}

void SenkoThemeSfxPrepare(void) {
    if (SenkoThemeIsBoykisser() && SenkoMeowEnabled())
        SenkoMeowPrepare();
    if (SenkoThemeIsMiside() && SenkoOuchEnabled())
        SenkoOuchPrepare();
}

void SenkoThemeSfxPlay(void) {
    SenkoMeowPlay();
    SenkoOuchPlay();
}

static void SenkoMeowSwizzle(Class cls, SEL orig, SEL neo) {
    Method m1 = class_getInstanceMethod(cls, orig);
    Method m2 = class_getInstanceMethod(cls, neo);
    if (!m1 || !m2) return;
    method_exchangeImplementations(m1, m2);
}

@implementation UIApplication (SenkoMeow)
- (BOOL)senko_sendAction:(SEL)action to:(id)target from:(id)sender forEvent:(UIEvent *)event {
    BOOL ok = [self senko_sendAction:action to:target from:sender forEvent:event];
    if (sender && ([sender isKindOfClass:[UIControl class]] ||
                   [sender isKindOfClass:[UIBarButtonItem class]]))
        SenkoThemeSfxPlay();
    return ok;
}
@end

void SenkoMeowInstallHooks(void) {
    if (gHooksOn) return;
    gHooksOn = YES;
    SenkoMeowSwizzle([UIApplication class],
                     @selector(sendAction:to:from:forEvent:),
                     @selector(senko_sendAction:to:from:forEvent:));
}
