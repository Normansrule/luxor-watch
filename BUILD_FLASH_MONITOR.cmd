@echo off
setlocal
cd /d "%~dp0"
echo === Luxor rework: clean build + flash + monitor ===
echo.
if "%~1"=="" (
  echo No COM port given; esptool will auto-detect.
  echo Usage for an explicit port:  BUILD_FLASH_MONITOR.cmd COM9
  set PORTARG=
) else (
  echo Using port %~1
  set PORTARG=-p %~1
)
echo.
rem sdkconfig.defaults changed in the rework, so the old sdkconfig must go.
if exist sdkconfig del /f /q sdkconfig
if exist build rmdir /s /q build
if exist dependencies.lock del /f /q dependencies.lock
if exist managed_components rmdir /s /q managed_components
idf.py set-target esp32s3 || exit /b 1
idf.py build || exit /b 1
idf.py %PORTARG% flash monitor
endlocal
