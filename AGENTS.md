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

## Device notes

- Display env: `pio run -e display`. PIO binary: `~/.platformio/penv/bin/pio`.
- USB flash is Espressif `303A:1001`, not the Pico on `ttyACM0`.
- Official GitHub OTA is HTTPS pull only. LAN push works only after this fork’s toggle is on.
