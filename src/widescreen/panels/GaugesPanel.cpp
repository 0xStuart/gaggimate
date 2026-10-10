#include "GaugesPanel.h"

#include <cmath>
#include <cstdio>
#include <widescreen/Layout.h>

namespace widescreen {

// lv_meter only takes integers, so every scale runs in tenths of its unit.
static constexpr int VALUE_SCALE = 10;
static constexpr lv_coord_t GAUGE_SIZE = 236;
static constexpr lv_coord_t GAUGE_GAP_X = 16;
static constexpr lv_coord_t GAUGE_TOP = 24;
static constexpr lv_coord_t GAUGE_GAP_Y = 24;
static constexpr uint16_t SWEEP_DEGREES = 270;
static constexpr uint16_t SWEEP_ROTATION = 135;
static constexpr uint16_t ARC_WIDTH = 10;
static constexpr uint16_t TARGET_WIDTH = 20;
static constexpr float TARGET_SPAN = 0.015f; // fraction of the range covered by the target mark
static constexpr float NEEDLE_SMOOTHING = 0.35f;

static const GaugesPanel::Spec PRESSURE{"PRESSURE", "bar", 0.0f, 12.0f, 2.0f, 4, 1};
static const GaugesPanel::Spec FLOW{"FLOW", "ml/s", 0.0f, 8.0f, 2.0f, 4, 1};
static const GaugesPanel::Spec WEIGHT{"WEIGHT", "g", 0.0f, 60.0f, 10.0f, 5, 1};
static const GaugesPanel::Spec TEMPERATURE{"TEMP",
                                           "\xC2\xB0"
                                           "C",
                                           20.0f,
                                           160.0f,
                                           20.0f,
                                           4,
                                           1};

static int toScale(const GaugesPanel::Spec &spec, float value) {
    const float clamped = value < spec.min ? spec.min : (value > spec.max ? spec.max : value);
    return static_cast<int>(lroundf(clamped * VALUE_SCALE));
}

static void tickLabelCb(lv_event_t *event) {
    auto *dsc = static_cast<lv_obj_draw_part_dsc_t *>(lv_event_get_param(event));
    if (dsc->class_p != &lv_meter_class || dsc->type != LV_METER_DRAW_PART_TICK || dsc->text == nullptr)
        return;
    static char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d", static_cast<int>(dsc->value / VALUE_SCALE));
    dsc->text = buf;
}

void GaugesPanel::create(lv_obj_t *parent, const PanelTheme &theme) {
    const lv_coord_t left = (PANEL_W - 2 * GAUGE_SIZE - GAUGE_GAP_X) / 2;
    const lv_coord_t right = left + GAUGE_SIZE + GAUGE_GAP_X;
    const lv_coord_t bottom = GAUGE_TOP + GAUGE_SIZE + GAUGE_GAP_Y;
    createGauge(gauges[0], PRESSURE, parent, left, GAUGE_TOP, theme.blue, theme);
    createGauge(gauges[1], FLOW, parent, right, GAUGE_TOP, theme.accent, theme);
    createGauge(gauges[2], WEIGHT, parent, left, bottom, theme.amber, theme);
    createGauge(gauges[3], TEMPERATURE, parent, right, bottom, theme.red, theme);
}

void GaugesPanel::createGauge(Gauge &gauge, const Spec &spec, lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color,
                              const PanelTheme &theme) {
    gauge = Gauge{};
    gauge.spec = &spec;
    gauge.shown = spec.min;

    lv_obj_t *meter = lv_meter_create(parent);
    gauge.meter = meter;
    lv_obj_set_size(meter, GAUGE_SIZE, GAUGE_SIZE);
    lv_obj_set_pos(meter, x, y);
    lv_obj_clear_flag(meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(meter, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(meter, theme.background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(meter, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(meter, 14, LV_PART_MAIN);
    lv_obj_set_style_text_color(meter, theme.muted, LV_PART_TICKS);
    lv_obj_set_style_text_font(meter, &lv_font_montserrat_14, LV_PART_TICKS);
    lv_obj_set_style_bg_color(meter, theme.text, LV_PART_INDICATOR);
    lv_obj_set_style_width(meter, 14, LV_PART_INDICATOR);
    lv_obj_set_style_height(meter, 14, LV_PART_INDICATOR);
    lv_obj_add_event_cb(meter, tickLabelCb, LV_EVENT_DRAW_PART_BEGIN, nullptr);

    const int minScaled = toScale(spec, spec.min);
    const int maxScaled = toScale(spec, spec.max);
    const uint16_t majors = static_cast<uint16_t>(lroundf((spec.max - spec.min) / spec.majorStep));
    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, scale, majors * spec.minorPerMajor + 1, 2, 8, theme.muted);
    lv_meter_set_scale_major_ticks(meter, scale, spec.minorPerMajor, 3, 14, theme.text, 12);
    lv_meter_set_scale_range(meter, scale, minScaled, maxScaled, SWEEP_DEGREES, SWEEP_ROTATION);

    lv_meter_indicator_t *track = lv_meter_add_arc(meter, scale, ARC_WIDTH, lv_color_mix(color, theme.background, LV_OPA_20), 0);
    lv_meter_set_indicator_start_value(meter, track, minScaled);
    lv_meter_set_indicator_end_value(meter, track, maxScaled);

    gauge.valueArc = lv_meter_add_arc(meter, scale, ARC_WIDTH, color, 0);
    lv_meter_set_indicator_start_value(meter, gauge.valueArc, minScaled);
    lv_meter_set_indicator_end_value(meter, gauge.valueArc, minScaled);

    gauge.targetMark = lv_meter_add_arc(meter, scale, TARGET_WIDTH, theme.text, 5);
    lv_meter_set_indicator_start_value(meter, gauge.targetMark, minScaled);
    lv_meter_set_indicator_end_value(meter, gauge.targetMark, minScaled);

    gauge.needle = lv_meter_add_needle_line(meter, scale, 4, theme.text, -24);
    lv_meter_set_indicator_value(meter, gauge.needle, minScaled);

    lv_obj_t *title = lv_label_create(meter);
    lv_label_set_text(title, spec.title);
    lv_obj_set_style_text_color(title, theme.muted, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -44);

    gauge.valueLabel = lv_label_create(meter);
    lv_obj_set_style_text_color(gauge.valueLabel, theme.text, 0);
    lv_obj_set_style_text_font(gauge.valueLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(gauge.valueLabel, LV_ALIGN_CENTER, 0, 46);

    lv_obj_t *unit = lv_label_create(meter);
    lv_label_set_text(unit, spec.unit);
    lv_obj_set_style_text_color(unit, color, 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, 0);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 74);
}

void GaugesPanel::destroy() {
    for (Gauge &gauge : gauges)
        gauge = Gauge{};
}

void GaugesPanel::update(const Telemetry &t) {
    if (gauges[0].meter == nullptr)
        return;
    setGauge(gauges[0], t.pressure, t.targetPressure);
    setGauge(gauges[1], t.flow, t.targetFlow);
    setGauge(gauges[2], t.weight, 0.0f);
    setGauge(gauges[3], t.temp, t.targetTemp);
}

void GaugesPanel::setGauge(Gauge &gauge, float value, float target) {
    const Spec &spec = *gauge.spec;
    gauge.shown += (value - gauge.shown) * NEEDLE_SMOOTHING;
    const int scaled = toScale(spec, gauge.shown);
    lv_meter_set_indicator_value(gauge.meter, gauge.needle, scaled);
    lv_meter_set_indicator_end_value(gauge.meter, gauge.valueArc, scaled);

    if (target != gauge.shownTarget) {
        gauge.shownTarget = target;
        const float half = (spec.max - spec.min) * TARGET_SPAN;
        const bool visible = target > spec.min && target <= spec.max;
        lv_meter_set_indicator_start_value(gauge.meter, gauge.targetMark, visible ? toScale(spec, target - half) : 0);
        lv_meter_set_indicator_end_value(gauge.meter, gauge.targetMark, visible ? toScale(spec, target + half) : 0);
    }

    const float factor = spec.decimals > 0 ? powf(10.0f, spec.decimals) : 1.0f;
    const int labelValue = static_cast<int>(lroundf(value * factor));
    if (labelValue != gauge.lastLabelValue) {
        gauge.lastLabelValue = labelValue;
        char buf[16];
        snprintf(buf, sizeof(buf), "%.*f", spec.decimals, value);
        lv_label_set_text(gauge.valueLabel, buf);
    }
}

} // namespace widescreen
