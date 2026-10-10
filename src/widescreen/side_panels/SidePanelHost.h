#pragma once

#include "SidePanel.h"
#include "SidePanelMenu.h"
#include "SidePanelMode.h"
#include <vector>

namespace widescreen {

struct SidePanelEntry {
    SidePanel *panel;
    uint8_t shown;    // modeBit() set: panel is in the swipe rotation for that mode
    uint8_t defaults; // modeBit() set: panel is shown first when entering that mode
};

// Owns the right-half LVGL display: shows one SidePanel at a time, picks the
// default side panel for the machine's mode, rotates through that mode's side panels on
// horizontal swipes, opens the side panel menu on a long press and feeds live telemetry.
class SidePanelHost {
  public:
    static SidePanelHost &instance();

    void add(SidePanel *panel, uint8_t shown, uint8_t defaults);
    void begin(lv_disp_t *disp); // call after the display is registered, on the LVGL thread

    const std::vector<SidePanelEntry> &entries() const { return panels; }
    void setShown(size_t index, SidePanelMode mode, bool on);
    void setDefault(size_t index, SidePanelMode mode, bool on);
    void menuClosed();
    SidePanelTheme currentTheme() const;

  private:
    SidePanelHost() = default;

    void loadFlags();
    void saveFlags(size_t index) const;
    SidePanelMode detectMode();
    bool isShown(int index) const;
    void showDefault();
    void show(int index);
    void hidePanel();
    void step(int direction);
    void updateDots();
    void sample(Telemetry &telemetry);

    static void onTimer(lv_timer_t *timer);
    static void onGesture(lv_event_t *event);
    static void onPress(lv_event_t *event);

    std::vector<SidePanelEntry> panels;
    SidePanelMenu menu;
    lv_obj_t *screen = nullptr;
    lv_obj_t *content = nullptr;
    lv_obj_t *dots = nullptr;
    int active = -1;
    SidePanelMode mode = SidePanelMode::Standby;
    int themeIndex = -1;
    uint32_t brewStartMs = 0;
    uint32_t lastBrewMs = 0;
    bool wasBrewing = false;
    bool hasBrewed = false;
    uint32_t pressStartMs = 0;
    lv_point_t pressStart{};
    bool longPressArmed = false;
};

} // namespace widescreen
