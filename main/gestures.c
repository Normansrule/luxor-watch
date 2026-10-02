#include "gestures.h"
#include <string.h>

#define TAP_GAP_US       360000LL
#define TAP_RADIUS_PX    46

void luxor_gesture_init(luxor_gesture_state_t *g){
    if(g) memset(g, 0, sizeof(*g));
}
void luxor_gesture_reset(luxor_gesture_state_t *g){
    if(g) memset(g, 0, sizeof(*g));
}
void luxor_gesture_note_tap(luxor_gesture_state_t *g, int16_t x, int16_t y, int64_t now_us){
    if(!g) return;
    int32_t dx = x - g->anchor_x;
    int32_t dy = y - g->anchor_y;
    bool same_spot = g->taps && (dx*dx + dy*dy <= TAP_RADIUS_PX*TAP_RADIUS_PX);
    bool in_time = g->taps && (now_us - g->last_tap_us <= TAP_GAP_US);
    if(!same_spot || !in_time){
        g->taps = 1;
        g->anchor_x = x;
        g->anchor_y = y;
    } else if(g->taps < 3){
        g->taps++;
    }
    g->last_tap_us = now_us;
}
luxor_gesture_t luxor_gesture_poll(luxor_gesture_state_t *g, int64_t now_us){
    if(!g || !g->taps) return LUXOR_GESTURE_NONE;
    if(g->taps >= 3){
        luxor_gesture_reset(g);
        return LUXOR_GESTURE_TRIPLE_TAP;
    }
    if(now_us - g->last_tap_us > TAP_GAP_US){
        luxor_gesture_t out = g->taps == 2 ? LUXOR_GESTURE_DOUBLE_TAP : LUXOR_GESTURE_NONE;
        luxor_gesture_reset(g);
        return out;
    }
    return LUXOR_GESTURE_NONE;
}
