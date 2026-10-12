#pragma once

#include <cstdint>

namespace waveshare_esp32_p4_7b {

// What the machine is doing, as far as the side panels care. Each panel has a
// "shown" and a "default" flag per mode.
enum class SidePanelMode : uint8_t { Standby, BrewReady, Brewing, Steam, Water, Grind, Count };

constexpr int SIDE_PANEL_MODE_COUNT = static_cast<int>(SidePanelMode::Count);

constexpr uint8_t modeBit(SidePanelMode mode) { return static_cast<uint8_t>(1u << static_cast<uint8_t>(mode)); }

inline const char *modeName(SidePanelMode mode) {
    switch (mode) {
    case SidePanelMode::Standby:
        return "Standby";
    case SidePanelMode::BrewReady:
        return "Brew ready";
    case SidePanelMode::Brewing:
        return "Brewing";
    case SidePanelMode::Steam:
        return "Steam";
    case SidePanelMode::Water:
        return "Hot water";
    case SidePanelMode::Grind:
        return "Grind";
    default:
        return "";
    }
}

} // namespace waveshare_esp32_p4_7b
