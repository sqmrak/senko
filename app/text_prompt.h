#import <UIKit/UIKit.h>

/* one line of text on ios 4.3, which has no text input alert style. the
   handler runs after the screen is gone and only for the confirm button */
@interface SenkoTextPromptVC : UIViewController <UITextFieldDelegate>

+ (void)presentFrom:(UIViewController *)host
              title:(NSString *)title
            message:(NSString *)message
               text:(NSString *)text
           keyboard:(UIKeyboardType)keyboard
            confirm:(NSString *)confirm
            handler:(void (^)(NSString *text))handler;

@end
