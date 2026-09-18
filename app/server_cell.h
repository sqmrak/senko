#ifndef SENKO_SERVER_CELL_H
#define SENKO_SERVER_CELL_H

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import "control_client.h"

/* the remark carries a country as a regional indicator pair. one decoder here
   keeps the row, the detail sheet, and the status line naming a server the
   same way */
NSString *SenkoServerFlagCode(NSString *remark);
NSString *SenkoServerDisplayName(NSString *remark);

@interface ServerCell : UITableViewCell {
    UIView *_plate;
    UIView *_accent;
    UILabel *_title;
    UILabel *_detail;
    UILabel *_transport;
    UILabel *_unsupported;
    UILabel *_ping;
    UIActivityIndicatorView *_pingActivity;
    UIButton *_pingButton;
    UIImageView *_serverIcon;
    UIImageView *_chevron;
    CAGradientLayer *_plateGrad;
}
- (void)configureWithServer:(SenkoServer *)server
                      picked:(BOOL)picked
                     pingVal:(NSNumber *)ping
                 displayName:(NSString *)displayName;
- (void)configureWithTitle:(NSString *)title
                     detail:(NSString *)detail
                     picked:(BOOL)picked
                     status:(NSString *)status;
- (void)setPingTarget:(id)target action:(SEL)action serverIndex:(int)serverIndex;
/* lift the row into place; the plate is the rasterized layer, so it is the one
   that has to move */
- (void)revealAtIndex:(NSUInteger)index;
@end

#endif
