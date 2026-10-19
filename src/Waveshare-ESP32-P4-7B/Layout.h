// Geometry of the Waveshare ESP32-P4 7B (catalogue ESP32-P4-WIFI6-Touch-LCD-7B, 1024x600, EK79007).
// The official 480x480 round UI is centred in the left half; side panels own the right half.
#pragma once

// Panel is upside-down relative to the usual mounting. Display 180° is done in
// the EK79007 via MIPI 0x36 (UPDN+SHLR); GT911 coords are mirrored in software.
#define ROTATE_180 1

namespace waveshare_esp32_p4_7b {

constexpr int SCREEN_W = 1024;
constexpr int SCREEN_H = 600;

constexpr int HALF_W = SCREEN_W / 2;

constexpr int ISLAND_SIZE = 480;
constexpr int ISLAND_X = (HALF_W - ISLAND_SIZE) / 2;
constexpr int ISLAND_Y = (SCREEN_H - ISLAND_SIZE) / 2;

constexpr int SIDE_PANEL_X = HALF_W;
constexpr int SIDE_PANEL_W = SCREEN_W - SIDE_PANEL_X;
constexpr int SIDE_PANEL_H = SCREEN_H;

} // namespace waveshare_esp32_p4_7b
