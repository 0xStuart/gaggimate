#pragma once

#include "SidePanel.h"

namespace waveshare_esp32_p4_7b {

// Car-dashboard style dials for pressure, puck flow, weight and temperature.
class GaugesSidePanel : public SidePanel {
  public:
    const char *id() const override { return "gauges"; }
    const char *name() const override { return "Gauges"; }
    void create(lv_obj_t *parent, const SidePanelTheme &theme) override;
    void destroy() override;
    void update(const Telemetry &telemetry) override;

    struct Spec {
        const char *title;
        const char *unit;
        float min;
        float max;
        float majorStep;
        uint16_t minorPerMajor;
        int decimals;
    };

  private:
    struct Gauge {
        const Spec *spec = nullptr;
        lv_obj_t *meter = nullptr;
        lv_meter_indicator_t *valueArc = nullptr;
        lv_meter_indicator_t *targetMark = nullptr;
        lv_meter_indicator_t *needle = nullptr;
        lv_obj_t *valueLabel = nullptr;
        float shown = 0.0f;
        float shownTarget = -1.0f;
        int lastLabelValue = -1;
    };

    static constexpr int GAUGE_COUNT = 4;

    void createGauge(Gauge &gauge, const Spec &spec, lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color,
                     const SidePanelTheme &theme);
    static void setGauge(Gauge &gauge, float value, float target);

    Gauge gauges[GAUGE_COUNT];
};

} // namespace waveshare_esp32_p4_7b
