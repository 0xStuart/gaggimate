#pragma once

#include <cstdint>

namespace widescreen {

// What the machine is doing, as far as the side panels care. Each panel has a
// "shown" and a "default" flag per mode.
enum class PanelMode : uint8_t { Standby, BrewReady, Brewing, Steam, Water, Grind, Count };

constexpr int PANEL_MODE_COUNT = static_cast<int>(PanelMode::Count);

constexpr uint8_t modeBit(PanelMode mode) { return static_cast<uint8_t>(1u << static_cast<uint8_t>(mode)); }

inline const char *modeName(PanelMode mode) {
    switch (mode) {
    case PanelMode::Standby:
        return "Standby";
    case PanelMode::BrewReady:
        return "Brew ready";
    case PanelMode::Brewing:
        return "Brewing";
    case PanelMode::Steam:
        return "Steam";
    case PanelMode::Water:
        return "Hot water";
    case PanelMode::Grind:
        return "Grind";
    default:
        return "";
    }
}

} // namespace widescreen
