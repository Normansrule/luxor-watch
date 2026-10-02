# Luxor ESP-IDF 5.5.5 runtime fix

The previous build flashed correctly but rebooted during touch initialization.

Observed runtime failure:

- `i2c_master_transmit_receive(...): i2c transmit buffer or size invalid`
- `SPD2010: read_fw_version(...): Rx failed`
- `ESP_ERROR_CHECK ... esp_lcd_touch_new_i2c_spd2010(...)`

Cause: the Espressif SPD2010 touch component uses commandless I2C reads through
`esp_lcd_panel_io_rx_param()`. With the ESP-IDF 5.5 new I2C master backend used by
this project, that path reaches a zero-length transmit phase and returns
`ESP_ERR_INVALID_ARG` before the touch data can be read.

Fix in this package:

1. The QSPI LCD path is unchanged.
2. SPD2010 touch at I2C address `0x53` now uses the native IDF 5.5 I2C master API
   directly (`i2c_master_transmit()` + `i2c_master_receive()`).
3. The touch protocol follows the SPD2010 command sequence used by Espressif's
   public driver, but avoids the incompatible commandless `esp_lcd_panel_io` read.
4. Touch initialization is non-fatal. If touch probing fails, the LCD and Luxor UI
   still continue so the watch cannot be trapped in a reboot loop just because
   touch is unavailable.
5. The UI is created before optional IMU/audio/radio initialization and the
   backlight is forced bright during boot.

Expected monitor lines include:

```
=== Luxor boot ===
1/7 board init
2/7 display + touch init
display: panel init complete
display: panel display ON command sent
touch: SPD2010 responded at 0x53
touch: direct SPD2010 I2C driver ready
display: LVGL registered; backlight forced to 80% for boot
3/7 creating Luxor UI
Luxor first frame submitted
```

If touch does not answer, the watch should instead log a warning and still draw
the UI.
