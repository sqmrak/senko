#ifndef SENKO_IOS_COMPAT_H
#define SENKO_IOS_COMPAT_H

#import <UIKit/UIKit.h>

/* iOS 11 adds automatic safe-area inset adjustment. Every caller of this
   helper owns contentInset itself, so accepting UIKit's second adjustment
   hides rows below a home indicator */
static inline void SenkoScrollViewUseManualInsets(UIScrollView *scroll) {
    SEL selector = NSSelectorFromString(@"setContentInsetAdjustmentBehavior:");
    if (!scroll || ![scroll respondsToSelector:selector]) return;
    ((void (*)(id, SEL, NSInteger))[scroll methodForSelector:selector])(scroll, selector, 2);
}

/* ios 4.3 has only the modal calls that ios 5 replaced with completion blocks */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static inline void SenkoPresentModal(UIViewController *host, UIViewController *vc) {
    if ([host respondsToSelector:@selector(presentViewController:animated:completion:)])
        [host presentViewController:vc animated:YES completion:nil];
    else
        [host presentModalViewController:vc animated:YES];
}

static inline void SenkoDismissModal(UIViewController *vc, void (^completion)(void)) {
    if ([vc respondsToSelector:@selector(dismissViewControllerAnimated:completion:)]) {
        [vc dismissViewControllerAnimated:YES completion:completion];
        return;
    }
    /* ios 4.3 has no completion block, and a follow up presenting over a modal
       still animating out is ignored, so that dismissal skips the animation */
    [vc dismissModalViewControllerAnimated:completion ? NO : YES];
    if (completion) completion();
}

static inline UIViewController *SenkoPresentedController(UIViewController *vc) {
    if ([vc respondsToSelector:@selector(presentedViewController)])
        return vc.presentedViewController;
    return vc.modalViewController;
}
#pragma clang diagnostic pop

/* the ios 5 SDK predates these Foundation and UIKit spelling aliases */
#ifndef NS_ENUM
#define NS_ENUM(_type, _name) _type _name; enum
#endif

#if __IPHONE_OS_VERSION_MAX_ALLOWED < 60000
typedef UITextAlignment NSTextAlignment;
#define NSTextAlignmentLeft UITextAlignmentLeft
#define NSTextAlignmentCenter UITextAlignmentCenter
#define NSTextAlignmentRight UITextAlignmentRight

typedef UILineBreakMode NSLineBreakMode;
#define NSLineBreakByWordWrapping UILineBreakModeWordWrap
#define NSLineBreakByCharWrapping UILineBreakModeCharacterWrap
#define NSLineBreakByClipping UILineBreakModeClip
#define NSLineBreakByTruncatingHead UILineBreakModeHeadTruncation
#define NSLineBreakByTruncatingTail UILineBreakModeTailTruncation
#define NSLineBreakByTruncatingMiddle UILineBreakModeMiddleTruncation

typedef NSUInteger UIInterfaceOrientationMask;
#define UIInterfaceOrientationMaskPortrait (1u << UIInterfaceOrientationPortrait)
#define UIInterfaceOrientationMaskPortraitUpsideDown (1u << UIInterfaceOrientationPortraitUpsideDown)
#define UIInterfaceOrientationMaskLandscapeLeft (1u << UIInterfaceOrientationLandscapeLeft)
#define UIInterfaceOrientationMaskLandscapeRight (1u << UIInterfaceOrientationLandscapeRight)
#define UIInterfaceOrientationMaskLandscape (UIInterfaceOrientationMaskLandscapeLeft | UIInterfaceOrientationMaskLandscapeRight)
#define UIInterfaceOrientationMaskAll (UIInterfaceOrientationMaskPortrait | UIInterfaceOrientationMaskPortraitUpsideDown | UIInterfaceOrientationMaskLandscape)
#define UIInterfaceOrientationMaskAllButUpsideDown (UIInterfaceOrientationMaskPortrait | UIInterfaceOrientationMaskLandscape)

@interface UILabel (SenkoIOS5MinimumScaleFactor)
- (void)setMinimumScaleFactor:(CGFloat)factor;
@end
#endif

#endif
