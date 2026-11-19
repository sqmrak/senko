#ifndef SENKO_MOTION_H
#define SENKO_MOTION_H

#ifdef __cplusplus
extern "C" {
#endif

/* the curves of the interface. time and progress are fractions of the duration,
   and the progress of a spring passes 1. every bezier curve starts and ends at
   rest, so no move opens with a jump or stops against a wall; the ends of the
   springs are at rest as well. the shapes are tuned for smoothness here and are
   no longer the same numbers as the android build, yozora and exteria */
typedef enum senko_motion_curve {
    /* a view that arrives: it gathers speed gently and then settles for a long time */
    SENKO_MOTION_ENTER,
    /* a view that leaves: an even ease in and out */
    SENKO_MOTION_EXIT,
    /* a card that goes away: a soft start, then it leaves and settles at the edge */
    SENKO_MOTION_DISMISS,
    /* popups and sheets: a soft start and a long settle in, an ease that picks
       up speed late out */
    SENKO_MOTION_POPUP_ENTER,
    SENKO_MOTION_POPUP_EXIT,
    /* rows and cards that change place */
    SENKO_MOTION_LIST_MOVE,
    /* a spring that overshoots by a tenth and settles in one swing */
    SENKO_MOTION_SPRING,
    /* the loose spring of a sticker or a released button: two visible wobbles */
    SENKO_MOTION_WOBBLE,
    /* a thumb that settles with a barely visible overshoot */
    SENKO_MOTION_SETTLE
} senko_motion_curve;

/* seconds, zero for a curve that does not exist */
double senko_motion_duration(senko_motion_curve curve);
/* the progress at a time between 0 and 1, exactly 0 at 0 and exactly 1 at 1. a
   time outside the range or not a number is held to its end */
double senko_motion_progress(senko_motion_curve curve, double time);

#ifdef __cplusplus
}
#endif

#endif
