#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

typedef struct {
    float imu[6], roll, pitch, yaw, battery_v, mic_dbfs, csi_rms;
    uint32_t steps, csi_frames, pairing_pin;
    int wifi_count, wifi_rssi, ble_count;
    bool imu_ok, rtc_ok, wifi_connected, ble_on, ble_connected, mic_on;
    bool time_synced, notification_pending;
    char wifi_lines[320], ble_lines[240], notice[128], radio_status[80];
} watch_state_t;
extern watch_state_t g_state;
extern portMUX_TYPE g_state_mux;
#define STATE_LOCK() portENTER_CRITICAL(&g_state_mux)
#define STATE_UNLOCK() portEXIT_CRITICAL(&g_state_mux)
void state_read(watch_state_t *out);
void state_notice(const char *s);
void state_radio_status(const char *s);
