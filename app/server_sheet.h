#ifndef SENKO_SERVER_SHEET_H
#define SENKO_SERVER_SHEET_H

#import <UIKit/UIKit.h>
#import "control_client.h"

/* the sheet reports intent and never touches the daemon itself, so the main
   controller keeps owning selection, connection, and list mutation */
extern NSString * const SenkoServerSheetActionConnect;
extern NSString * const SenkoServerSheetActionPing;
extern NSString * const SenkoServerSheetActionCopy;
extern NSString * const SenkoServerSheetActionEdit;
extern NSString * const SenkoServerSheetActionDelete;

@class SenkoServerSheet;

@protocol SenkoServerSheetDelegate <NSObject>
- (void)serverSheet:(SenkoServerSheet *)sheet
     didChooseAction:(NSString *)action
         serverIndex:(int)index;
@end

@interface SenkoServerSheet : UIView <UIGestureRecognizerDelegate> {
    SenkoServer *_server;
    id<SenkoServerSheetDelegate> _delegate; /* assigned, the owner outlives the sheet */
    UIView   *_backdrop;
    UIView   *_card;
    /* a phone on its side has under 200pt left for the card, which is less than
       the rows plus the connect pill need, so the body scrolls inside it */
    UIScrollView *_scroll;
    UIView   *_content;
    UIPanGestureRecognizer *_pan;
    UIView   *_grabber;
    UIButton *_close;
    UIImageView *_badge;
    UILabel  *_title;
    UILabel  *_subtitle;
    UIView   *_rows;
    UIButton *_primary;
    UIImageView *_primaryGlyph;
    UILabel  *_primaryTitle;
    NSArray  *_actionButtons;
    UILabel  *_pingValue;
    UILabel  *_linkValue;
    NSString *_link;
    NSNumber *_ping;
    NSString *_source;
    BOOL      _active;
    BOOL      _canMutate;
    BOOL      _dismissing;
    BOOL      _scrollHeldForDrag;
    CGFloat   _dragStart;
}

- (id)initWithServer:(SenkoServer *)server
                ping:(NSNumber *)ping
              source:(NSString *)source
              active:(BOOL)active
           canMutate:(BOOL)canMutate
            delegate:(id<SenkoServerSheetDelegate>)delegate;

/* the link arrives from the daemon after the sheet is already on screen */
- (void)setLink:(NSString *)link;

/* a ping started from the sheet reports back into the same row */
- (void)setPingResult:(NSNumber *)ms;

- (void)presentInView:(UIView *)host;
- (void)dismiss;

@end

#endif
