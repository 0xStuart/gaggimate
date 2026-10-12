#pragma once

#include "SidePanel.h"
#include "SidePanelMode.h"
#include <cstddef>

namespace waveshare_esp32_p4_7b {

class SidePanelHost;

// Full-height overlay on the right half, opened by a long press: lists the side
// panels, and per side panel shows "Show" / "Default" switches for every machine mode.
class SidePanelMenu {
  public:
    void open(lv_obj_t *screen, SidePanelHost &host);
    void close();
    bool isOpen() const { return root != nullptr; }

  private:
    void showList();
    void showPanel(size_t index);
    void refreshSwitches();
    lv_obj_t *addHeader(const char *title, const char *buttonText, lv_event_cb_t onButton);
    lv_obj_t *addButton(lv_obj_t *parent, const char *text, lv_coord_t width, lv_coord_t height);

    static void onDone(lv_event_t *event);
    static void onBack(lv_event_t *event);
    static void onPanelSelected(lv_event_t *event);
    static void onSwitch(lv_event_t *event);

    SidePanelHost *host = nullptr;
    SidePanelTheme theme{};
    lv_obj_t *root = nullptr;
    size_t selected = 0;
    lv_obj_t *switches[SIDE_PANEL_MODE_COUNT][2] = {};
};

} // namespace waveshare_esp32_p4_7b
