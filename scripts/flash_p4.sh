#!/usr/bin/env bash
# Build and flash the Waveshare ESP32-P4 7B full GaggiMate UI (display-p4).
# Uses a Python 3.12 PlatformIO venv and esptool ≥5.3 (required for eco7 USB-JTAG).
#
# Default: flash the app partition only (0x10000) so NVS at 0x9000 is preserved
# (Wi‑Fi / AP password / settings). Writing firmware.factory.bin at 0x0 pads
# through NVS and wipes it.
#
# Full wipe (factory image, erases NVS): FULL_FLASH=1 ./scripts/flash_p4.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PORT="${UPLOAD_PORT:-/dev/ttyACM1}"
PIO_BIN="${ROOT}/.venv-pio/bin/pio"
ESPTOOL="${ROOT}/.venv-pio/bin/esptool"
ENV_NAME="${P4_ENV:-display-p4}"
BUILD="$ROOT/.pio/build/$ENV_NAME"

if [[ ! -x "$PIO_BIN" ]]; then
  echo "Missing .venv-pio — create with:"
  echo "  uv venv .venv-pio --python 3.12"
  echo "  uv pip install --python .venv-pio/bin/python 'platformio==6.1.19' pyyaml 'esptool>=5.3'"
  exit 1
fi

if [[ ! -e "$PORT" ]]; then
  echo "Upload port $PORT not found. Espressif USB-JTAG should be ttyACM1 (Pico is often ttyACM0)."
  exit 1
fi

"$PIO_BIN" run -e "$ENV_NAME"

ESPTOOL_ARGS=(--chip esp32p4 --port "$PORT" --baud 115200
  --before default-reset --after hard-reset
  write-flash -z --flash-mode qio --flash-freq 80m --flash-size 16MB)

if [[ "${FULL_FLASH:-0}" == "1" ]]; then
  echo "FULL_FLASH=1: writing factory image (NVS / Wi‑Fi settings will be erased)"
  "$ESPTOOL" "${ESPTOOL_ARGS[@]}" 0x0 "$BUILD/firmware.factory.bin"
else
  echo "Flashing app only (NVS preserved). Use FULL_FLASH=1 for a factory wipe."
  "$ESPTOOL" "${ESPTOOL_ARGS[@]}" 0x10000 "$BUILD/firmware.bin"
fi
echo "Flash OK. Monitor with: $PIO_BIN device monitor -e $ENV_NAME --port $PORT"
