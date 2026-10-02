/* The tap language, exactly as the watch should speak it:
 *
 *   1 tap   -> nothing (on a lit screen; on a dark one the board layer wakes it)
 *   2 taps  -> HOME, delivered only after the triple-tap window has closed
 *   3 taps  -> OFF / ON, delivered immediately
 *
 * Taps on buttons never reach this recognizer at all -- the board layer
 * drops them at touch-down -- so these tests only need to cover taps on
 * empty background. */
#include <assert.h>
#include <stdio.h>
#include "../main/gestures.h"

#define MS 1000LL

static void pass(const char *what) { printf("PASS: %s\n", what); }

int main(void)
{
    luxor_gesture_state_t g;

    /* A single tap is never a gesture, no matter how long we wait. */
    luxor_gesture_init(&g);
    luxor_gesture_note_tap(&g, 200, 200, 1000 * MS);
    assert(luxor_gesture_poll(&g, 1100 * MS) == LUXOR_GESTURE_NONE);
    assert(luxor_gesture_poll(&g, 3000 * MS) == LUXOR_GESTURE_NONE);
    pass("1 tap does nothing on a lit screen");

    /* Two taps: nothing while a third could still arrive, then HOME. */
    luxor_gesture_init(&g);
    luxor_gesture_note_tap(&g, 100, 100, 1000 * MS);
    luxor_gesture_note_tap(&g, 104, 102, 1200 * MS);
    assert(luxor_gesture_poll(&g, 1300 * MS) == LUXOR_GESTURE_NONE);
    assert(luxor_gesture_poll(&g, 1600 * MS + 1) == LUXOR_GESTURE_DOUBLE_TAP);
    assert(luxor_gesture_poll(&g, 2000 * MS) == LUXOR_GESTURE_NONE);   /* fires once */
    pass("2 taps -> HOME, once, after the triple-tap window");

    /* Three taps: OFF/ON, immediately, and never also a double. */
    luxor_gesture_init(&g);
    luxor_gesture_note_tap(&g, 200, 200, 2000 * MS);
    luxor_gesture_note_tap(&g, 203, 198, 2200 * MS);
    luxor_gesture_note_tap(&g, 201, 201, 2380 * MS);
    assert(luxor_gesture_poll(&g, 2380 * MS + 1) == LUXOR_GESTURE_TRIPLE_TAP);
    assert(luxor_gesture_poll(&g, 3000 * MS) == LUXOR_GESTURE_NONE);
    pass("3 taps -> OFF/ON, immediately, not also a double");

    /* Two taps far apart on the glass are two unrelated taps. */
    luxor_gesture_init(&g);
    luxor_gesture_note_tap(&g, 10, 10, 3000 * MS);
    luxor_gesture_note_tap(&g, 150, 150, 3100 * MS);
    assert(luxor_gesture_poll(&g, 3500 * MS + 1) == LUXOR_GESTURE_NONE);
    pass("taps in different places do not combine");

    /* Two taps too slowly are two single taps. */
    luxor_gesture_init(&g);
    luxor_gesture_note_tap(&g, 200, 200, 4000 * MS);
    luxor_gesture_note_tap(&g, 200, 200, 4500 * MS);
    assert(luxor_gesture_poll(&g, 5000 * MS) == LUXOR_GESTURE_NONE);
    pass("slow taps do not combine");

    printf("\nALL GESTURE CHECKS PASSED\n");
    return 0;
}
