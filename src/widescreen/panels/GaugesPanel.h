#pragma once

#include "SidePanel.h"

namespace widescreen {

// Car-dashboard style dials for pressure, puck flow, weight and temperature.
class GaugesPanel : public SidePanel {
  public:
    const char *id() const override { return "gauges"; }
    void create(lv_obj_t *parent, const PanelTheme &theme) override;
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
                     const PanelTheme &theme);
    static void setGauge(Gauge &gauge, float value, float target);

    Gauge gauges[GAUGE_COUNT];
};

} // namespace widescreen
