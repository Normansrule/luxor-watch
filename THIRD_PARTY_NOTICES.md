# Third-party notices

Luxor's SPD2010 direct-touch command flow is based on the public Espressif
`esp_lcd_touch_spd2010` implementation from esp-iot-solution, which is licensed
under Apache-2.0. The implementation in Luxor is adapted to use ESP-IDF's native
I2C master transmit/receive API so it does not depend on the incompatible panel-I/O
read path observed with this ESP-IDF 5.5 configuration.

Other managed dependencies retain their own upstream licenses.

## Fonts (v8)

`main/fonts/lux_inter_*.c` are generated from **Inter** by Rasmus Andersson and the Inter Project Authors, licensed under the SIL Open Font License 1.1 (`main/fonts/Inter-LICENSE.txt`). They also contain glyphs from **Font Awesome Free 5** (Fonticons, Inc.; icons CC BY 4.0, font SIL OFL 1.1), the same subset LVGL ships in its built-in fonts.
