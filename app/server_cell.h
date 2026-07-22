#ifndef SENKO_SERVER_CELL_H
#define SENKO_SERVER_CELL_H

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import "control_client.h"

@interface ServerCell : UITableViewCell {
    UIView *_plate;
    UIView *_accent;
    UILabel *_title;
    UILabel *_detail;
    UILabel *_unsupported;
    UILabel *_ping;
    UIImageView *_serverIcon;
    CAGradientLayer *_plateGrad;
}
- (void)configureWithServer:(SenkoServer *)server
                      picked:(BOOL)picked
                   hideLinks:(BOOL)hideLinks
                     pingVal:(NSNumber *)ping;
- (void)configureWithTitle:(NSString *)title
                     detail:(NSString *)detail
                     picked:(BOOL)picked
                     status:(NSString *)status;
@end

#endif
