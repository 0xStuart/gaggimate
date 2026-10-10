#include "GraphPanel.h"

#include <cmath>
#include <cstdio>
#include <widescreen/Layout.h>

namespace widescreen {

// Series order and colours follow the round UI's shot chart; every series is
// normalised to 0..CHART_SCALE of its own range, as there are more units than axes.
enum Series { PRESSURE, TARGET_PRESSURE, FLOW, TARGET_FLOW, TEMPERATURE, WEIGHT };

static constexpr lv_coord_t CHART_SCALE = 1000;
static constexpr uint32_t SAMPLE_INTERVAL_MS = 250;
static constexpr uint32_t IDLE_WINDOW_MS = 60000;
static constexpr uint32_t HISTORY_MS = IDLE_WINDOW_MS + 5000;
static constexpr float PUMP_RANGE_DEFAULT = 12.0f; // bar / ml/s
static constexpr float WEIGHT_RANGE_DEFAULT = 50.0f;
static constexpr float TEMP_HALF_RANGE = 10.0f; // around the target, widened to fit the readings
static constexpr float TEMP_STEP = 5.0f;
static constexpr uint8_t Y_DIVISIONS = 5;

static constexpr lv_coord_t MARGIN_X = 24;
static constexpr lv_coord_t LEGEND_Y = 54;
static constexpr lv_coord_t CHART_X = 52;
static constexpr lv_coord_t CHART_Y = 96;
static constexpr lv_coord_t CHART_W = 408;
static constexpr lv_coord_t CHART_H = 430;
static constexpr lv_coord_t AXIS_LABEL_SIZE = 40;

static uint32_t windowFor(const Telemetry &t) {
    if (!t.brewing)
        return IDLE_WINDOW_MS;
    if (t.brewMs < 30000)
        return 30000;
    return t.brewMs < 45000 ? 45000 : 60000;
}

static lv_coord_t normalise(float value, float low, float high) {
    const float ratio = (value - low) / (high - low);
    return static_cast<lv_coord_t>(lroundf(LV_CLAMP(0.0f, ratio, 1.0f) * CHART_SCALE));
}

void GraphPanel::create(lv_obj_t *parent, const PanelTheme &theme) {
    title = lv_label_create(parent);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, MARGIN_X, 18);

    const lv_color_t legendColors[4] = {theme.blue, theme.accent, theme.amber, theme.red};
    const lv_coord_t legendWidth = (PANEL_W - 2 * MARGIN_X) / 4;
    for (int i = 0; i < 4; i++) {
        legend[i] = lv_label_create(parent);
        lv_obj_set_style_text_font(legend[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(legend[i], legendColors[i], 0);
        lv_obj_set_pos(legend[i], MARGIN_X + i * legendWidth, LEGEND_Y);
    }

    chart = lv_chart_create(parent);
    lv_obj_set_pos(chart, CHART_X, CHART_Y);
    lv_obj_set_size(chart, CHART_W, CHART_H);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(chart, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, theme.muted, LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(chart, theme.muted, LV_PART_TICKS);
    lv_obj_set_style_text_font(chart, &lv_font_montserrat_14, LV_PART_TICKS);
    lv_obj_set_style_line_color(chart, theme.muted, LV_PART_TICKS);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart, POINT_COUNT);
    lv_chart_set_div_line_count(chart, Y_DIVISIONS, 0);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_SCALE);
    lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, CHART_SCALE);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 6, 0, Y_DIVISIONS, 1, true, AXIS_LABEL_SIZE);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_SECONDARY_Y, 6, 0, Y_DIVISIONS, 1, true, AXIS_LABEL_SIZE);
    lv_obj_add_event_cb(chart, drawPartCb, LV_EVENT_DRAW_PART_BEGIN, this);

    const lv_color_t colors[SERIES_COUNT] = {theme.blue, theme.blue, theme.accent, theme.accent, theme.red, theme.amber};
    for (int i = 0; i < SERIES_COUNT; i++) {
        series[i] = lv_chart_add_series(chart, colors[i], LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_ext_y_array(chart, series[i], points[i]);
    }

    titleColor = theme.text;
    shotColor = theme.accent;
    render(latest);
}

void GraphPanel::destroy() {
    chart = nullptr;
    title = nullptr;
    for (lv_obj_t *&label : legend)
        label = nullptr;
    for (lv_chart_series_t *&s : series)
        s = nullptr;
}

void GraphPanel::observe(const Telemetry &t) {
    latest = t;
    const uint32_t now = lv_tick_get();
    if (now - lastSampleMs < SAMPLE_INTERVAL_MS)
        return;
    lastSampleMs = now;
    sampled = true;

    Sample sample{now, {}};
    sample.values[PRESSURE] = t.pressure;
    sample.values[TARGET_PRESSURE] = t.targetPressure;
    sample.values[FLOW] = t.flow;
    sample.values[TARGET_FLOW] = t.targetFlow;
    sample.values[TEMPERATURE] = t.temp;
    sample.values[WEIGHT] = t.weight;
    samples.push_back(sample);
    while (!samples.empty() && now - samples.front().ms > HISTORY_MS)
        samples.pop_front();
}

void GraphPanel::update(const Telemetry &t) {
    if (chart == nullptr || !sampled)
        return;
    sampled = false;
    render(t);
}

void GraphPanel::render(const Telemetry &t) {
    char buf[32];
    if (t.brewing) {
        snprintf(buf, sizeof(buf), "Shot  %lu:%02lu", static_cast<unsigned long>(t.brewMs / 60000),
                 static_cast<unsigned long>((t.brewMs / 1000) % 60));
    } else {
        snprintf(buf, sizeof(buf), "Live");
    }
    lv_label_set_text(title, buf);
    lv_obj_set_style_text_color(title, t.brewing ? shotColor : titleColor, 0);

    snprintf(buf, sizeof(buf), "%.1f bar", t.pressure);
    lv_label_set_text(legend[0], buf);
    snprintf(buf, sizeof(buf), "%.1f ml/s", t.flow);
    lv_label_set_text(legend[1], buf);
    snprintf(buf, sizeof(buf), "%.1f g", t.weight);
    lv_label_set_text(legend[2], buf);
    snprintf(buf, sizeof(buf),
             "%.1f \xC2\xB0"
             "C",
             t.temp);
    lv_label_set_text(legend[3], buf);

    const uint32_t now = lv_tick_get();
    const uint32_t window = windowFor(t);
    const uint32_t start = now - window;

    const float tempCentre = t.targetTemp > 0.0f ? t.targetTemp : t.temp;
    float pumpMax = PUMP_RANGE_DEFAULT;
    float weightMax = WEIGHT_RANGE_DEFAULT;
    float tempMin = tempCentre - TEMP_HALF_RANGE;
    float tempMax = tempCentre + TEMP_HALF_RANGE;
    for (const Sample &s : samples) {
        if (now - s.ms > window)
            continue;
        for (int i : {PRESSURE, TARGET_PRESSURE, FLOW, TARGET_FLOW})
            pumpMax = LV_MAX(pumpMax, ceilf(s.values[i]));
        weightMax = LV_MAX(weightMax, ceilf((s.values[WEIGHT] + 5.0f) / 10.0f) * 10.0f);
        tempMin = LV_MIN(tempMin, s.values[TEMPERATURE]);
        tempMax = LV_MAX(tempMax, s.values[TEMPERATURE]);
    }
    pumpRange = pumpMax;
    tempLow = floorf(tempMin / TEMP_STEP) * TEMP_STEP;
    tempHigh = ceilf(tempMax / TEMP_STEP) * TEMP_STEP;

    size_t cursor = 0;
    for (uint16_t p = 0; p < POINT_COUNT; p++) {
        const uint32_t at = start + static_cast<uint32_t>(static_cast<uint64_t>(window) * p / (POINT_COUNT - 1));
        while (cursor + 1 < samples.size() && static_cast<int32_t>(samples[cursor + 1].ms - at) <= 0)
            cursor++;
        const bool missing = samples.empty() || static_cast<int32_t>(samples[cursor].ms - at) > 0 ||
                             static_cast<int32_t>(at - samples[cursor].ms) > static_cast<int32_t>(2 * SAMPLE_INTERVAL_MS);
        for (int i = 0; i < SERIES_COUNT; i++) {
            if (missing) {
                points[i][p] = LV_CHART_POINT_NONE;
                continue;
            }
            const float v = samples[cursor].values[i];
            const bool isTarget = i == TARGET_PRESSURE || i == TARGET_FLOW;
            if (isTarget && v <= 0.0f) {
                points[i][p] = LV_CHART_POINT_NONE;
            } else if (i == TEMPERATURE) {
                points[i][p] = normalise(v, tempLow, tempHigh);
            } else if (i == WEIGHT) {
                points[i][p] = normalise(v, 0.0f, weightMax);
            } else {
                points[i][p] = normalise(v, 0.0f, pumpRange);
            }
        }
    }
    lv_chart_refresh(chart);
}

void GraphPanel::drawPartCb(lv_event_t *event) {
    auto *panel = static_cast<GraphPanel *>(lv_event_get_user_data(event));
    auto *dsc = static_cast<lv_obj_draw_part_dsc_t *>(lv_event_get_param(event));

    if (dsc->part == LV_PART_TICKS && dsc->text != nullptr) {
        const float ratio = static_cast<float>(dsc->value) / CHART_SCALE;
        if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
            const float temp = panel->tempLow + ratio * (panel->tempHigh - panel->tempLow);
            lv_snprintf(dsc->text, dsc->text_length, "%d\xC2\xB0", static_cast<int>(lroundf(temp)));
        } else if (dsc->id == LV_CHART_AXIS_SECONDARY_Y) {
            lv_snprintf(dsc->text, dsc->text_length, "%d", static_cast<int>(lroundf(ratio * panel->pumpRange)));
        }
        return;
    }

    // lv_chart reuses one line descriptor for every series, so set all of it each time.
    if (dsc->part == LV_PART_ITEMS && dsc->line_dsc != nullptr) {
        const bool target =
            dsc->sub_part_ptr == panel->series[TARGET_PRESSURE] || dsc->sub_part_ptr == panel->series[TARGET_FLOW];
        dsc->line_dsc->width = target ? 2 : 3;
        dsc->line_dsc->dash_width = target ? 6 : 0;
        dsc->line_dsc->dash_gap = target ? 6 : 0;
        dsc->line_dsc->opa = target ? LV_OPA_60 : LV_OPA_COVER;
    }
}

} // namespace widescreen
