#import "qr_scan.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>
#import "quirc.h"

@interface QRScanVC () <AVCaptureVideoDataOutputSampleBufferDelegate>
@end

@implementation QRScanVC
@synthesize delegate = _delegate;

- (void)showScanTimeout {
    if (_done || !_hintLabel) return;
    _hintLabel.text = @"QR code not detected\nuse native AmneziaWG / WireGuard .conf\nlegacy and 2.0 formats";
}

- (void)dealloc {
    if (_qr) quirc_destroy(_qr);
    [_hintLabel release];
    [(AVCaptureSession *)_session release];
    [(AVCaptureVideoPreviewLayer *)_previewLayer release];
    if (_queue) dispatch_release(_queue);
    [super dealloc];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor blackColor];
    self.title = @"Scan native config";

    _hintLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _hintLabel.text = @"native AmneziaWG / WireGuard .conf\nlegacy and 2.0 formats\nAmnezia VPN vpn:// bundles are not supported";
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

    _qr = quirc_new();
    [self setupCapture];
}

- (void)cancelPressed {
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

- (void)setupCapture {
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
    sess.sessionPreset = AVCaptureSessionPreset640x480;
    if ([sess canAddInput:in]) [sess addInput:in];

    AVCaptureVideoDataOutput *out = [[[AVCaptureVideoDataOutput alloc] init] autorelease];
    out.alwaysDiscardsLateVideoFrames = YES;
    _queue = dispatch_queue_create("senko.qr", NULL);
    [out setSampleBufferDelegate:self queue:_queue];
    if ([sess canAddOutput:out]) [sess addOutput:out];
    [sess commitConfiguration];

    AVCaptureVideoPreviewLayer *pv =
        [[AVCaptureVideoPreviewLayer alloc] initWithSession:sess];
    pv.videoGravity = AVLayerVideoGravityResizeAspectFill;
    pv.frame = self.view.bounds;
    [self.view.layer addSublayer:pv];

    _session = sess;
    _previewLayer = pv;

    CGRect b = self.view.bounds;
    CGFloat box = b.size.width * 0.66;
    UIView *aim = [[[UIView alloc] initWithFrame:
        CGRectMake((b.size.width - box) / 2, (b.size.height - box) / 2, box, box)] autorelease];
    aim.backgroundColor = [UIColor clearColor];
    aim.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.8].CGColor;
    aim.layer.borderWidth = 2;
    aim.layer.cornerRadius = 8;
    aim.autoresizingMask = UIViewAutoresizingFlexibleTopMargin | UIViewAutoresizingFlexibleBottomMargin |
                           UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin;
    [self.view addSubview:aim];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    _done = NO;
    [_hintLabel setText:@"native AmneziaWG / WireGuard .conf\nlegacy and 2.0 formats\nAmnezia VPN vpn:// bundles are not supported"];
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(showScanTimeout) object:nil];
    [self performSelector:@selector(showScanTimeout) withObject:nil afterDelay:8.0];
    if (_session) [(AVCaptureSession *)_session startRunning];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    if (_session) [(AVCaptureSession *)_session stopRunning];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    ((AVCaptureVideoPreviewLayer *)_previewLayer).frame = self.view.bounds;
    _hintLabel.frame = CGRectMake(18, self.view.bounds.size.height - 92,
                                  self.view.bounds.size.width - 36, 76);
}

- (void)showNoCamera {
    UILabel *l = [[[UILabel alloc] initWithFrame:self.view.bounds] autorelease];
    l.text = @"No camera available";
    l.textColor = [UIColor whiteColor];
    l.textAlignment = NSTextAlignmentCenter;
    l.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:l];
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
}

- (void)hit:(NSString *)txt {
    if (_done) return;
    _done = YES;
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(showScanTimeout) object:nil];
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([_delegate respondsToSelector:@selector(qrScanner:didDecode:)])
            [_delegate qrScanner:self didDecode:txt];
    });
}

@end
