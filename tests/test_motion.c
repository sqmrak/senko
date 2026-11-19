#include "senko_motion.h"

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_motion: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(void) {
    static const senko_motion_curve curves[] = {
        SENKO_MOTION_ENTER, SENKO_MOTION_EXIT, SENKO_MOTION_DISMISS, SENKO_MOTION_POPUP_ENTER,
        SENKO_MOTION_POPUP_EXIT, SENKO_MOTION_LIST_MOVE, SENKO_MOTION_SPRING, SENKO_MOTION_WOBBLE,
        SENKO_MOTION_SETTLE
    };
    for (size_t i = 0; i < sizeof curves / sizeof curves[0]; ++i) {
        senko_motion_curve curve = curves[i];
        double duration = senko_motion_duration(curve);
        CHECK(duration > 0.1 && duration < 1.0);
        /* the ends are exact, and a time outside the range or not a number is held to them */
        CHECK(senko_motion_progress(curve, 0.0) == 0.0);
        CHECK(senko_motion_progress(curve, 1.0) == 1.0);
        CHECK(senko_motion_progress(curve, -3.0) == 0.0);
        CHECK(senko_motion_progress(curve, 7.0) == 1.0);
        CHECK(senko_motion_progress(curve, NAN) == 0.0);
        CHECK(senko_motion_progress(curve, INFINITY) == 1.0);
        for (int step = 1; step < 100; ++step) {
            double value = senko_motion_progress(curve, step / 100.0);
            CHECK(isfinite(value));
            CHECK(value > -0.05 && value < 1.5);
        }
    }
    /* the first six are beziers and never go backwards, a spring does */
    for (size_t i = 0; i < 6; ++i) {
        double previous = 0.0;
        for (int step = 1; step <= 100; ++step) {
            double value = senko_motion_progress(curves[i], step / 100.0);
            CHECK(value >= previous - 1e-9);
            previous = value;
        }
    }
    /* the spring passes its target by about a tenth, the wobble by more and swings back
       past it again, and the settle stays nearly inside its track */
    {
        double wobble_peak = 0.0;
        double spring_peak = 0.0;
        double settle_peak = 0.0;
        int crossings = 0;
        int above = 0;
        for (int step = 1; step < 1000; ++step) {
            double t = step / 1000.0;
            double w = senko_motion_progress(SENKO_MOTION_WOBBLE, t);
            double s = senko_motion_progress(SENKO_MOTION_SPRING, t);
            double e = senko_motion_progress(SENKO_MOTION_SETTLE, t);
            if (w > wobble_peak) wobble_peak = w;
            if (s > spring_peak) spring_peak = s;
            if (e > settle_peak) settle_peak = e;
            if ((w > 1.0) != above) {
                ++crossings;
                above = w > 1.0;
            }
        }
        CHECK(wobble_peak > 1.2 && wobble_peak < 1.45);
        CHECK(spring_peak > 1.04 && spring_peak < 1.15);
        CHECK(settle_peak > 1.0 && settle_peak < 1.06);
        CHECK(crossings >= 3);
    }
    /* smoothness: a move that opens with speed jumps in its first frame and one that ends
       with speed stops against a wall. every curve covers almost nothing of its way in its
       first and its last frame at 60 Hz (a spring a little more at the start), and the beziers
       never run faster than four times
       the average speed */
    for (size_t i = 0; i < sizeof curves / sizeof curves[0]; ++i) {
        double frame = (1.0 / 60.0) / senko_motion_duration(curves[i]);
        double first = senko_motion_progress(curves[i], frame);
        double last = 1.0 - senko_motion_progress(curves[i], 1.0 - frame);
        /* a spring is a little livelier than a bezier but starts at rest as well */
        CHECK(first >= -0.001 && first < (i < 6 ? 0.03 : 0.05));
        CHECK(fabs(last) < 0.03);
        if (i < 6) {
            double previous = 0.0;
            for (int step = 1; step <= 1000; ++step) {
                double value = senko_motion_progress(curves[i], step / 1000.0);
                CHECK((value - previous) * 1000.0 < 4.0);
                previous = value;
            }
        }
    }
    CHECK(senko_motion_duration((senko_motion_curve)99) == 0.0);
    CHECK(senko_motion_progress((senko_motion_curve)99, 0.5) == 0.0);
    if (failures) return 1;
    puts("motion: curve ends, bounds, monotonic beziers, spring and wobble overshoot, soft start and stop passed");
    return 0;
}
