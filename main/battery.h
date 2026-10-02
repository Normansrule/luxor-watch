#pragma once
/*
 * Battery gauge logic -- no ESP-IDF, no LVGL, so tests/test_battery.c can
 * run it on the host.
 *
 * board.c only measures a voltage (ADC1 channel 7 = GPIO8, through the
 * board's 3:1 divider).  Everything that turns that noisy number into a
 * calm percentage lives here:
 *
 *   - a first-order low-pass (time constant LUX_BATT_TAU_S) so backlight and
 *     radio load steps do not make the number jump,
 *   - a +/-1 % dead band so the shown value never flickers between neighbours,
 *   - "unknown" only after several bad reads in a row, never from one glitch.
 *
 * This is a voltage estimate, not a coulomb-counting fuel gauge.
 */
#include <stdbool.h>

#define LUX_BATT_TAU_S        20.0f  /* low-pass time constant, seconds   */
#define LUX_BATT_BAD_LIMIT    3      /* bad reads in a row before "--"    */
#define LUX_BATT_LOW_PERCENT  15     /* badge turns red at or below this  */

typedef struct {
    float v_filt;     /* filtered voltage, volts                          */
    int   shown;      /* percentage on screen, -1 when unknown            */
    int   bad;        /* consecutive invalid reads                        */
    bool  primed;     /* filter holds a real value                        */
} lux_batt_t;

void  lux_batt_init(lux_batt_t *b);

/* Single-cell lithium-polymer (LiPo) curve: volts -> 0..100 (float). */
float lux_batt_curve(float volts);

/* Feed one measurement; volts <= 0 means "the read failed". dt_s is the time
 * since the previous call. */
void  lux_batt_update(lux_batt_t *b, float volts, float dt_s);

/* Percentage to display, or -1 when unknown. */
int   lux_batt_percent(const lux_batt_t *b);

/* Filtered voltage, or -1 when unknown. */
float lux_batt_volts(const lux_batt_t *b);

/* Fill level of the battery glyph in pixels for a glyph interior `inner_w`
 * wide.  A non-zero charge always shows at least 1 px so "nearly empty" is
 * still distinguishable from "unknown". */
int   lux_batt_fill_px(int percent, int inner_w);
