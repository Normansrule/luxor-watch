@echo off
setlocal
cd /d "%~dp0"
echo === Luxor stable ESP-IDF 5.5.x build ===
echo Normalizing extracted file timestamps to prevent Ninja/CMake reconfigure loops...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$now=Get-Date; Get-ChildItem -LiteralPath . -Recurse -Force -File | ForEach-Object { $_.LastWriteTime=$now }"
if errorlevel 1 (
  echo ERROR: Could not normalize timestamps.
  exit /b 1
)

echo Cleaning generated build state...
if exist build rmdir /s /q build
if exist sdkconfig.old del /f /q sdkconfig.old

where idf.py >nul 2>nul
if errorlevel 1 (
  echo ERROR: idf.py is not available. Open an ESP-IDF 5.5 Command Prompt and run this script again.
  exit /b 1
)

echo Setting target esp32s3...
idf.py set-target esp32s3 || exit /b 1

echo Building Luxor...
idf.py build || exit /b 1

echo.
echo === BUILD SUCCEEDED ===
echo Flash with:
echo   idf.py -p <your COM port> flash monitor
endlocal
