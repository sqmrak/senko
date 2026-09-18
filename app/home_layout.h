#ifndef SENKO_HOME_LAYOUT_H
#define SENKO_HOME_LAYOUT_H

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

/* the status card owns its own pieces instead of hiding them behind tags: the
   layout pass runs on every scroll frame, and -viewWithTag: walks the subtree
   on each call */
@interface SenkoHomeCard : UIView {
@public
    UIView      *ring;  /* pulses while connecting */
    UIView      *orb;
    UIView      *core;  /* plain dot for every state but connected */
    UIImageView *glyph; /* shield once the tunnel carries traffic */
    UILabel     *state;
    UILabel     *traffic;
}
@end

/* the header still answers to tags because other screens look it up that way */
enum {
    SenkoHomeTagTitle    = 8001,
    SenkoHomeTagGear     = 8002,
    SenkoHomeTagPlus     = 8003,
    SenkoHomeTagConnect  = 8004,
    SenkoHomeTagCard     = 8010,
    SenkoHomeTagWell     = 9001
};

/* every view the home screen lays out, resolved once by the controller. the
   pointers are borrowed: the view hierarchy owns all of them */
typedef struct {
    UILabel         *title;
    UIButton        *gear;
    UIButton        *plus;
    SenkoHomeCard   *card;
    UIButton        *connect;
    UIButton        *check;
    UILabel         *detail;
    UITableView     *table;
    UIView          *well;
    CAGradientLayer *background;
} SenkoHomeChrome;

SenkoHomeCard *SenkoHomeBuildStatusCard(UIView *root);
NSString *SenkoFormatBytes(unsigned long long value);
void SenkoHomeApplyTraffic(const SenkoHomeChrome *ui, BOOL known,
                           uint64_t up, uint64_t down);

/* repaint the card, the header buttons, and both pills for the current theme */
void SenkoHomeStyleChrome(const SenkoHomeChrome *ui);

/* drive the card from the tunnel state: idle, connecting, connected, or error */
void SenkoHomeApplyStatus(const SenkoHomeChrome *ui, NSString *state,
                          NSString *title, BOOL animated);

/* place everything. headerProgress runs 0 to 1 and morphs the card from its
   full form into a single compact row. the list never moves: it keeps a fixed
   frame and a top inset, so a scroll frame costs no table relayout */
void SenkoHomeLayout(UIView *root, const SenkoHomeChrome *ui,
                     CGFloat headerProgress);

/* the same screen with the pre-rebuild hero: a dome connect button above the
   check and status pills, no status card. the header and the list are placed
   exactly as above, so everything the rebuild added still works underneath */
void SenkoHomeLayoutClassic(UIView *root, const SenkoHomeChrome *ui,
                            CGFloat headerProgress);

/* centre of the orb in root coordinates, for the status glow behind it */
CGPoint SenkoHomeOrbCenter(const SenkoHomeChrome *ui);

/* a filled pill has to label itself against its own fill: a theme is free to
   hand over a pale idle colour that white text disappears into */
UIColor *SenkoPillLabelColor(UIColor *fill);

#endif
