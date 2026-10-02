#!/usr/bin/env bash
#
# Luxor rework - clean build, flash and monitor from Ubuntu (including the
# Windows Subsystem for Linux, WSL2).
#
#   ./BUILD_FLASH_UBUNTU.sh                 build only
#   ./BUILD_FLASH_UBUNTU.sh /dev/ttyACM0    build, flash and monitor
#
# Expects ESP-IDF 5.5.5 to be installed.  If the environment is not already
# active the script sources it from the usual places.

set -euo pipefail
cd "$(dirname "$0")"

PORT="${1:-}"

# ---- 1. ESP-IDF environment -------------------------------------------------
if ! command -v idf.py >/dev/null 2>&1; then
    for candidate in "$HOME/esp/esp-idf-v5.5.5/export.sh" \
                     "$HOME/esp/esp-idf/export.sh" \
                     "/opt/esp/idf/export.sh"; do
        if [ -f "$candidate" ]; then
            echo "Sourcing $candidate"
            # shellcheck disable=SC1090
            . "$candidate"
            break
        fi
    done
fi

if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py not found.  Install ESP-IDF 5.5.5 and source export.sh first." >&2
    exit 1
fi

idf.py --version

# ---- 2. Host tests first ----------------------------------------------------
# These need no toolchain and no hardware, so a geometry or sign regression is
# caught before you wait on a firmware build.
echo
echo "=== host tests ==="
make -C tests

# ---- 3. Clean, because sdkconfig.defaults changed in the rework -------------
echo
echo "=== clean ==="
rm -f  sdkconfig sdkconfig.old dependencies.lock
rm -rf build managed_components

# ---- 4. Build ---------------------------------------------------------------
echo
echo "=== build ==="
idf.py set-target esp32s3
idf.py build

# ---- 5. Flash ---------------------------------------------------------------
if [ -z "$PORT" ]; then
    echo
    echo "Build finished.  To flash:"
    echo "  ls /dev/ttyACM* /dev/ttyUSB*      # find the board"
    echo "  ./BUILD_FLASH_UBUNTU.sh /dev/ttyACM0"
    exit 0
fi

if [ ! -e "$PORT" ]; then
    echo "ERROR: $PORT does not exist." >&2
    echo "Under WSL2 the board must be attached with usbipd-win first." >&2
    exit 1
fi

echo
echo "=== flash + monitor on $PORT  (Ctrl-] to leave the monitor) ==="
idf.py -p "$PORT" flash monitor
