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

CGFloat SenkoMainTableWidth(CGFloat width, CGFloat height) {
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

    CGFloat width = SenkoMainTableWidth(W, H);
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

/* leave a visible band below the classic wordmark. the old value put the dome
   eight points below the label's clearance on modern phone proportions */
    CGFloat titleClear = compact ? 54.0f : (pad ? 60.0f : 64.0f);
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

    /* keep the table itself still while its header spacer scrolls away. the
       old classic layout moved the whole table on every scroll callback, which
       made UIKit lay out each visible row again on an iPhone 4S. calculate the
       two endpoints after their collision clamps, not from this scroll frame. */
    CGFloat openDomeYFinal = openDomeY;
    CGFloat openDomeSFinal = openDomeS;
    CGFloat shutDomeYFinal = shutDomeY;
    CGFloat shutDomeSFinal = shutDomeS;
    if (openDomeSFinal < 0.48f) openDomeSFinal = 0.48f;
    if (openDomeSFinal > 1.35f) openDomeSFinal = 1.35f;
    if (shutDomeSFinal < 0.48f) shutDomeSFinal = 0.48f;
    if (shutDomeSFinal > 1.35f) shutDomeSFinal = 1.35f;
    CGFloat openMinDome = titleClear + ((CGFloat)kSenkoDomeBase * openDomeSFinal) * 0.5f + 8.0f;
    CGFloat shutMinDome = titleClear + ((CGFloat)kSenkoDomeBase * shutDomeSFinal) * 0.5f + 8.0f;
    if (openDomeYFinal < openMinDome) openDomeYFinal = openMinDome;
    if (shutDomeYFinal < shutMinDome) shutDomeYFinal = shutMinDome;
    CGFloat openControlYFinal = openCtrlY;
    CGFloat shutControlYFinal = shutCtrlY;
    CGFloat openMinCtrl = DomeBottomY(openDomeYFinal, openDomeSFinal) + 14.0f;
    CGFloat shutMinCtrl = DomeBottomY(shutDomeYFinal, shutDomeSFinal) + 14.0f;
    if (openControlYFinal < openMinCtrl) openControlYFinal = openMinCtrl;
    if (shutControlYFinal < shutMinCtrl) shutControlYFinal = shutMinCtrl;
    CGFloat openListTop = openListY;
    CGFloat shutListTop = shutListY;
    CGFloat openMinList = DomeBottomY(openDomeYFinal, openDomeSFinal) + 18.0f;
    CGFloat shutMinList = DomeBottomY(shutDomeYFinal, shutDomeSFinal) + 18.0f;
    CGFloat openMinListCtrl = openControlYFinal + btnH + 12.0f;
    CGFloat shutMinListCtrl = shutControlYFinal + btnH + 12.0f;
    if (openMinList < openMinListCtrl) openMinList = openMinListCtrl;
    if (shutMinList < shutMinListCtrl) shutMinList = shutMinListCtrl;
    if (openListTop < openMinList) openListTop = openMinList;
    if (shutListTop < shutMinList) shutListTop = shutMinList;
    shutListTop += top;
    CGFloat listSpacer = openListTop - (shutListTop - top);
    if (listSpacer < 0.0f) listSpacer = 0.0f;
    CGFloat listHeight = H - shutListTop;
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
    CGRect tableFrame = CGRectMake(x, shutListTop, width, listHeight);
    if (!CGRectEqualToRect(table.frame, tableFrame))
        table.frame = tableFrame;

    UIView *header = table.tableHeaderView;
    CGFloat headerH = header ? header.bounds.size.height : 0.0f;
    CGFloat headerW = header ? header.bounds.size.width : 0.0f;
    if ((fabsf((float)(headerH - listSpacer)) > 0.5f ||
         fabsf((float)(headerW - width)) > 0.5f) &&
        !table.tracking && !table.decelerating) {
        UIView *fresh = [[[UIView alloc] initWithFrame:
            CGRectMake(0.0f, 0.0f, width, listSpacer)] autorelease];
        fresh.backgroundColor = [UIColor clearColor];
        fresh.userInteractionEnabled = NO;
        table.tableHeaderView = fresh;
    }

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
/* the classic hero puts this line straight on the wallpaper, where a patterned
   theme swallows it. a dark slab beside the check capsule read as an empty
   text field, so the chip is cut from the same light the rest of the screen
   uses rather than from black. the card hero clears it again */
    status.backgroundColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.06f]
        : [UIColor colorWithWhite:1 alpha:0.10f];
    status.layer.cornerRadius = btnH * 0.5f;
    status.layer.masksToBounds = YES;
    status.layer.borderWidth = 0.5f;
    status.layer.borderColor = SenkoThemeIsLight()
        ? [UIColor colorWithWhite:0 alpha:0.10f].CGColor
        : [UIColor colorWithWhite:1 alpha:0.18f].CGColor;

    UIView *well = [root viewWithTag:9001];
    if (well) {
/* phone: no side gutter; compact keeps flat corners */
        CGFloat wellInset = pad ? 10.0f : 0.0f;
        well.frame = CGRectMake(x + wellInset, shutListTop,
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
        connect.autoresizingMask = UIViewAutoresizingNone;

        BOOL sizeWrong =
            fabsf((float)(connect.bounds.size.width - kBase)) > 0.5f ||
            fabsf((float)(connect.bounds.size.height - kBase)) > 0.5f;
        if (sizeWrong) {
            connect.transform = CGAffineTransformIdentity;
            connect.bounds = CGRectMake(0, 0, kBase, kBase);
            if (domeTop && domeBot) {
                ApplyGlossyDome(connect, domeTop, domeBot);
                [connect bringSubviewToFront:connect.titleLabel];
            }
        }

        CGPoint center = CGPointMake(CGRectGetMidX(bounds), top + domeY);
        if (!CGPointEqualToPoint(connect.center, center)) connect.center = center;
        CGAffineTransform scale = CGAffineTransformMakeScale(domeS, domeS);
        if (!CGAffineTransformEqualToTransform(connect.transform, scale))
            connect.transform = scale;
    }

    UIView *boy = [root viewWithTag:9002];
    if (boy)
        boy.frame = bounds;
}
