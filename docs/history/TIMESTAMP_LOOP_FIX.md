# Luxor ESP-IDF 5.5.x - Ninja/CMake reconfigure-loop fix

## Symptom
`idf.py build` repeatedly prints `[0/N] Re-running CMake...`, with N increasing, and never reaches normal compilation.

## Cause
The project was packaged on a UTC Linux environment and extracted on Windows in a UTC-7 locale. ZIP timestamps are timezone-naive, so Windows can preserve source-file timestamps several hours in the future. Ninja then sees CMake inputs newer than the generated build files and continuously regenerates CMake.

## Fix
Run `FIX_TIMESTAMP_AND_BUILD_IDF55.cmd` from an ESP-IDF 5.5 Command Prompt. It resets all extracted project-file timestamps to the current Windows time, deletes the generated build directory, sets the target to `esp32s3`, and rebuilds.

Manual equivalent from the `Luxor\firmware` directory:

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -Command "$now=Get-Date; Get-ChildItem -LiteralPath . -Recurse -Force -File | ForEach-Object { $_.LastWriteTime=$now }"
if exist build rmdir /s /q build
idf.py set-target esp32s3
idf.py build
```

After a successful build:

```cmd
idf.py -p <your COM port> flash monitor
```
