#import <UIKit/UIKit.h>
#include <dispatch/dispatch.h>

@class QRScanVC;

@protocol QRScanDelegate <NSObject>
- (void)qrScanner:(QRScanVC *)s didDecode:(NSString *)text;
- (void)qrScannerDidCancel:(QRScanVC *)s;
@end

@interface QRScanVC : UIViewController {
@private
    id _session; /* retain the camera capture session */
    id _previewLayer; /* retain the preview layer attached to the view */
    dispatch_queue_t _queue; /* serialize frame decoding off the main thread */
    struct quirc *_qr;
    int _qrw, _qrh;
    BOOL _done;
    UILabel *_hintLabel;
    id<QRScanDelegate> _delegate;
}
@property (nonatomic, assign) id<QRScanDelegate> delegate;
@end
