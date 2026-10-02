#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "state.h"

/*
 * Luxor UI contract
 * -----------------
 * Physical orientation: the USB-C port is 6 o'clock / the bottom of the watch.
 * Every coordinate comes from layout.h, which tests/test_layout.c validates on
 * the host, so nothing important can drift outside the round display.
 */

typedef enum {
    LUXOR_APP_HOME = 0,
    /* the seven launcher applications, in honeycomb slot order */
    LUXOR_APP_DISPLAY,      /* centre    */
    LUXOR_APP_SETTINGS,     /* 12 o'clock */
    LUXOR_APP_MUSIC,        /*  2        */
    LUXOR_APP_CALCULATOR,   /*  4        */
    LUXOR_APP_CLOCK,        /*  6        */
    LUXOR_APP_MAZE,         /*  8        */
    LUXOR_APP_FITNESS,      /* 10        */
    /* screens reached from Settings rather than from the launcher */
    LUXOR_APP_TIMESET,
    LUXOR_APP_MOTION,
    LUXOR_APP_ABOUT,
    LUXOR_APP_COUNT,
} luxor_app_t;

#define LUXOR_LAUNCHER_APPS 7

typedef enum {
    LUXOR_UI_NONE = 0,
    LUXOR_UI_BRIGHTNESS,      /* value = 5..100        */
    LUXOR_UI_VOLUME,          /* value = 0..100        */
    LUXOR_UI_CLOCK_24H,       /* value = 0/1           */
    LUXOR_UI_SWEEP_SECONDS,   /* value = 0/1           */
    LUXOR_UI_STEP_GOAL,       /* value = new goal      */
    LUXOR_UI_STRIDE,          /* value = new stride cm */
    LUXOR_UI_SLEEP_TIMEOUT,   /* value = seconds       */
    LUXOR_UI_THEME_SAVE,
    LUXOR_UI_ART_SAVE,
    LUXOR_UI_TILT_CAL,       /* value: new map | 0x100 = calibrated; 0xFF = redo */
    LUXOR_UI_WIFI_TIME,
    LUXOR_UI_RESET_STEPS,
    LUXOR_UI_GYRO_ZERO,
    LUXOR_UI_IMU_MAP,         /* value = new axis map byte */
    LUXOR_UI_TIME_SAVE,
    LUXOR_UI_MUSIC_PREV,
    LUXOR_UI_MUSIC_TOGGLE,
    LUXOR_UI_MUSIC_NEXT,
} luxor_ui_command_t;

typedef struct {
    luxor_ui_command_t cmd;
    int32_t value;
} luxor_ui_event_t;

typedef struct {
    time_t   now;
    uint32_t sub_us;         /* microseconds into the current second */
    bool     time_valid;     /* the clock has been set at least once */
    bool     time_trusted;   /* set from the RTC or a real time source */

    watch_state_t watch;
    bool     touch_ok;       /* the SPD2010 touch controller answered at boot */

    float    tilt_x, tilt_y; /* screen-direction tilt, -1..1 */
    float    raw[6];         /* untouched IMU reading, for the Motion screen */
    uint8_t  imu_map;

    uint32_t goal_steps;
    uint8_t  goal_percent;
    float    distance_km;
    uint32_t active_seconds;

    unsigned brightness, volume;
    uint16_t sleep_s;
    uint16_t stride_cm;
    uint8_t  theme_index;
    uint8_t  art_index;      /* selected Display gallery plate */
    bool     tilt_cal_done;  /* maze has confirmed its up/down sense */
    bool     clock_24h, sweep_seconds;

    bool     music_playing;
    uint8_t  music_track;

    int      battery_percent;   /* -1 when unknown */
    float    battery_v;
} luxor_ui_snapshot_t;

void        luxor_ui_init(uint8_t initial_theme);
void        luxor_ui_open(luxor_app_t app);
void        luxor_ui_home(void);
luxor_app_t luxor_ui_current_app(void);
bool        luxor_ui_is_dynamic(void);   /* true when the screen wants 20 Hz */
void        luxor_ui_render(const luxor_ui_snapshot_t *s);
bool        luxor_ui_take_command(luxor_ui_event_t *out);
void        luxor_ui_service(void);
uint8_t     luxor_ui_theme_index(void);
void        luxor_ui_toast(const char *text);

/* Time editor, used by LUXOR_UI_TIME_SAVE. */
void        luxor_ui_time_edit_seed(time_t local_now);
void        luxor_ui_time_edit_get(struct tm *out);

/* True when (x, y) is on empty background rather than on a control. */
bool luxor_ui_tap_is_background(int16_t x, int16_t y);
