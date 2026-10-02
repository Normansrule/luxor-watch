/*
 * Luxor smartwatch - startup and runtime loop.
 *
 * Boot order matters here.  The timezone is installed and the real-time clock
 * (RTC) chip is read *before* the user interface is built, so the very first
 * frame the user sees already shows the correct local time instead of "--:--"
 * followed by a jump a second later.  Everything that is optional - inertial
 * measurement unit (IMU), audio, Wi-Fi, Bluetooth Low Energy (BLE) - is started
 * afterwards and can never stop the watch from coming up.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "audio.h"
#include "battery.h"
#include "board.h"
#include "fitness.h"
#include "gestures.h"
#include "luxconf.h"
#include "motion.h"
#include "radio.h"
#include "state.h"
#include "ui.h"
#include "version.h"

static const char *TAG = "Luxor";

/* ---------------------------------------------------------------- state */

static motion_t              motion;
static luxor_fitness_t       fitness;
static luxor_gesture_state_t gestures;
static lux_batt_t            battery;   /* smoothed gauge, see battery.c */

static bool    screen_awake = true, standby, poweroff_requested;
static bool    time_valid, time_trusted;
static int64_t last_active;
static int64_t config_dirty_since;      /* 0 = nothing to write */

/* gyroscope zeroing */
static bool    gyro_zeroing;
static int64_t gyro_start;
static unsigned gyro_count;
static double  gyro_sum[3];

#define SAVE_DEBOUNCE_US 3000000LL      /* coalesce bursts of slider moves */

/* ---------------------------------------------------------- persistence */

static void config_touch(void)
{
    if (!config_dirty_since) config_dirty_since = esp_timer_get_time();
}

static void config_flush(void)
{
    lux_cfg.steps    = motion.steps;
    lux_cfg.active_s = fitness.active_seconds;
    lux_cfg.theme    = luxor_ui_theme_index();
    luxconf_save();
    config_dirty_since = 0;
}

/* ------------------------------------------------------------ timekeeping
 *
 * Three levels of confidence, in order of preference:
 *
 *   trusted   - came from the RTC chip, a Wi-Fi time sync or the user
 *   valid     - plausible, but seeded from the firmware build timestamp
 *   unset     - the clock shows placeholders and Settings nags for a time
 */

/* Parse the compiler's __DATE__ / __TIME__ into a local struct tm.  This is a
 * last resort: it is never right, but it is always within a few hours to days,
 * which beats 1970 and lets the calendar and the step-day rollover behave. */
static bool build_timestamp(struct tm *out)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;

    if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) != 3) return false;
    if (sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss) != 3) return false;

    const char *p = strstr(months, mon);
    if (!p) return false;

    memset(out, 0, sizeof(*out));
    out->tm_mon   = (int)(p - months) / 3;
    out->tm_mday  = day;
    out->tm_year  = year - 1900;
    out->tm_hour  = hh;
    out->tm_min   = mm;
    out->tm_sec   = ss;
    out->tm_isdst = -1;
    return true;
}

static void clock_apply(time_t utc, bool trusted)
{
    struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    time_valid = true;
    if (trusted) time_trusted = true;
}

/* Runs before the UI is created. */
static void clock_bootstrap(void)
{
    setenv("TZ", CONFIG_RW_TIMEZONE, 1);
    tzset();

    time_t rtc_utc = 0;
    bool   rtc_ok  = board_rtc_read(&rtc_utc);

    /* Anything before 2024 means the RTC lost power or was never set. */
    if (rtc_ok && rtc_utc > 1704067200) {
        clock_apply(rtc_utc, true);
        struct tm lt;
        localtime_r(&rtc_utc, &lt);
        ESP_LOGI(TAG, "clock: RTC gave %04d-%02d-%02d %02d:%02d:%02d local (%s)",
                 lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                 lt.tm_hour, lt.tm_min, lt.tm_sec, CONFIG_RW_TIMEZONE);
    } else {
        struct tm bt;
        if (build_timestamp(&bt)) {
            time_t seeded = mktime(&bt);
            if (seeded > 0) {
                clock_apply(seeded, false);
                ESP_LOGW(TAG, "clock: no valid RTC; seeded from the build "
                              "timestamp - set the time in Settings");
            }
        }
        if (!time_valid) ESP_LOGW(TAG, "clock: unset");
    }

    STATE_LOCK();
    g_state.rtc_ok = board_rtc_present();
    STATE_UNLOCK();
}

/* Push the current system time back out to the RTC chip so it survives a
 * reboot, and mark it trusted. */
static void clock_commit(time_t utc, const char *why)
{
    clock_apply(utc, true);
    bool ok = board_rtc_write(utc);
    STATE_LOCK();
    g_state.rtc_ok = ok;
    STATE_UNLOCK();
    ESP_LOGI(TAG, "clock: set from %s%s", why, ok ? " (saved to RTC)" : "");
}

/* ------------------------------------------------------------ power state */

static void wake_screen(void)
{
    if (standby) return;
    screen_awake = true;
    last_active  = esp_timer_get_time();
    board_backlight(lux_cfg.brightness);
    lv_disp_trig_activity(NULL);
}

static void enter_standby(void)
{
    if (standby) return;
    config_flush();
    audio_music_stop();
    audio_set_mic(false);
    radio_command(RADIO_ALL_OFF);
    board_imu_set_enabled(false);
    luxor_ui_home();
    luxor_gesture_reset(&gestures);
    standby      = true;
    screen_awake = false;
    board_set_dark_tap_capture(true);
    board_backlight(0);
    ESP_LOGI(TAG, "standby: screen/radios/IMU off; triple tap still wakes");
}

static void leave_standby(void)
{
    if (!standby) return;
    standby = false;
    board_imu_set_enabled(true);
    board_set_dark_tap_capture(false);
    luxor_gesture_reset(&gestures);
    luxor_ui_home();
    wake_screen();
}

/* ------------------------------------------------------- gyroscope zeroing */

static void start_gyro_zero(void)
{
    gyro_zeroing = true;
    gyro_count   = 0;
    memset(gyro_sum, 0, sizeof(gyro_sum));
    gyro_start = esp_timer_get_time();
    luxor_ui_toast("Hold still...");
}

static void service_gyro_zero(const imu_sample_t *s, int64_t us)
{
    if (!gyro_zeroing) return;

    if (motion_is_still(s)) {
        gyro_sum[0] += s->gx;
        gyro_sum[1] += s->gy;
        gyro_sum[2] += s->gz;
        gyro_count++;
    } else {
        gyro_count = 0;
        memset(gyro_sum, 0, sizeof(gyro_sum));
    }

    if (gyro_count >= 120) {                 /* ~2 s of still samples */
        for (int i = 0; i < 3; i++)
            lux_cfg.gyro_bias[i] = (float)(gyro_sum[i] / gyro_count);
        motion.yaw_deg = 0.f;
        gyro_zeroing   = false;
        config_flush();
        luxor_ui_toast("Gyroscope zeroed");
        ESP_LOGI(TAG, "gyro bias = %.2f %.2f %.2f deg/s",
                 lux_cfg.gyro_bias[0], lux_cfg.gyro_bias[1], lux_cfg.gyro_bias[2]);
    } else if (us - gyro_start > 9000000LL) {
        gyro_zeroing = false;
        luxor_ui_toast("Too much movement");
    }
}

/* ------------------------------------------------------------- UI commands */

static void handle_ui_command(const luxor_ui_event_t *e)
{
    if (e->cmd == LUXOR_UI_NONE) return;
    wake_screen();

    switch (e->cmd) {
    case LUXOR_UI_BRIGHTNESS:
        lux_cfg.brightness = (uint8_t)e->value;
        board_backlight(lux_cfg.brightness);
        config_touch();
        break;

    case LUXOR_UI_VOLUME:
        lux_cfg.volume = (uint8_t)e->value;
        audio_set_volume(lux_cfg.volume);
        config_touch();
        break;

    case LUXOR_UI_CLOCK_24H:
        lux_cfg.clock_24h = e->value != 0;
        config_touch();
        break;

    case LUXOR_UI_SWEEP_SECONDS:
        lux_cfg.sweep_seconds = e->value != 0;
        config_touch();
        break;

    case LUXOR_UI_STEP_GOAL:
        lux_cfg.step_goal  = (uint16_t)e->value;
        fitness.goal_steps = lux_cfg.step_goal;
        config_touch();
        break;

    case LUXOR_UI_STRIDE:
        lux_cfg.stride_cm  = (uint8_t)e->value;
        fitness.stride_cm  = lux_cfg.stride_cm;
        config_touch();
        break;

    case LUXOR_UI_SLEEP_TIMEOUT:
        lux_cfg.sleep_s = (uint8_t)e->value;
        config_touch();
        break;

    case LUXOR_UI_THEME_SAVE:
        lux_cfg.theme = luxor_ui_theme_index();
        config_touch();
        break;

    case LUXOR_UI_ART_SAVE:
        lux_cfg.art = (uint8_t)e->value;
        config_touch();
        break;

    case LUXOR_UI_TILT_CAL:
        if (e->value == 0xFF) {
            lux_cfg.tilt_cal = 0;            /* Settings: calibrate again */
        } else {
            lux_cfg.imu_map  = (uint8_t)(e->value & LUX_IMU_MAP_MASK);
            lux_cfg.tilt_cal = 1;
            board_imu_set_map(lux_cfg.imu_map);
            ESP_LOGI(TAG, "tilt calibrated: axis map = 0x%X", lux_cfg.imu_map);
        }
        config_flush();                      /* this one is worth the write */
        break;

    case LUXOR_UI_WIFI_TIME:
        audio_music_stop();
        radio_command(RADIO_WIFI_TIME);
        luxor_ui_toast("Asking the network...");
        break;

    case LUXOR_UI_RESET_STEPS:
        motion.steps           = 0;
        fitness.last_steps     = 0;
        fitness.active_seconds = 0;
        config_flush();
        luxor_ui_toast("Steps reset");
        break;

    case LUXOR_UI_GYRO_ZERO:
        start_gyro_zero();
        break;

    case LUXOR_UI_IMU_MAP:
        lux_cfg.imu_map = (uint8_t)(e->value & LUX_IMU_MAP_MASK);
        board_imu_set_map(lux_cfg.imu_map);
        config_touch();
        break;

    case LUXOR_UI_TIME_SAVE: {
        struct tm edit;
        luxor_ui_time_edit_get(&edit);
        edit.tm_isdst = -1;                  /* let the timezone rules decide */
        time_t t = mktime(&edit);
        if (t > 0) {
            clock_commit(t, "the Set time screen");
            luxor_ui_toast("Time saved");
        } else {
            luxor_ui_toast("Invalid date");
        }
        break;
    }

    case LUXOR_UI_MUSIC_PREV:   audio_music_previous(); break;
    case LUXOR_UI_MUSIC_NEXT:   audio_music_next();     break;
    case LUXOR_UI_MUSIC_TOGGLE:
        if (!audio_music_playing()) radio_command(RADIO_ALL_OFF);
        audio_music_toggle();
        break;

    default: break;
    }
}

/* --------------------------------------------------------------- gestures */

static void process_gestures(int64_t now_us)
{
    int16_t x, y;
    while (board_take_tap(&x, &y)) luxor_gesture_note_tap(&gestures, x, y, now_us);

    luxor_gesture_t g = luxor_gesture_poll(&gestures, now_us);
    if (g == LUXOR_GESTURE_TRIPLE_TAP) {
        if (standby) leave_standby(); else enter_standby();
        return;
    }
    if (g == LUXOR_GESTURE_DOUBLE_TAP && !standby) {
        luxor_ui_home();
        wake_screen();
    }
}

/* ----------------------------------------------------------------- render */

static float last_tilt_x, last_tilt_y, last_raw[6];

static void render_ui(void)
{
    if (standby || !screen_awake) return;

    struct timeval tv;
    gettimeofday(&tv, NULL);

    watch_state_t s;
    state_read(&s);

    luxor_ui_snapshot_t snap = {0};
    snap.now          = tv.tv_sec;
    snap.sub_us       = (uint32_t)tv.tv_usec;
    snap.time_valid   = time_valid;
    snap.time_trusted = time_trusted;
    snap.watch        = s;
    snap.touch_ok     = board_touch_ok();

    snap.tilt_x  = last_tilt_x;
    snap.tilt_y  = last_tilt_y;
    memcpy(snap.raw, last_raw, sizeof(snap.raw));
    snap.imu_map = board_imu_get_map();

    snap.goal_steps     = fitness.goal_steps;
    snap.goal_percent   = luxor_fitness_goal_percent(&fitness, s.steps);
    snap.distance_km    = luxor_fitness_distance_km(&fitness, s.steps);
    snap.active_seconds = fitness.active_seconds;

    snap.brightness    = lux_cfg.brightness;
    snap.volume        = lux_cfg.volume;
    snap.sleep_s       = lux_cfg.sleep_s;
    snap.stride_cm     = lux_cfg.stride_cm;
    snap.theme_index   = luxor_ui_theme_index();
    snap.art_index     = lux_cfg.art;
    snap.tilt_cal_done = lux_cfg.tilt_cal != 0;
    snap.clock_24h     = lux_cfg.clock_24h;
    snap.sweep_seconds = lux_cfg.sweep_seconds;

    snap.music_playing = audio_music_playing();
    snap.music_track   = audio_music_track();

    snap.battery_v       = lux_batt_volts(&battery);
    snap.battery_percent = lux_batt_percent(&battery);

    luxor_ui_render(&snap);
}

/* ======================================================================= */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Luxor %s boot ===", LUXOR_FW_VERSION);
    board_power_hold();

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs reset; erasing preferences partition");
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e != ESP_OK) ESP_LOGW(TAG, "NVS unavailable: %s", esp_err_to_name(e));

    luxconf_load();     /* falls back to validated defaults on any failure */

    motion_init(&motion, lux_cfg.steps);
    luxor_fitness_init(&fitness, lux_cfg.steps, lux_cfg.active_s,
                       lux_cfg.step_goal, lux_cfg.stride_cm);
    luxor_gesture_init(&gestures);

    /* --- 1/7 board ------------------------------------------------------ */
    ESP_LOGI(TAG, "1/7 board init");
    e = board_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "board_init failed: %s", esp_err_to_name(e));
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    /* Prime the gauge before the first frame, and log the raw reading so the
     * divider can be checked against a multimeter from the serial monitor. */
    lux_batt_init(&battery);
    {
        float v0 = board_battery_voltage();
        lux_batt_update(&battery, v0, 1.0f);
        ESP_LOGI(TAG, "battery: %.3f V -> %d %% (-1 = unknown)", (double)v0, lux_batt_percent(&battery));
    }

    /* --- 2/7 display + touch -------------------------------------------- */
    ESP_LOGI(TAG, "2/7 display + touch init");
    e = board_display_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "board_display_init failed: %s", esp_err_to_name(e));
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    board_set_dark_tap_capture(false);
    board_backlight(80);          /* unmistakable boot level */

    /* --- 3/7 clock, THEN the user interface ------------------------------
     * Reading the RTC here rather than three steps later is the whole fix for
     * "the watch shows the wrong time": the first frame is already correct. */
    ESP_LOGI(TAG, "3/7 clock init");
    clock_bootstrap();

    ESP_LOGI(TAG, "4/7 creating Luxor UI");
    luxor_ui_init(lux_cfg.theme);
    last_active = esp_timer_get_time();
    render_ui();
    for (int i = 0; i < 12; i++) {          /* pump LVGL before anything optional */
        lv_timer_handler();
        luxor_ui_service();
        vTaskDelay(pdMS_TO_TICKS(12));
    }
    board_backlight(lux_cfg.brightness);
    ESP_LOGI(TAG, "Luxor first frame submitted; brightness=%u%%", lux_cfg.brightness);

    /* --- 5/7 IMU -------------------------------------------------------- */
    ESP_LOGI(TAG, "5/7 IMU init");
    board_imu_set_map(lux_cfg.imu_map);
    e = board_imu_init();
    STATE_LOCK();
    g_state.imu_ok = e == ESP_OK;
    STATE_UNLOCK();
    if (e != ESP_OK) ESP_LOGW(TAG, "IMU unavailable: %s", esp_err_to_name(e));
    else ESP_LOGI(TAG, "IMU axis map = 0x%X (swap=%d invX=%d invY=%d invZ=%d)",
                  lux_cfg.imu_map,
                  !!(lux_cfg.imu_map & LUX_IMU_SWAP_XY),
                  !!(lux_cfg.imu_map & LUX_IMU_INV_X),
                  !!(lux_cfg.imu_map & LUX_IMU_INV_Y),
                  !!(lux_cfg.imu_map & LUX_IMU_INV_Z));

    /* --- 6/7 audio ------------------------------------------------------ */
    ESP_LOGI(TAG, "6/7 audio init");
    e = audio_init();
    if (e == ESP_OK) audio_set_volume(lux_cfg.volume);
    else ESP_LOGW(TAG, "Audio unavailable; continuing: %s", esp_err_to_name(e));

    /* --- 7/7 radios ----------------------------------------------------- */
    ESP_LOGI(TAG, "7/7 radio init");
    e = radio_init();
    state_radio_status(e == ESP_OK ? "Ready" : "Radio unavailable");
    if (e != ESP_OK) ESP_LOGW(TAG, "Radio unavailable; continuing: %s", esp_err_to_name(e));

    /* Taps only count towards double/triple-tap when they land on empty
     * background -- pressing a button twice is two presses, never "home". */
    board_set_tap_filter(luxor_ui_tap_is_background);

    ESP_LOGI(TAG, "=== Luxor %s running ===", LUXOR_FW_VERSION);

    /* ------------------------------------------------------------- loop - */
    int64_t imu_due = 0, previous_sample = 0, ui_due = 0, battery_due = 0, battery_last = 0;
    int64_t fitness_due = 0, pwr_down = 0, low_since = 0, boot_changed = 0, pwr_changed = 0;
    bool boot_was = false, boot_raw = false, low_warned = false, pwr_was = false;
    bool pwr_raw = false, pwr_stable = false;
    /* The PWR key is usually still held from switching the watch on.  Ignore
     * it until it has been seen released once, or that same press would be
     * read as "go home" -- or, held long enough, as "power off". */
    bool pwr_armed = !board_pwr_pressed();

    for (;;) {
        int64_t us = esp_timer_get_time();

        if (board_take_touch_wake() && !standby) wake_screen();
        process_gestures(us);
        if (screen_awake && !standby && lv_disp_get_inactive_time(NULL) < 500)
            last_active = us;

        /* ---- motion, ~60 Hz -------------------------------------------- */
        if (!standby && us >= imu_due) {
            imu_due = us + 17000;
            imu_sample_t sample;
            float raw[6];
            if (board_imu_read(&sample, raw)) {
                float dt = previous_sample ? (float)(us - previous_sample) / 1e6f
                                           : 1.f / 58.75f;
                previous_sample = us;

                bool raised = motion_update(&motion, &sample, dt,
                                            (uint32_t)(us / 1000),
                                            lux_cfg.gyro_bias);

                last_tilt_x = motion.tilt_x;
                last_tilt_y = motion.tilt_y;
                memcpy(last_raw, raw, sizeof(last_raw));

                STATE_LOCK();
                g_state.imu[0] = sample.ax; g_state.imu[1] = sample.ay;
                g_state.imu[2] = sample.az; g_state.imu[3] = sample.gx;
                g_state.imu[4] = sample.gy; g_state.imu[5] = sample.gz;
                g_state.steps  = motion.steps;
                g_state.roll   = motion.roll_deg;
                g_state.pitch  = motion.pitch_deg;
                g_state.yaw    = motion.yaw_deg;
                STATE_UNLOCK();

                if (raised && !screen_awake) wake_screen();
                service_gyro_zero(&sample, us);
            }
        }

        if (us >= fitness_due) {
            fitness_due = us + 1000000;
            luxor_fitness_tick(&fitness, motion.steps, (uint32_t)(us / 1000000));
        }

        /* ---- physical buttons ------------------------------------------ */
        bool boot = board_boot_pressed();
        if (boot != boot_raw) { boot_raw = boot; boot_changed = us; }
        if (us - boot_changed >= 40000 && boot_was != boot_raw) {
            boot_was = boot_raw;
            if (boot_was && !standby) { luxor_ui_home(); wake_screen(); }
        }

        /* Same 40 ms debounce as BOOT.  Without it a single electrical glitch
         * on release was a "short press" and sent the watch home. */
        bool pwr_now = board_pwr_pressed();
        if (pwr_now != pwr_raw) { pwr_raw = pwr_now; pwr_changed = us; }
        if (us - pwr_changed >= 40000) pwr_stable = pwr_raw;
        if (!pwr_armed && !pwr_stable) pwr_armed = true;
        bool pwr = pwr_armed && pwr_stable;
        if (pwr && !pwr_was) {
            pwr_down = us;
            if (standby) leave_standby(); else wake_screen();
        }
        if (pwr && pwr_down && us - pwr_down > 3000000) poweroff_requested = true;
        if (!pwr && pwr_was && !poweroff_requested && !standby) {
            luxor_ui_home();
            wake_screen();
        }
        pwr_was = pwr;

        if (poweroff_requested && !pwr) {
            config_flush();
            audio_music_stop();
            audio_set_mic(false);
            radio_command(RADIO_ALL_OFF);
            board_backlight(0);
            vTaskDelay(pdMS_TO_TICKS(250));
            board_shutdown();
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }

        /* ---- screen timeout, from Settings ----------------------------- */
        if (!standby && screen_awake &&
            us - last_active > (int64_t)lux_cfg.sleep_s * 1000000LL) {
            screen_awake = false;
            board_backlight(0);
        }

        /* ---- notifications and network time ---------------------------- */
        bool notice = false, synced = false;
        STATE_LOCK();
        notice = g_state.notification_pending; g_state.notification_pending = false;
        synced = g_state.time_synced;          g_state.time_synced = false;
        STATE_UNLOCK();
        if (notice) { audio_chime(); wake_screen(); }
        if (synced) {
            clock_commit(time(NULL), "the network");
            luxor_ui_toast("Time synced");
        }

        /* ---- battery and day rollover, 1 Hz ---------------------------- */
        if (us >= battery_due) {
            battery_due = us + (standby ? 5000000LL : 1000000LL);
            float dt = battery_last ? (float)(us - battery_last) / 1e6f : 1.0f;
            battery_last = us;
            lux_batt_update(&battery, board_battery_voltage(), dt);
            float v = lux_batt_volts(&battery);    /* filtered; -1 when unknown */
            STATE_LOCK();
            g_state.battery_v = v;
            STATE_UNLOCK();

            if (v > 0 && v < 3.45f) {
                if (!low_since) low_since = us;
                if (us - low_since > 10000000 && !low_warned) {
                    low_warned = true;
                    state_notice("Low battery: charge externally");
                }
            } else { low_since = 0; low_warned = false; }

            if (time_valid) {
                time_t t = time(NULL);
                struct tm local;
                localtime_r(&t, &local);
                uint32_t day = (uint32_t)((local.tm_year + 1900) * 1000 + local.tm_yday);
                if (lux_cfg.day_key && lux_cfg.day_key != day) {
                    motion.steps           = 0;
                    fitness.last_steps     = 0;
                    fitness.active_seconds = 0;
                    ESP_LOGI(TAG, "new day: step counters reset");
                }
                if (lux_cfg.day_key != day) { lux_cfg.day_key = day; config_touch(); }
            }
        }

        /* ---- user interface -------------------------------------------- */
        luxor_ui_event_t ev;
        while (luxor_ui_take_command(&ev)) handle_ui_command(&ev);

        if (us >= ui_due) {
            /* 20 Hz only where it shows - the sweeping second hand, the maze
             * ball, the bubble level.  Static screens redraw at 5 Hz, which
             * leaves far more time for LVGL and the radios. */
            int64_t period = standby ? 1000000LL
                           : luxor_ui_is_dynamic() ? 50000LL : 200000LL;
            ui_due = us + period;
            render_ui();
        }

        if (config_dirty_since && us - config_dirty_since > SAVE_DEBOUNCE_US)
            config_flush();

        lv_timer_handler();
        luxor_ui_service();
        vTaskDelay(pdMS_TO_TICKS(standby ? 25 : 5));
    }
}
