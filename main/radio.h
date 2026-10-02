#pragma once
#include "esp_err.h"
typedef enum { RADIO_WIFI_SCAN, RADIO_WIFI_TIME, RADIO_WIFI_OFF, RADIO_BLE_ON,
 RADIO_BLE_OFF, RADIO_BLE_SCAN, RADIO_CSI_TOGGLE, RADIO_ALL_OFF, RADIO_ERASE_BONDS } radio_cmd_t;
esp_err_t radio_init(void);
void radio_command(radio_cmd_t command);
