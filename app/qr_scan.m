#import "qr_scan.h"
#import "ui_theme.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>
#include <dlfcn.h>
#import "quirc.h"

@interface QRScanVC () <AVCaptureVideoDataOutputSampleBufferDelegate>
/* AVCaptureMetadataOutputObjectsDelegate does not exist in the ios 5 sdk this
   slice builds against, so the callback is declared instead of adopted */
- (void)captureOutput:(AVCaptureOutput *)out
didOutputMetadataObjects:(NSArray *)objects
       fromConnection:(AVCaptureConnection *)conn;
@end

/* AVMetadataObjectTypeQRCode is younger than the oldest sdk this app builds
   against, so it is resolved from the loaded framework. its value is
   "org.iso.QRCode"; the iso 18004 standard number is not what apple publishes,
   and asking for that string left every system without a working detector */
static NSString *SenkoQRMetadataType(void) {
    static NSString *cached = nil;
    if (cached) return cached;
    NSString * const *symbol = (NSString * const *)
        dlsym(RTLD_DEFAULT, "AVMetadataObjectTypeQRCode");
    if (symbol && *symbol) cached = [*symbol copy];
    if (!cached) cached = @"org.iso.QRCode";
    return cached;
}

@implementation QRScanVC
@synthesize delegate = _delegate;

- (void)showScanTimeout {
    if (_done || !_hintLabel) return;
    _hintLabel.text = @"QR code not detected\nfill the frame with the code\nand hold the phone still";
}

- (void)dealloc {
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    if (_captureOutput)
        [(AVCaptureVideoDataOutput *)_captureOutput setSampleBufferDelegate:nil queue:NULL];
    if (_metadataOutput) {
        SEL clearSel = NSSelectorFromString(@"setMetadataObjectsDelegate:queue:");
        if ([_metadataOutput respondsToSelector:clearSel])
            ((void (*)(id, SEL, id, dispatch_queue_t))objc_msgSend)
                (_metadataOutput, clearSel, nil, NULL);
    }
    if (_session) [(AVCaptureSession *)_session stopRunning];
    if (_queue) dispatch_sync(_queue, ^{});
    if (_qr) quirc_destroy(_qr);
    [_hintLabel release];
    [_aimView release];
    [_captureOutput release];
    [_metadataOutput release];
    [(AVCaptureSession *)_session release];
    [(AVCaptureVideoPreviewLayer *)_previewLayer release];
    if (_queue) dispatch_release(_queue);
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor blackColor];
    self.title = @"Scan QR";

    _hintLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _hintLabel.text = @"point the camera at a QR code\nserver link, subscription URL\nor a WireGuard / AmneziaWG .conf";
    _hintLabel.textColor = [UIColor whiteColor];
    _hintLabel.textAlignment = NSTextAlignmentCenter;
    _hintLabel.numberOfLines = 3;
    _hintLabel.font = [UIFont systemFontOfSize:14.0];
    _hintLabel.backgroundColor = [UIColor colorWithWhite:0 alpha:0.55];
    [self.view addSubview:_hintLabel];

    self.navigationItem.leftBarButtonItem =
        [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                                                       target:self
                                                       action:@selector(cancelPressed)] autorelease];

    [self setupCapture];
}

- (void)cancelPressed {
    _done = YES;
    if ([_delegate respondsToSelector:@selector(qrScannerDidCancel:)])
        [_delegate qrScannerDidCancel:self];
}

- (AVCaptureDevice *)backCamera {
    Class devClass = NSClassFromString(@"AVCaptureDevice");
    if (!devClass) return nil;
    if ([devClass respondsToSelector:@selector(devicesWithMediaType:)]) {
        for (AVCaptureDevice *d in [devClass devicesWithMediaType:AVMediaTypeVideo]) {
            if ([d position] == AVCaptureDevicePositionBack)
                return d;
        }
    }
    return [devClass defaultDeviceWithMediaType:AVMediaTypeVideo];
}

- (void)showCameraProblem:(NSString *)message {
    _hintLabel.text = message;
    _hintLabel.numberOfLines = 3;
    _hintLabel.hidden = NO;
}

/* a code that never comes into focus never decodes, and a camera left alone
   keeps whatever focus it happened to wake up with */
static void SenkoFocusForScanning(AVCaptureDevice *cam) {
    if (![cam lockForConfiguration:NULL]) return;
    if ([cam isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus])
        cam.focusMode = AVCaptureFocusModeContinuousAutoFocus;
    if ([cam isFocusPointOfInterestSupported])
        cam.focusPointOfInterest = CGPointMake(0.5f, 0.5f);
    if ([cam isExposureModeSupported:AVCaptureExposureModeContinuousAutoExposure])
        cam.exposureMode = AVCaptureExposureModeContinuousAutoExposure;
    /* smoothing exists for filming faces and holds a soft frame far longer
       than a code held in front of the lens can wait */
    SEL smoothSupported = NSSelectorFromString(@"isSmoothAutoFocusSupported");
    SEL setSmooth = NSSelectorFromString(@"setSmoothAutoFocusEnabled:");
    if ([cam respondsToSelector:smoothSupported] && [cam respondsToSelector:setSmooth] &&
        ((BOOL (*)(id, SEL))objc_msgSend)(cam, smoothSupported))
        ((void (*)(id, SEL, BOOL))objc_msgSend)(cam, setSmooth, NO);
    /* 1 is AVCaptureAutoFocusRangeRestrictionNear, and a code is always near */
    SEL rangeSupported = NSSelectorFromString(@"isAutoFocusRangeRestrictionSupported");
    SEL setRange = NSSelectorFromString(@"setAutoFocusRangeRestriction:");
    if ([cam respondsToSelector:rangeSupported] && [cam respondsToSelector:setRange] &&
        ((BOOL (*)(id, SEL))objc_msgSend)(cam, rangeSupported))
        ((void (*)(id, SEL, NSInteger))objc_msgSend)(cam, setRange, 1);
    [cam unlockForConfiguration];
}

/* the system detector reads codes this app used to decode by hand, and it
   reads the dense ones a hand decoder gives up on */
- (BOOL)addMetadataOutput:(AVCaptureSession *)sess {
    Class outClass = NSClassFromString(@"AVCaptureMetadataOutput");
    if (!outClass) return NO;
    id out = [[outClass alloc] init];
    SEL typesSel = NSSelectorFromString(@"availableMetadataObjectTypes");
    SEL setTypesSel = NSSelectorFromString(@"setMetadataObjectTypes:");
    SEL setDelegateSel = NSSelectorFromString(@"setMetadataObjectsDelegate:queue:");
    if (![out respondsToSelector:typesSel] || ![out respondsToSelector:setTypesSel] ||
        ![out respondsToSelector:setDelegateSel] || ![sess canAddOutput:out]) {
        [out release];
        return NO;
    }
    /* the type list stays empty until the output belongs to the session */
    [sess addOutput:out];
    NSString *qrType = SenkoQRMetadataType();
    NSArray *available = ((id (*)(id, SEL))objc_msgSend)(out, typesSel);
    if (![available containsObject:qrType]) {
        [sess removeOutput:out];
        [out release];
        return NO;
    }
    ((void (*)(id, SEL, id))objc_msgSend)(out, setTypesSel,
                                          [NSArray arrayWithObject:qrType]);
    ((void (*)(id, SEL, id, dispatch_queue_t))objc_msgSend)
        (out, setDelegateSel, self, dispatch_get_main_queue());
    _metadataOutput = out;
    return YES;
}

- (BOOL)addFrameOutput:(AVCaptureSession *)sess {
    _qr = quirc_new();
    if (!_qr) return NO;
    AVCaptureVideoDataOutput *out = [[AVCaptureVideoDataOutput alloc] init];
    out.alwaysDiscardsLateVideoFrames = YES;
    if (![sess canAddOutput:out]) {
        [out release];
        return NO;
    }
    [sess addOutput:out];

    /* the format list answers only once the output is in the session, and a
       format the device does not publish delivers no frames at all */
    NSArray *formats = [out availableVideoCVPixelFormatTypes];
    static const unsigned wantedOrder[] = {
        kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
        kCVPixelFormatType_32BGRA
    };
    unsigned picked = 0;
    for (size_t i = 0; i < sizeof wantedOrder / sizeof wantedOrder[0] && !picked; ++i) {
        if ([formats containsObject:[NSNumber numberWithUnsignedInt:wantedOrder[i]]])
            picked = wantedOrder[i];
    }
    if (picked)
        out.videoSettings = [NSDictionary dictionaryWithObject:
            [NSNumber numberWithUnsignedInt:picked]
                                    forKey:(NSString *)kCVPixelBufferPixelFormatTypeKey];

    _queue = dispatch_queue_create("senko.qr", NULL);
    [out setSampleBufferDelegate:self queue:_queue];
    _captureOutput = out;
    return YES;
}

- (void)setupCapture {
    if (_captureSetup || _done) return;
    Class devClass = NSClassFromString(@"AVCaptureDevice");
    if (!devClass) { [self showNoCamera]; return; }

    SEL statusSel = NSSelectorFromString(@"authorizationStatusForMediaType:");
    if ([devClass respondsToSelector:statusSel]) {
        NSInteger (*statusCall)(id, SEL, id) =
            (NSInteger (*)(id, SEL, id))[devClass methodForSelector:statusSel];
        NSInteger status = statusCall((id)devClass, statusSel, AVMediaTypeVideo);
        if (status == 0) {
            SEL requestSel = NSSelectorFromString(@"requestAccessForMediaType:completionHandler:");
            if (![devClass respondsToSelector:requestSel]) {
                [self showCameraProblem:@"Camera access could not be requested"];
                return;
            }
            void (*requestCall)(id, SEL, id, void (^)(BOOL)) =
                (void (*)(id, SEL, id, void (^)(BOOL)))[devClass methodForSelector:requestSel];
            requestCall((id)devClass, requestSel, AVMediaTypeVideo, ^(BOOL granted) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (_done) return;
                    if (granted) [self setupCapture];
                    else [self showCameraProblem:@"Camera access is disabled\nEnable it in Settings > Privacy > Camera"];
                });
            });
            return;
        }
        if (status == 1 || status == 2) {
            [self showCameraProblem:@"Camera access is disabled\nEnable it in Settings > Privacy > Camera"];
            return;
        }
    }

    AVCaptureDevice *cam = [self backCamera];
    if (!cam) {
        [self showNoCamera];
        return;
    }
    NSError *err = nil;
    AVCaptureDeviceInput *in = [AVCaptureDeviceInput deviceInputWithDevice:cam error:&err];
    if (!in) { [self showNoCamera]; return; }

    AVCaptureSession *sess = [[AVCaptureSession alloc] init];
    [sess beginConfiguration];
    if (![sess canAddInput:in]) {
        [sess commitConfiguration];
        [sess release];
        [self showNoCamera];
        return;
    }
    [sess addInput:in];

    /* a server link or a wireguard profile is hundreds of bytes, which is a
       code eighty modules across; 640x480 leaves about two pixels per module
       at reading distance, under what any decoder can work with */
    if ([sess canSetSessionPreset:AVCaptureSessionPreset1280x720])
        sess.sessionPreset = AVCaptureSessionPreset1280x720;
    else if ([sess canSetSessionPreset:AVCaptureSessionPreset640x480])
        sess.sessionPreset = AVCaptureSessionPreset640x480;
    [sess commitConfiguration];

    /* the outputs are attached after the session is configured, because what a
       detector reports it can read depends on the input and the preset */
    if (![self addMetadataOutput:sess] && ![self addFrameOutput:sess]) {
        [sess release];
        [self showNoCamera];
        return;
    }
    SenkoFocusForScanning(cam);

    AVCaptureVideoPreviewLayer *pv =
        [[AVCaptureVideoPreviewLayer alloc] initWithSession:sess];
    pv.videoGravity = AVLayerVideoGravityResizeAspectFill;
    pv.frame = self.view.bounds;
    [self.view.layer addSublayer:pv];

    _session = sess;
    _previewLayer = pv;
    _captureSetup = YES;

    _aimView = [[UIView alloc] initWithFrame:CGRectZero];
    _aimView.backgroundColor = [UIColor clearColor];
    _aimView.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.8].CGColor;
    _aimView.layer.borderWidth = 2;
    _aimView.layer.cornerRadius = 8;
    [self.view addSubview:_aimView];
    [self.view bringSubviewToFront:_hintLabel];
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
    if (self.view.window && !_done) [sess startRunning];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    _done = NO;
    [_hintLabel setText:@"point the camera at a QR code\nserver link, subscription URL\nor a WireGuard / AmneziaWG .conf"];
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(showScanTimeout) object:nil];
    [self performSelector:@selector(showScanTimeout) withObject:nil afterDelay:8.0];
    if (_session) [(AVCaptureSession *)_session startRunning];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    _done = YES;
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(showScanTimeout) object:nil];
    if (_session) [(AVCaptureSession *)_session stopRunning];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    ((AVCaptureVideoPreviewLayer *)_previewLayer).frame = self.view.bounds;
    UIEdgeInsets safe = SenkoSafeAreaInsets(self.view);
    CGFloat w = self.view.bounds.size.width;
    CGFloat h = self.view.bounds.size.height;
    CGFloat hintX = safe.left + 18.0f;
    CGFloat hintW = w - safe.left - safe.right - 36.0f;
    if (hintW < 120.0f) { hintX = 8.0f; hintW = w - 16.0f; }
    _hintLabel.frame = CGRectMake(hintX, h - safe.bottom - 92.0f, hintW, 76.0f);
    CGFloat availableW = w - safe.left - safe.right - 36.0f;
    CGFloat availableH = h - safe.top - safe.bottom - 120.0f;
    CGFloat box = MIN(availableW, availableH) * 0.68f;
    if (box > 320.0f) box = 320.0f;
    if (box < 120.0f) box = 120.0f;
    CGFloat centerX = safe.left + (w - safe.left - safe.right) * 0.5f;
    CGFloat centerY = safe.top + (h - safe.top - safe.bottom - 76.0f) * 0.5f;
    _aimView.frame = CGRectMake(floorf(centerX - box * 0.5f),
                                floorf(centerY - box * 0.5f), box, box);

    id preview = (id)_previewLayer;
    SEL connectionSel = NSSelectorFromString(@"connection");
    if (preview && [preview respondsToSelector:connectionSel]) {
        id (*getConnection)(id, SEL) = (id (*)(id, SEL))[preview methodForSelector:connectionSel];
        id connection = getConnection(preview, connectionSel);
        SEL supportsSel = NSSelectorFromString(@"isVideoOrientationSupported");
        SEL setSel = NSSelectorFromString(@"setVideoOrientation:");
        if ([connection respondsToSelector:supportsSel] && [connection respondsToSelector:setSel]) {
            BOOL (*supports)(id, SEL) = (BOOL (*)(id, SEL))[connection methodForSelector:supportsSel];
            if (supports(connection, supportsSel)) {
                void (*setOrientation)(id, SEL, NSInteger) =
                    (void (*)(id, SEL, NSInteger))[connection methodForSelector:setSel];
                setOrientation(connection, setSel,
                    (NSInteger)[UIApplication sharedApplication].statusBarOrientation);
            }
        }
    }
}

- (void)showNoCamera {
    [self showCameraProblem:@"No camera available"];
}

- (void)feedQuircFromBuffer:(CVImageBufferRef)img width:(size_t)w height:(size_t)h {
    if (_qrw != (int)w || _qrh != (int)h) {
        if (quirc_resize(_qr, (int)w, (int)h) >= 0) { _qrw = (int)w; _qrh = (int)h; }
    }
    if (_qrw != (int)w || _qrh != (int)h) return;

    int qw = 0, qh = 0;
    uint8_t *dst = quirc_begin(_qr, &qw, &qh);
    OSType fmt = CVPixelBufferGetPixelFormatType(img);

    if (fmt == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
        fmt == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
        uint8_t *base = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(img, 0);
        size_t stride = CVPixelBufferGetBytesPerRowOfPlane(img, 0);
        if (base) {
            for (size_t y = 0; y < h; ++y)
                memcpy(dst + y * w, base + y * stride, w);
        }
    } else {
        uint8_t *base = (uint8_t *)CVPixelBufferGetBaseAddress(img);
        size_t stride = CVPixelBufferGetBytesPerRow(img);
        if (base) {
            for (size_t y = 0; y < h; ++y) {
                uint8_t *row = base + y * stride;
                uint8_t *drow = dst + y * w;
                for (size_t x = 0; x < w; ++x) {
                    uint8_t b = row[x * 4 + 0];
                    uint8_t g = row[x * 4 + 1];
                    uint8_t r = row[x * 4 + 2];
                    drow[x] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
                }
            }
        }
    }
    quirc_end(_qr);
}

- (void)captureOutput:(AVCaptureOutput *)out
didOutputSampleBuffer:(CMSampleBufferRef)sb
       fromConnection:(AVCaptureConnection *)conn {
    (void)out; (void)conn;
    if (_done || !_qr) return;

    CVImageBufferRef img = CMSampleBufferGetImageBuffer(sb);
    if (!img) return;
/* this runs on the capture queue at the frame rate, and every decoded payload
   is an autoreleased string. without a pool of its own the whole scan session
   accumulates on an old device until jetsam takes the app */
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);

    size_t w = CVPixelBufferGetWidth(img);
    size_t h = CVPixelBufferGetHeight(img);
    OSType fmt = CVPixelBufferGetPixelFormatType(img);
    if (fmt == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
        fmt == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
        w = CVPixelBufferGetWidthOfPlane(img, 0);
        h = CVPixelBufferGetHeightOfPlane(img, 0);
    }

    if (w > 0 && h > 0)
        [self feedQuircFromBuffer:img width:w height:h];

    int n = quirc_count(_qr);
    for (int i = 0; i < n; ++i) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(_qr, i, &code);
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
            NSString *txt = [[[NSString alloc] initWithBytes:data.payload
                                                      length:(NSUInteger)data.payload_len
                                                    encoding:NSUTF8StringEncoding] autorelease];
            if ([txt length] == 0)
                txt = [[[NSString alloc] initWithBytes:data.payload
                                                length:(NSUInteger)data.payload_len
                                              encoding:NSISOLatin1StringEncoding] autorelease];
            if ([txt length]) { [self hit:txt]; break; }
        }
    }
    CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
    [pool release];
}

- (void)captureOutput:(AVCaptureOutput *)out
didOutputMetadataObjects:(NSArray *)objects
       fromConnection:(AVCaptureConnection *)conn {
    (void)out; (void)conn;
    if (_done) return;
    SEL stringSel = NSSelectorFromString(@"stringValue");
    for (id object in objects) {
        if (![object respondsToSelector:stringSel]) continue;
        NSString *text = ((id (*)(id, SEL))objc_msgSend)(object, stringSel);
        if ([text length]) {
            [self hit:text];
            return;
        }
    }
}

- (void)hit:(NSString *)txt {
    if (_done) return;
    _done = YES;
/* the metadata and sample buffer callbacks both land on the capture queue, and
   the timeout was scheduled on the main run loop: cancelling it from here
   touched another thread's run loop */
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSObject cancelPreviousPerformRequestsWithTarget:self
                                                 selector:@selector(showScanTimeout)
                                                   object:nil];
        if ([_delegate respondsToSelector:@selector(qrScanner:didDecode:)])
            [_delegate qrScanner:self didDecode:txt];
    });
}

@end
