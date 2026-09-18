#import <UIKit/UIKit.h>
#include <dispatch/dispatch.h>
#include <stddef.h>
#include <stdint.h>

typedef struct zbar_image_s zbar_image_t;
typedef struct zbar_image_scanner_s zbar_image_scanner_t;

@class QRScanVC;

@protocol QRScanDelegate <NSObject>
- (void)qrScanner:(QRScanVC *)s didDecode:(NSString *)text;
- (void)qrScannerDidCancel:(QRScanVC *)s;
@end

@interface QRScanVC : UIViewController {
@private
    id _session; /* retain the camera capture session */
    id _captureOutput; /* clear its delegate before the decoder is released */
    id _metadataOutput; /* the system detector, where the system has one */
    id _previewLayer; /* retain the preview layer attached to the view */
    dispatch_queue_t _queue; /* serialize frame decoding off the main thread */
    zbar_image_scanner_t *_zbarScanner;
    zbar_image_t *_zbarImage;
    uint8_t *_zbarPixels;
    size_t _zbarCapacity;
    BOOL _done;
    BOOL _captureSetup;
    UILabel *_hintLabel;
    UIView *_aimView;
    id<QRScanDelegate> _delegate;
}
@property (nonatomic, assign) id<QRScanDelegate> delegate;
@end
