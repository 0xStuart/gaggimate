#pragma once

#include "SidePanel.h"

namespace widescreen {

// Analog clock the same size as the round UI, mirroring it on the right half.
class ClockPanel : public SidePanel {
  public:
    const char *id() const override { return "clock"; }
    void create(lv_obj_t *parent, const PanelTheme &theme) override;
    void destroy() override;
    void update(const Telemetry &telemetry) override;

  private:
    lv_obj_t *meter = nullptr;
    lv_meter_indicator_t *hourHand = nullptr;
    lv_meter_indicator_t *minuteHand = nullptr;
    lv_meter_indicator_t *secondHand = nullptr;
    lv_obj_t *timeLabel = nullptr;
    lv_obj_t *dateLabel = nullptr;
    long lastSecond = -1;
};

} // namespace widescreen
