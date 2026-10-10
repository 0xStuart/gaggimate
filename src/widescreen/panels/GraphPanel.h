#pragma once

#include "SidePanel.h"
#include <deque>

namespace widescreen {

// Rolling chart of pressure, puck flow, weight and temperature, like the web UI's
// live chart: a 60 s window when idle, 30 s growing to 60 s during a shot.
class GraphPanel : public SidePanel {
  public:
    static constexpr int SERIES_COUNT = 6;
    static constexpr uint16_t POINT_COUNT = 200;

    const char *id() const override { return "graph"; }
    const char *name() const override { return "Graph"; }
    void create(lv_obj_t *parent, const PanelTheme &theme) override;
    void destroy() override;
    void update(const Telemetry &telemetry) override;
    void observe(const Telemetry &telemetry) override;

  private:
    struct Sample {
        uint32_t ms;
        float values[SERIES_COUNT];
    };

    void render(const Telemetry &telemetry);
    static void drawPartCb(lv_event_t *event);

    std::deque<Sample> samples; // recorded even while hidden so switching panels keeps the history
    uint32_t lastSampleMs = 0;
    bool sampled = false;
    Telemetry latest;

    lv_obj_t *chart = nullptr;
    lv_obj_t *title = nullptr;
    lv_obj_t *legend[4] = {};
    lv_chart_series_t *series[SERIES_COUNT] = {};
    lv_coord_t points[SERIES_COUNT][POINT_COUNT] = {};
    lv_color_t titleColor{};
    lv_color_t shotColor{};
    float tempLow = 0.0f;
    float tempHigh = 0.0f;
    float pumpRange = 0.0f;
};

} // namespace widescreen
