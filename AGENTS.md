# AGENTS.md

This is a **personal fork** of [jniebuhr/gaggimate](https://github.com/jniebuhr/gaggimate), not upstream.

## Fork-only work

Keep the **This fork** list in [README.md](README.md) current whenever fork behaviour is added or changed.

Current fork features:

1. **Scale-ready brew confirm** — Wait for a BLE *weight sample* (`isBluetoothScaleHealthy()`), not GATT `isConnected()`. Reuse the existing brew-confirm overlay (Back / Start anyway). Do not add an EEZ screen for this.
2. **Network firmware upload** — System setting `networkOta` (NVS `n_ota`), default **off**. ArduinoOTA on port 3232, no password, STA only. Plugin: `src/display/plugins/ArduinoOTAPlugin.*`. Do **not** open an upstream PR for this.

## Do not

- PR ArduinoOTA / local unsigned upload / custom OTA URL to `jniebuhr/gaggimate` unless Stuart asks.
- Treat BLE GATT connect as “scale ready”.
- Print or repeat Wi‑Fi / Home Assistant passwords from `/api/settings`.

## Widescreen display watch

Stuart wants a **1024×600-class** ESP32 touch panel (Waveshare ESP32-S3-Touch-LCD-7B is the current favourite) so the official **480×480** circular UI can sit on the left and LVGL graphs on the right. Fork is OK if official UI stays an untouched 480×480 LVGL island (two logical displays, one framebuffer).

**Keep watching for boards that:**
- ≥ **1024×600** capacitive touch (800×480 is too narrow for a brew graph)
- ESP32-S3 + 8MB PSRAM, Arduino-capable, so GaggiMate’s display stack can be reused
- Preferably **less Wi‑Fi vs RGB contention** than the 7B (IDF 5.1+ bounce buffers, MIPI/DSI, or ESP32-P4 — P4 is a *new* platform, call that out)
- In stock in the UK / EU when possible

**Not a candidate:** 2.1" rounds, 1.43/1.75" AMOLEDs, 4" 480 squares, Elecrow-style **800×480** 5–7" panels.

If a better board shows up, tell Stuart; do not buy or start a driver until he asks.

## Device notes

- Display env: `pio run -e display`. PIO binary: `~/.platformio/penv/bin/pio`.
- USB flash is Espressif `303A:1001`, not the Pico on `ttyACM0`.
- Official GitHub OTA is HTTPS pull only. LAN push works only after this fork’s toggle is on.
