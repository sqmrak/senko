#import "main_layout.h"
#import "ui_theme.h"

enum { kSenkoConnectTag = 8005 };
enum { kSenkoDomeBase = 128 }; /* layout size; screen size is transform */

static BOOL SenkoIsPad(void) {
    return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad;
}

/* height <= 568 needs compact metrics */
static BOOL SenkoIsCompactPhone(CGFloat height) {
    if (SenkoIsPad()) return NO;
    return height <= 568.0f; /* 4 inch and smaller */
}

static CGFloat MainTableWidth(CGFloat width, CGFloat height) {
    BOOL pad = SenkoIsPad();
    BOOL land = width > height;
    if (pad) {
        CGFloat margin = land ? 40.0f : 32.0f;
        CGFloat cap = land ? 1100.0f : 900.0f;
        CGFloat want = width - margin;
        if (want > cap) want = cap;
        if (want < 420.0f) want = (width > 420.0f) ? 420.0f : width;
        return want;
    }
    if (land && width > 700.0f)
        return 640.0f;
    return width;
}

/* connect bottom edge after scale */
static CGFloat DomeBottomY(CGFloat domeY, CGFloat scale) {
    return domeY + ((CGFloat)kSenkoDomeBase * scale) * 0.5f;
}

void SenkoLayoutMainContent(UIView *root,
                            CAGradientLayer *background,
                            UITableView *table,
                            UIButton *pingAll,
                            UILabel *status,
                            UIButton *connect,
                            UIColor *domeTop,
                            UIColor *domeBot,
                            CGFloat headerProgress) {
    if (!root || !table || !pingAll || !status) return;

    if (headerProgress < 0.0f) headerProgress = 0.0f;
    if (headerProgress > 1.0f) headerProgress = 1.0f;

    CGRect bounds = root.bounds;
    CGFloat W = bounds.size.width;
    CGFloat H = bounds.size.height;
    BOOL land = W > H;
    BOOL pad = SenkoIsPad();
    BOOL compact = SenkoIsCompactPhone(H);

    CGFloat width = MainTableWidth(W, H);
    CGFloat x = (W - width) / 2.0f;
    CGFloat top = GetTopOffset();

/* metrics are below status bar; leave room for ping/status under connect */
    CGFloat openCtrlY, shutCtrlY, openListY, shutListY, openDomeY, shutDomeY;
    CGFloat openDomeS, shutDomeS;

    if (pad) {
        if (land) {
            openDomeY = 108.0f; shutDomeY = 78.0f;
            openDomeS = 1.05f;  shutDomeS = 0.88f;
            openCtrlY = 200.0f; shutCtrlY = 140.0f;
            openListY = 250.0f; shutListY = 180.0f;
        } else {
/* pad open scale >1 so dome reads large */
            openDomeY = 150.0f; shutDomeY = 108.0f;
            openDomeS = 1.25f;  shutDomeS = 0.95f;
            openCtrlY = 270.0f; shutCtrlY = 180.0f;
            openListY = 330.0f; shutListY = 230.0f;
        }
    } else if (compact) {
/* 4": full-size on; clamps keep list/controls clear */
        if (land) {
            openDomeY = 64.0f;  shutDomeY = 50.0f;
            openDomeS = 0.60f;  shutDomeS = 0.50f;
            openCtrlY = 118.0f; shutCtrlY = 88.0f;
            openListY = 148.0f; shutListY = 112.0f;
        } else {
            openDomeY = 112.0f; shutDomeY = 82.0f;
            openDomeS = 1.00f;  shutDomeS = 0.62f;
            openCtrlY = 190.0f; shutCtrlY = 128.0f;
            openListY = 228.0f; shutListY = 160.0f;
        }
    } else {
/* non-compact portrait spacing */
        openDomeY = land ? 82.0f  : 128.0f;
        shutDomeY = land ? 60.0f  : 92.0f;
        openDomeS = land ? (96.0f / (CGFloat)kSenkoDomeBase) : 1.00f;
        shutDomeS = land ? (96.0f / (CGFloat)kSenkoDomeBase) : 0.62f;
        openCtrlY = land ? 122.0f : 210.0f;
        shutCtrlY = land ? 88.0f  : 134.0f;
        openListY = land ? 156.0f : 250.0f;
        shutListY = land ? 116.0f : 168.0f;
    }

    CGFloat controlY = openCtrlY + (shutCtrlY - openCtrlY) * headerProgress;
    CGFloat listTop  = openListY + (shutListY - openListY) * headerProgress;
    CGFloat domeY    = openDomeY + (shutDomeY - openDomeY) * headerProgress;
    CGFloat domeS    = openDomeS + (shutDomeS - openDomeS) * headerProgress;
    if (domeS < 0.48f) domeS = 0.48f;
    if (domeS > 1.35f) domeS = 1.35f;

    CGFloat btnH;
    if (pad) btnH = land ? 34.0f : 36.0f;
    else if (compact) btnH = land ? 26.0f : 28.0f;
    else btnH = land ? 28.0f : 30.0f;

/* dome top must clear title band (+ miside logo under senko) */
    CGFloat titleClear = compact ? 48.0f : (pad ? 58.0f : 52.0f);
    if (SenkoThemeIsMiside())
        titleClear = compact ? 74.0f : (pad ? 88.0f : 80.0f);
    CGFloat minDomeY = titleClear + ((CGFloat)kSenkoDomeBase * domeS) * 0.5f + 8.0f;
    if (domeY < minDomeY) domeY = minDomeY;

/* clamp so controls clear connect */
    CGFloat minCtrl = DomeBottomY(domeY, domeS) + 14.0f;
    if (controlY < minCtrl) controlY = minCtrl;

/* clamp so list clears controls */
    CGFloat minList = DomeBottomY(domeY, domeS) + 18.0f;
    CGFloat minListCtrl = controlY + btnH + 12.0f;
    if (minList < minListCtrl) minList = minListCtrl;
    if (listTop < minList) listTop = minList;

    CGFloat listHeight = H - listTop - top;
    if (listHeight < 80.0f) {
/* last resort scale shrink on tiny height */
        if (listHeight < 80.0f && domeS > 0.55f) {
            domeS = 0.55f;
            minCtrl = DomeBottomY(domeY, domeS) + 12.0f;
            if (controlY < minCtrl) controlY = minCtrl;
            minList = controlY + btnH + 10.0f;
            listTop = minList;
            listHeight = H - listTop - top;
        }
        if (listHeight < 0) listHeight = 0;
    }

    SenkoSetLayerFrame(background, bounds);
    table.frame = CGRectMake(x, listTop + top, width, listHeight);

    CGFloat buttonWidth;
    if (pad) buttonWidth = land ? 110.0f : 118.0f;
    else if (compact) buttonWidth = land ? 78.0f : 88.0f;
    else buttonWidth = land ? 88.0f : 96.0f;

    CGFloat sidePad = pad ? 18.0f : (compact ? 8.0f : 12.0f);
    CGRect pingFrame = CGRectMake(x + sidePad, controlY + top, buttonWidth, btnH);
    if (!CGRectEqualToRect(pingAll.frame, pingFrame))
        pingAll.frame = pingFrame;
    StyleGlossyCapsuleLayout(pingAll);

    CGFloat statusX = x + sidePad + buttonWidth + 8.0f;
    CGFloat statusWidth = width - (statusX - x) - sidePad;
    if (statusWidth < 72.0f) {
        statusX = x + sidePad;
        statusWidth = width - sidePad * 2.0f;
    }
    status.textAlignment = NSTextAlignmentCenter;
    status.frame = CGRectMake(statusX, controlY + top, statusWidth, btnH);

    UIView *well = [root viewWithTag:9001];
    if (well) {
/* phone: no side gutter; compact keeps flat corners */
        CGFloat wellInset = pad ? 10.0f : 0.0f;
        well.frame = CGRectMake(x + wellInset, listTop + top,
                                width - wellInset * 2.0f, listHeight);
        well.layer.cornerRadius = pad ? 12.0f : (compact ? 0.0f : 8.0f);
        well.layer.borderWidth = 0;
        well.layer.borderColor = [UIColor clearColor].CGColor;
        well.layer.shadowOpacity = 0;
        for (CALayer *layer in well.layer.sublayers) {
            if ([layer.name isEqualToString:@"wellGrad"])
                SenkoSetLayerFrame(layer, well.bounds);
        }
    }

    if (!connect || ![connect isKindOfClass:[UIButton class]]) {
        UIView *tagged = [root viewWithTag:kSenkoConnectTag];
        if ([tagged isKindOfClass:[UIButton class]])
            connect = (UIButton *)tagged;
        else
            connect = nil;
    }

    if (connect) {
        const CGFloat kBase = (CGFloat)kSenkoDomeBase;
        connect.transform = CGAffineTransformIdentity;
        connect.autoresizingMask = UIViewAutoresizingNone;

        BOOL sizeWrong =
            fabsf((float)(connect.bounds.size.width - kBase)) > 0.5f ||
            fabsf((float)(connect.bounds.size.height - kBase)) > 0.5f;
        if (sizeWrong) {
            connect.bounds = CGRectMake(0, 0, kBase, kBase);
            if (domeTop && domeBot) {
                ApplyGlossyDome(connect, domeTop, domeBot);
                [connect bringSubviewToFront:connect.titleLabel];
            }
        }

        connect.center = CGPointMake(CGRectGetMidX(bounds), top + domeY);
        connect.transform = CGAffineTransformMakeScale(domeS, domeS);
    }

    UIView *boy = [root viewWithTag:9002];
    if (boy)
        boy.frame = bounds;
}
