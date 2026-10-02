#include "luxconf.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "luxconf";
static const char *NS = "luxor";

luxor_config_t lux_cfg;
static nvs_handle_t handle;
static bool opened;

void luxconf_defaults(luxor_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->version       = LUXCONF_VERSION;
    c->art           = 0;
    c->tilt_cal      = 0;
    c->brightness    = 60;
    c->volume        = 45;
    c->theme         = 0;
    c->imu_map       = (uint8_t)(LUX_IMU_MAP_DEFAULT & LUX_IMU_MAP_MASK);
    c->step_goal     = CONFIG_RW_STEP_GOAL;
    c->stride_cm     = CONFIG_RW_STRIDE_CM;
    c->sleep_s       = 30;
    c->clock_24h     = false;
    c->sweep_seconds = true;
}

void luxconf_validate(luxor_config_t *c)
{
    if (c->art >= 6)                              c->art = 0;
    if (c->brightness < 5 || c->brightness > 100) c->brightness = 60;
    if (c->volume > 100)                          c->volume = 45;
    if (c->theme > 5)                             c->theme = 0;
    c->imu_map &= LUX_IMU_MAP_MASK;
    if (c->step_goal < 1000 || c->step_goal > 40000) c->step_goal = CONFIG_RW_STEP_GOAL;
    if (c->stride_cm < 30 || c->stride_cm > 150)     c->stride_cm = CONFIG_RW_STRIDE_CM;
    if (c->sleep_s < 10 || c->sleep_s > 180)         c->sleep_s = 30;
    if (c->steps > 200000)    c->steps = 0;
    if (c->active_s > 86400)  c->active_s = 0;
    for (int i = 0; i < 3; i++) {
        if (!isfinite(c->gyro_bias[i]) || fabsf(c->gyro_bias[i]) > 60.0f) c->gyro_bias[i] = 0.0f;
    }
}

esp_err_t luxconf_load(void)
{
    luxconf_defaults(&lux_cfg);

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs a reset; erasing the preferences partition");
        if (nvs_flash_erase() == ESP_OK) e = nvs_flash_init();
    }
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable (%s); running on defaults", esp_err_to_name(e));
        return e;
    }

    e = nvs_open(NS, NVS_READWRITE, &handle);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed (%s); running on defaults", esp_err_to_name(e));
        return e;
    }
    opened = true;

    /* One blob keeps load/save atomic and makes a version bump trivial. */
    luxor_config_t stored;
    size_t n = sizeof(stored);
    if (nvs_get_blob(handle, "cfg", &stored, &n) == ESP_OK && n == sizeof(stored)) {
        uint8_t was = stored.version;
        lux_cfg = stored;

        if (was != LUXCONF_VERSION) {
            /* Corrected defaults have to reach a watch that already has a
             * stored blob, otherwise the old wrong value wins forever and the
             * only fix is erasing the flash.  Personal choices survive; the
             * two fields whose *defaults* changed are reset. */
            luxor_config_t d;
            luxconf_defaults(&d);
            lux_cfg.imu_map  = d.imu_map;
            lux_cfg.theme    = d.theme;
            lux_cfg.tilt_cal = 0;            /* run the calibration again */
            lux_cfg.version  = LUXCONF_VERSION;
            ESP_LOGW(TAG, "settings upgraded v%u -> v%u; axis map and theme "
                          "reset to the corrected defaults", was, LUXCONF_VERSION);
        }
        luxconf_validate(&lux_cfg);
        if (was != LUXCONF_VERSION) luxconf_save();
        ESP_LOGI(TAG, "settings restored (theme %u, imu_map 0x%X, goal %u)",
                 lux_cfg.theme, lux_cfg.imu_map, (unsigned)lux_cfg.step_goal);
    } else {
        ESP_LOGI(TAG, "no stored settings; writing defaults");
        luxconf_save();
    }
    return ESP_OK;
}

esp_err_t luxconf_save(void)
{
    if (!opened) return ESP_ERR_INVALID_STATE;
    luxconf_validate(&lux_cfg);
    esp_err_t e = nvs_set_blob(handle, "cfg", &lux_cfg, sizeof(lux_cfg));
    if (e == ESP_OK) e = nvs_commit(handle);
    if (e != ESP_OK) ESP_LOGW(TAG, "settings not saved: %s", esp_err_to_name(e));
    return e;
}
