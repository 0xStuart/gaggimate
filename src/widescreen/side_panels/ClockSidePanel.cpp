#include "ClockSidePanel.h"

#include <ctime>
#include <widescreen/Layout.h>

namespace widescreen {

// Before NTP sync the RTC starts near 1970; don't show that as a real time.
static constexpr int MIN_VALID_YEAR = 2024;

void ClockSidePanel::create(lv_obj_t *parent, const SidePanelTheme &theme) {
    meter = lv_meter_create(parent);
    lv_obj_set_size(meter, ISLAND_SIZE, ISLAND_SIZE);
    lv_obj_set_pos(meter, ISLAND_X, ISLAND_Y);
    lv_obj_clear_flag(meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(meter, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(meter, theme.background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(meter, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(meter, 20, LV_PART_MAIN);
    lv_obj_set_style_text_color(meter, theme.text, LV_PART_TICKS);
    lv_obj_set_style_text_font(meter, &lv_font_montserrat_34, LV_PART_TICKS);
    lv_obj_set_style_bg_color(meter, theme.text, LV_PART_INDICATOR);
    lv_obj_set_style_width(meter, 18, LV_PART_INDICATOR);
    lv_obj_set_style_height(meter, 18, LV_PART_INDICATOR);

    lv_meter_scale_t *minuteScale = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, minuteScale, 61, 2, 12, theme.muted);
    lv_meter_set_scale_range(meter, minuteScale, 0, 60, 360, 270);

    lv_meter_scale_t *hourScale = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, hourScale, 12, 0, 0, theme.muted);
    lv_meter_set_scale_major_ticks(meter, hourScale, 1, 5, 26, theme.text, 22);
    lv_meter_set_scale_range(meter, hourScale, 1, 12, 330, 300);

    hourHand = lv_meter_add_needle_line(meter, minuteScale, 10, theme.text, -120);
    minuteHand = lv_meter_add_needle_line(meter, minuteScale, 6, theme.text, -50);
    secondHand = lv_meter_add_needle_line(meter, minuteScale, 3, theme.accent, -30);

    timeLabel = lv_label_create(meter);
    lv_obj_set_style_text_color(timeLabel, theme.text, 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, 90);

    dateLabel = lv_label_create(meter);
    lv_obj_set_style_text_color(dateLabel, theme.muted, 0);
    lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 122);

    lastSecond = -1;
    update(Telemetry{});
}

void ClockSidePanel::destroy() {
    meter = nullptr;
    hourHand = minuteHand = secondHand = nullptr;
    timeLabel = dateLabel = nullptr;
}

void ClockSidePanel::update(const Telemetry &) {
    if (meter == nullptr)
        return;
    const time_t now = time(nullptr);
    if (now == lastSecond)
        return;
    lastSecond = now;

    tm local{};
    localtime_r(&now, &local);
    const int minuteTicks = local.tm_min;
    lv_meter_set_indicator_value(meter, hourHand, (local.tm_hour % 12) * 5 + minuteTicks / 12);
    lv_meter_set_indicator_value(meter, minuteHand, minuteTicks);
    lv_meter_set_indicator_value(meter, secondHand, local.tm_sec);

    if (local.tm_year + 1900 < MIN_VALID_YEAR) {
        lv_label_set_text(timeLabel, "--:--");
        lv_label_set_text(dateLabel, "");
        return;
    }
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M", &local);
    lv_label_set_text(timeLabel, buf);
    strftime(buf, sizeof(buf), "%a %d %b", &local);
    lv_label_set_text(dateLabel, buf);
}

} // namespace widescreen
