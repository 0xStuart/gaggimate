#!/usr/bin/env bash
# Build and flash the Waveshare ESP32-P4 7B full GaggiMate UI (display-p4).
# Uses a Python 3.12 PlatformIO venv and esptool ≥5.3 (required for eco7 USB-JTAG).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PORT="${UPLOAD_PORT:-/dev/ttyACM1}"
PIO_BIN="${ROOT}/.venv-pio/bin/pio"
ESPTOOL="${ROOT}/.venv-pio/bin/esptool"
ENV_NAME="${P4_ENV:-display-p4}"

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
FACTORY="$ROOT/.pio/build/$ENV_NAME/firmware.factory.bin"
"$ESPTOOL" --chip esp32p4 --port "$PORT" --baud 115200 \
  --before default-reset --after hard-reset \
  write-flash -z --flash-mode qio --flash-freq 80m --flash-size 16MB \
  0x0 "$FACTORY"
echo "Flash OK. Monitor with: $PIO_BIN device monitor -e $ENV_NAME --port $PORT"
