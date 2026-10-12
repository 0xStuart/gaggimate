#pragma once

#include <cstdint>
#include <lvgl.h>

namespace waveshare_esp32_p4_7b {

struct Telemetry {
    float temp = 0.0f;
    float targetTemp = 0.0f;
    float pressure = 0.0f;
    float targetPressure = 0.0f;
    float flow = 0.0f; // puck flow, matching the web UI chart
    float targetFlow = 0.0f;
    float weight = 0.0f;
    bool brewing = false;
    uint32_t brewMs = 0;
};

// Colours from the active GaggiMate theme (see theme_colors in the EEZ screens).
struct SidePanelTheme {
    lv_color_t text;
    lv_color_t background;
    lv_color_t accent;
    lv_color_t muted;
    lv_color_t red;
    lv_color_t blue;
    lv_color_t amber;
};

// A swappable view on the right half of the Waveshare ESP32-P4 7B. Only the visible
// side panel exists: create() builds its objects, destroy() is just deleting the parent.
class SidePanel {
  public:
    virtual ~SidePanel() = default;

    virtual const char *id() const = 0;   // stable key persisted in NVS, at most 12 characters
    virtual const char *name() const = 0; // shown in the side panel menu
    virtual void create(lv_obj_t *parent, const SidePanelTheme &theme) = 0;
    virtual void destroy() = 0;                          // drop pointers; the host deletes the objects
    virtual void update(const Telemetry &telemetry) = 0; // visible panel only
    virtual void observe(const Telemetry &) {}           // every panel, visible or not
};

} // namespace waveshare_esp32_p4_7b
