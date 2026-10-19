# AGENTS.md

This worktree is `p4-port`: the GaggiMate display port for the **Waveshare ESP32-P4 7B**.

## Name

Call the board **Waveshare ESP32-P4 7B** in prose, comments, and UI. Directory names use `Waveshare-ESP32-P4-7B`. The C++ namespace is `waveshare_esp32_p4_7b`. The NVS namespace is `ws_p4_7b` (NVS names are limited to 15 characters).

Catalogue name: ESP32-P4-WIFI6-Touch-LCD-7B. The -C kit is the same 7-inch panel plus a camera. The Touch-LCD-7, Touch-LCD-8, and Touch-LCD-10.1 boards are different panels (720×1280 or 800×1280, different display driver, GT9271 touch) and are not this port. Do not call this display "widescreen", and do not shop for other panels.

## Board

1024×600 landscape, EK79007 MIPI-DSI, GT911 touch (I2C SDA 7 / SCL 8, reset 23), backlight PWM on GPIO32 (inverted), ESP32-C6 for Wi‑Fi 6 / BLE over ESP-Hosted (SDIO). This is a new platform, not a LilyGo/T-RGB drop-in.

The official 480×480 UI stays an island on the left. Side panels live in `src/Waveshare-ESP32-P4-7B/side_panels/` (outside upstream's `+<display/>` filters). Call them side panels (`SidePanel`, `SidePanelHost`, …), never plugins. Bare "panel" already means the LCD.

Simulator: `pio run -e display-sim-waveshare-esp32-p4-7b -t run` (driver in `sim-Waveshare-ESP32-P4-7B/`, needs `libsdl2-dev`). Browser build: `source ~/emsdk/emsdk_env.sh && python3 sim-web/build.py --serve`.

## Full UI firmware (`display-p4`)

`[env:display-p4]` builds the GaggiMate display UI with the 480×480 island on the left and side panels on the right. Flash with `scripts/flash_p4.sh` (`.venv-pio` + esptool ≥5.3). Default flashes the **app only** so NVS (Wi‑Fi / AP password) survives. Do not write `firmware.factory.bin` at `0x0` for routine updates — it pads through NVS and wipes settings. Full wipe: `FULL_FLASH=1 ./scripts/flash_p4.sh`.

**Glass check (2026-10-19, Stuart's 7B, rev v3.2 / eco7):** **GO** — factory image flashes; `WaveshareEsp32P47BDriver` / `P4DsiPanel` init 1024×600 RGB565; GT911 at `0x5D` (1024×600); ESP-Hosted Wi‑Fi AP + Web UI start. Optional real web bundle: run `scripts/build_webui.sh` before flash (otherwise an empty stub is embedded).

Practical notes:

- USB port is Espressif `303A:1001` → `/dev/ttyACM1` here (`ttyACM0` is the Pico). Prefer the `by-id` Espressif symlink.
- Platform: `pioarduino` **55.03.38** (Arduino 3.3.8). **55.03.31-2 bootloops** on eco7.
- Flash with **esptool ≥ 5.3** (5.6 works). PlatformIO's older bundled esptool fails USB-JTAG baud changes on this board.
- System Python 3.14 is too new for some pioarduino checks; use `.venv-pio` (Python 3.12).
- Board JSON / env use `default_16MB.csv` (app ~6.5 MB). Without that, `checkprogsize` fails against the 1.25 MB default app slot.
- BLE uses vendored `lib/esp-nimble-cpp` with `-DCONFIG_NIMBLE_CPP_IDF=1` (not NimBLE-Arduino). HomeKit is omitted on P4.
- Hosted host firmware (2.12.3) is newer than the stock C6 slave (reported 0.0.0); Wi‑Fi/BLE still work. C6 update is optional later.

## Hardware spike

`[env:display-p4-spike]` is an isolated USB flash + ESP-Hosted Wi‑Fi/BLE check under `src/Waveshare-ESP32-P4-7B/spike/`. It does **not** build the GaggiMate UI. Flash: `scripts/flash_p4_spike.sh`.

## Fork policy

`p4-port` is the **shareable** Waveshare ESP32-P4 7B port: same upstream UI in the left island, P4 driver + side panels only. Keep personal/fork-only features (ArduinoOTA, experimental UI, etc.) on a separate branch — do not bring them onto `p4-port`.
