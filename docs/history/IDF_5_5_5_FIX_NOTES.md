# Luxor - ESP-IDF 5.5.5 compatibility fix

This revision is intended for ESP-IDF 5.5.x (tested against the compiler behavior shown by ESP-IDF 5.5.5 / GCC 14.2 build logs).

Changes from the previous IDF 5.5 source package:

- Retains the component manifest range `idf: ">=5.4.2,<5.6.0"`.
- Fixes GCC 14 `-Werror=misleading-indentation` failures in `watch_main.c`, `board.c`, and `radio.c` without disabling warnings.
- Changes persisted `brightness` and `volume` storage variables to `uint32_t`, matching `nvs_get_u32()` / `nvs_set_u32()`.
- Keeps Luxor features, UI, hardware pin mapping, and COM-port behavior unchanged.
- Changes the internal log tag from the obsolete `Luxor-b23` label to `Luxor`.

Before rebuilding an older extracted tree, delete `build`, `dependencies.lock`, and `managed_components`, then run `idf.py set-target esp32s3` and `idf.py build`.
