#pragma once

#include "SidePanel.h"
#include <display/models/shot_log_format.h>

namespace waveshare_esp32_p4_7b {

// The latest saved shot from the shot history: time, yield, ratio and peak pressure
// above a chart of the whole shot. Arrow buttons page back through older shots.
class LastShotSidePanel : public SidePanel {
  public:
    static constexpr int SERIES_COUNT = 6;
    static constexpr uint16_t POINT_COUNT = 200;
    static constexpr size_t RECENT_COUNT = 10;

    const char *id() const override { return "lastshot"; }
    const char *name() const override { return "Last shot"; }
    void create(lv_obj_t *parent, const SidePanelTheme &theme) override;
    void destroy() override;
    void update(const Telemetry &telemetry) override;
    void observe(const Telemetry &telemetry) override;

  private:
    void reload();
    bool loadShot();
    void render();
    void select(size_t index);
    static void navCb(lv_event_t *event);
    static void drawPartCb(lv_event_t *event);

    ShotIndexEntry entries[RECENT_COUNT] = {};
    size_t entryCount = 0;
    size_t selected = 0;
    bool loaded = false;

    bool wasBrewing = false;
    uint32_t brewEndMs = 0;
    bool awaitingSave = false; // a shot just ended and its index entry is not written yet
    uint32_t lastPollMs = 0;

    float values[SERIES_COUNT][POINT_COUNT] = {};
    uint16_t pointCount = 0;
    float durationS = 0.0f;
    float yieldG = 0.0f;
    float doseG = 0.0f;
    float peakBar = 0.0f;
    uint32_t startEpoch = 0;
    float pumpRange = 0.0f;
    float weightRange = 0.0f;
    float tempLow = 0.0f;
    float tempHigh = 0.0f;

    lv_obj_t *title = nullptr;
    lv_obj_t *subtitle = nullptr;
    lv_obj_t *pageLabel = nullptr;
    lv_obj_t *olderButton = nullptr;
    lv_obj_t *newerButton = nullptr;
    lv_obj_t *body = nullptr;
    lv_obj_t *emptyLabel = nullptr;
    lv_obj_t *tileValues[4] = {};
    lv_obj_t *chart = nullptr;
    lv_chart_series_t *series[SERIES_COUNT] = {};
    lv_coord_t points[SERIES_COUNT][POINT_COUNT] = {};
};

} // namespace waveshare_esp32_p4_7b
