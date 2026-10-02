@echo off
REM Flash the prebuilt Luxor v9 without building.
REM Run from an "ESP-IDF 5.5 CMD" window:   FLASH_PREBUILT_V9.cmd COM5
REM The COM port moves (COM5 / COM8 / COM9) - check Device Manager.
cd /d "%~dp0"
if "%~1"=="" (
  echo Usage: FLASH_PREBUILT_V9.cmd COMx
  exit /b 1
)
python -m esptool --chip esp32s3 -p %1 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 luxor_v9.bin
if errorlevel 1 exit /b 1
idf.py -p %1 monitor
