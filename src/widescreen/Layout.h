// Geometry of the 1024x600 widescreen panel (Waveshare ESP32-P4-WIFI6-Touch-LCD-7B).
// The official 480x480 round UI is centred in the left half; side panels own the right half.
#pragma once

namespace widescreen {

constexpr int SCREEN_W = 1024;
constexpr int SCREEN_H = 600;

constexpr int HALF_W = SCREEN_W / 2;

constexpr int ISLAND_SIZE = 480;
constexpr int ISLAND_X = (HALF_W - ISLAND_SIZE) / 2;
constexpr int ISLAND_Y = (SCREEN_H - ISLAND_SIZE) / 2;

constexpr int PANEL_X = HALF_W;
constexpr int PANEL_W = SCREEN_W - PANEL_X;
constexpr int PANEL_H = SCREEN_H;

} // namespace widescreen
