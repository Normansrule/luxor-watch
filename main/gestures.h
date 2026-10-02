#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LUXOR_GESTURE_NONE = 0,
    LUXOR_GESTURE_DOUBLE_TAP,
    LUXOR_GESTURE_TRIPLE_TAP,
} luxor_gesture_t;

typedef struct {
    uint8_t taps;
    int16_t anchor_x;
    int16_t anchor_y;
    int64_t last_tap_us;
} luxor_gesture_state_t;

void luxor_gesture_init(luxor_gesture_state_t *g);
void luxor_gesture_reset(luxor_gesture_state_t *g);
void luxor_gesture_note_tap(luxor_gesture_state_t *g, int16_t x, int16_t y, int64_t now_us);
luxor_gesture_t luxor_gesture_poll(luxor_gesture_state_t *g, int64_t now_us);
