#pragma once

#include "SidePanel.h"
#include <vector>

namespace widescreen {

// Owns the right-half LVGL display: shows one SidePanel at a time, switches on
// horizontal swipes, remembers the choice and feeds the panel live telemetry.
class PanelHost {
  public:
    static PanelHost &instance();

    void add(SidePanel *panel);
    void begin(lv_disp_t *disp); // call after the display is registered, on the LVGL thread

  private:
    PanelHost() = default;

    void show(int index);
    void updateDots();
    void sample(Telemetry &telemetry);
    PanelTheme currentTheme() const;

    static void onTimer(lv_timer_t *timer);
    static void onGesture(lv_event_t *event);

    std::vector<SidePanel *> panels;
    lv_obj_t *screen = nullptr;
    lv_obj_t *content = nullptr;
    lv_obj_t *dots = nullptr;
    int active = -1;
    int themeIndex = -1;
    uint32_t brewStartMs = 0;
    bool wasBrewing = false;
};

} // namespace widescreen
