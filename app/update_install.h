#ifndef SENKO_UPDATE_INSTALL_H
#define SENKO_UPDATE_INSTALL_H

#import <UIKit/UIKit.h>

@class SenkoControl;

/* cydia-style modal install sheet for.deb updates */
@interface UpdateInstallVC : UIViewController {
    SenkoControl *_ctl;
    NSString *_path;
    UIProgressView *_bar;
    UILabel *_titleLbl;
    UILabel *_statusLbl;
    UITextView *_log;
    UIButton *_closeBtn;
    NSTimer *_creepTimer;
    float _progressFloor;
    float _progressCap;
    BOOL _finished;
    BOOL _ok;
}
- (id)initWithControl:(SenkoControl *)ctl packagePath:(NSString *)path;
@end

#endif
