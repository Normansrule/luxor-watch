# Prebuilt Luxor v9

Built with ESP-IDF 5.5.5 for esp32s3 from this folder's sources (LVGL 8.3.11, esp_lcd_spd2010 2.0.0~1). Use it if a local build fails; building yourself is still the normal path.

Windows (ESP-IDF 5.5 CMD):

```powershell
cd <this folder>\prebuilt
FLASH_PREBUILT_V9.cmd COM5
```

Linux, WSL or macOS (no ESP-IDF needed; checks the hashes first):

```bash
./prebuilt/flash_prebuilt_v9.sh /dev/ttyACM0
```

`SHA256SUMS.txt` lists the file hashes. Note: `idf.py monitor` needs to run from the project folder to decode addresses; from here it still shows the log.
