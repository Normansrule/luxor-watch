#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "lvgl.h"
#include "motion.h"

/* ---- power / buttons -------------------------------------------------- */
void      board_power_hold(void);
void      board_shutdown(void);
bool      board_pwr_pressed(void);
bool      board_boot_pressed(void);

/* ---- bring-up --------------------------------------------------------- */
esp_err_t board_init(void);
esp_err_t board_display_init(void);
bool      board_touch_ok(void);

/* ---- display ---------------------------------------------------------- */
void      board_backlight(unsigned percent);   /* 0..100, perceptually spaced */

/* ---- battery ---------------------------------------------------------- */
float     board_battery_voltage(void);         /* volts at the cell, or -1; percent: battery.h */

/* ---- inertial measurement unit ---------------------------------------- */
esp_err_t board_imu_init(void);
void      board_imu_set_enabled(bool enabled);

/*
 * Read one sample.  "out" is delivered in the Luxor watch frame
 * (+X = 3 o'clock, +Y = 12 o'clock, +Z = out of the glass) after the axis map
 * has been applied; "raw_out" - optional - is the untouched sensor reading,
 * which the Motion setup screen shows so a wrong map is obvious.
 */
bool      board_imu_read(imu_sample_t *out, float raw_out[6]);
void      board_imu_set_map(uint8_t map);
uint8_t   board_imu_get_map(void);

/* ---- real-time clock chip --------------------------------------------- */
bool      board_rtc_read(time_t *utc);
bool      board_rtc_write(time_t utc);
bool      board_rtc_present(void);

/* ---- touch plumbing used by the gesture layer ------------------------- */
bool      board_take_touch_wake(void);
void      board_set_dark_tap_capture(bool enable);
bool      board_take_tap(int16_t *x, int16_t *y);

/* Called at touch-down while the screen is lit; return true if a tap at
 * (x, y) may count towards double/triple-tap gestures. */
void board_set_tap_filter(bool (*fn)(int16_t x, int16_t y));
