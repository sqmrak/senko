#include "senko_motion.h"

#include <math.h>

static double cubic(double t, double first, double second) {
    double inverse = 1.0 - t;
    return 3.0 * inverse * inverse * t * first + 3.0 * inverse * t * t * second + t * t * t;
}

/* the cubic bezier with the end points (0, 0) and (1, 1). x has no closed form,
   so it is solved by bisection */
static double bezier(double time, double x1, double y1, double x2, double y2) {
    double lower = 0.0;
    double upper = 1.0;
    for (int i = 0; i < 32; ++i) {
        double middle = (lower + upper) * 0.5;
        if (cubic(middle, x1, x2) < time) lower = middle;
        else upper = middle;
    }
    return cubic((lower + upper) * 0.5, y1, y2);
}

/* every bezier below keeps both inner control points on the end levels (y1 = 0,
   y2 = 1), so the speed is exactly zero at the start and at the end. a start with
   speed makes the first frames jump (the old entry covered a fifth of its way in
   one frame at 60 Hz, a popup nearly half) and an end with speed reads as a stop
   against a wall. the x of the control points only moves the fastest part of the
   move earlier or later */

/* a damped spring of unit mass released at rest. the damping ratio is below 1,
   so the swing oscillates */
static double spring(double seconds, double stiffness, double damping) {
    double frequency = sqrt(stiffness);
    double decay = frequency * damping;
    double oscillation = frequency * sqrt(1.0 - damping * damping);
    return 1.0 - exp(-decay * seconds) *
        (cos(oscillation * seconds) + decay / oscillation * sin(oscillation * seconds));
}

double senko_motion_duration(senko_motion_curve curve) {
    switch (curve) {
    case SENKO_MOTION_ENTER: return 0.45;
    case SENKO_MOTION_EXIT: return 0.30;
    case SENKO_MOTION_DISMISS: return 0.38;
    case SENKO_MOTION_POPUP_ENTER: return 0.48;
    case SENKO_MOTION_POPUP_EXIT: return 0.30;
    case SENKO_MOTION_LIST_MOVE: return 0.38;
    case SENKO_MOTION_SPRING: return 0.62;
    case SENKO_MOTION_WOBBLE: return 0.76;
    case SENKO_MOTION_SETTLE: return 0.5;
    }
    return 0.0;
}

double senko_motion_progress(senko_motion_curve curve, double time) {
    double duration = senko_motion_duration(curve);
    if (duration == 0.0) return 0.0;
    if (!(time > 0.0)) return 0.0;
    if (time >= 1.0) return 1.0;
    switch (curve) {
    case SENKO_MOTION_ENTER: return bezier(time, 0.28, 0.0, 0.12, 1.0);
    case SENKO_MOTION_EXIT: return bezier(time, 0.4, 0.0, 0.6, 1.0);
    case SENKO_MOTION_DISMISS: return bezier(time, 0.32, 0.0, 0.2, 1.0);
    case SENKO_MOTION_POPUP_ENTER: return bezier(time, 0.3, 0.0, 0.1, 1.0);
    case SENKO_MOTION_POPUP_EXIT: return bezier(time, 0.45, 0.0, 0.7, 1.0);
    case SENKO_MOTION_LIST_MOVE: return bezier(time, 0.3, 0.0, 0.15, 1.0);
    /* a spring is stretched by its value at the end of the duration, so the last
       sample is exactly 1 */
    /* the springs are soft: a stiff spring is at rest at the start but covers
       seven percent of its way in the first frame, and the old loose one was
       still moving when it was cut off at the end of its time */
    case SENKO_MOTION_SPRING: return spring(time * duration, 360.0, 0.62) / spring(duration, 360.0, 0.62);
    case SENKO_MOTION_WOBBLE: return spring(time * duration, 330.0, 0.42) / spring(duration, 330.0, 0.42);
    case SENKO_MOTION_SETTLE: return spring(time * duration, 360.0, 0.78) / spring(duration, 360.0, 0.78);
    }
    return 0.0;
}
