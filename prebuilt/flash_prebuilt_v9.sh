#!/usr/bin/env bash
# Flash the prebuilt Luxor v9 from Linux / WSL / macOS without ESP-IDF.
#   ./flash_prebuilt_v9.sh /dev/ttyACM0
set -euo pipefail
cd "$(dirname "$0")"
PORT="${1:-/dev/ttyACM0}"
VENV="$HOME/.venvs/esptool"
[ -x "$VENV/bin/python" ] || { python3 -m venv "$VENV" && "$VENV/bin/pip" install -q esptool; }
if command -v sha256sum >/dev/null; then sha256sum -c SHA256SUMS.txt; else shasum -a 256 -c SHA256SUMS.txt; fi
"$VENV/bin/python" -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 luxor_v9.bin
echo "Flashed. Watch the log with:  $VENV/bin/python -m serial.tools.miniterm $PORT 115200"
