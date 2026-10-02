#pragma once
/*
 * Luxor persistent configuration.
 *
 * Everything the watch remembers across a reboot lives in one struct so the UI,
 * the motion code and the boot sequence all read the same values, and so a
 * single validation pass can reject nonsense that was written by an older
 * firmware revision.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* ---- IMU axis mapping -------------------------------------------------
 *
 * The QMI8658 is soldered in whatever rotation suits the PCB, which is not the
 * rotation the user sees.  Rather than hard-coding a guess, Luxor maps the raw
 * sensor axes into a fixed "watch frame":
 *
 *      +X  towards 3 o'clock          (screen right)
 *      +Y  towards 12 o'clock         (screen up)
 *      +Z  out of the glass           (towards your eye)
 *
 * The mapping is a swap followed by per-axis inversion, stored as four bits so
 * it can be corrected on the watch itself (Settings -> Motion setup) without
 * rebuilding.  All eight physically reachable orientations are covered.
 */
#define LUX_IMU_SWAP_XY 0x01
#define LUX_IMU_INV_X   0x02
#define LUX_IMU_INV_Y   0x04
#define LUX_IMU_INV_Z   0x08
#define LUX_IMU_MAP_MASK 0x0F

/* Bumped whenever a *default* changes in a way that must reach watches that
 * already have settings stored.  On a version mismatch the fields listed in
 * luxconf_load() are reset to the new defaults and everything else is kept.
 *
 *   1  first release of the reworked firmware
 *   2  IMU default corrected (tilt up/down was inverted); white/gold theme
 *   3  IMU default no longer read from Kconfig -- a stale sdkconfig had
 *      been re-injecting the old value through luxconf_defaults()
 *   4  tilt direction is now self-calibrated on the watch; the flag is
 *      cleared here so every watch runs the calibration once
 */
#define LUXCONF_VERSION 4

/* The one axis map that has been verified on the Waveshare
 * ESP32-S3-Touch-LCD-1.46B with the USB-C connector at 6 o'clock: swap the
 * raw X and Y, invert nothing.  This is deliberately NOT a Kconfig symbol:
 * the value has to be the same no matter what sdkconfig happens to be lying
 * around in the build directory.  Correct it on the watch (Settings -> Motion
 * setup, or the flip control on the maze) if your board differs. */
#define LUX_IMU_MAP_DEFAULT (LUX_IMU_SWAP_XY)

typedef struct {
    uint8_t  version;         /* LUXCONF_VERSION at the time of writing     */
    uint8_t  art;             /* selected Display gallery plate, 0..5       */
    uint8_t  tilt_cal;        /* 1 once the maze has calibrated up/down     */
    uint8_t  brightness;      /* 5..100 %                                   */
    uint8_t  volume;          /* 0..100 %                                   */
    uint8_t  theme;           /* 0..5                                       */
    uint8_t  imu_map;         /* LUX_IMU_* bits                             */
    uint16_t step_goal;       /* 1000..40000                                */
    uint8_t  stride_cm;       /* 30..150                                    */
    uint8_t  sleep_s;         /* 10..180 screen timeout                     */
    bool     clock_24h;
    bool     sweep_seconds;   /* smooth second hand vs 1 Hz tick            */
    uint32_t steps;           /* today's step count                         */
    uint32_t active_s;        /* today's active seconds                     */
    uint32_t day_key;         /* year*1000 + yday the counters belong to    */
    float    gyro_bias[3];    /* deg/s, watch frame                         */
} luxor_config_t;

extern luxor_config_t lux_cfg;

void      luxconf_defaults(luxor_config_t *c);
void      luxconf_validate(luxor_config_t *c);
esp_err_t luxconf_load(void);
esp_err_t luxconf_save(void);
