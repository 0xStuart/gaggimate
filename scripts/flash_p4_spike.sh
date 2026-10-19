#!/usr/bin/env bash
# Build and flash the Waveshare ESP32-P4 7B hardware spike.
# Uses a Python 3.12 PlatformIO venv (system python may be 3.14) and esptool ≥5.3
# (required for this board's eco7 / rev v3.2 USB-Serial/JTAG).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PORT="${UPLOAD_PORT:-/dev/ttyACM1}"
PIO_BIN="${ROOT}/.venv-pio/bin/pio"
ESPTOOL="${ROOT}/.venv-pio/bin/esptool"

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

"$PIO_BIN" run -e display-p4-spike
FACTORY="$ROOT/.pio/build/display-p4-spike/firmware.factory.bin"
"$ESPTOOL" --chip esp32p4 --port "$PORT" --baud 115200 \
  --before default-reset --after hard-reset \
  write-flash -z --flash-mode qio --flash-freq 80m --flash-size 16MB \
  0x0 "$FACTORY"
echo "Flash OK. Monitor with: $PIO_BIN device monitor -e display-p4-spike --port $PORT"
