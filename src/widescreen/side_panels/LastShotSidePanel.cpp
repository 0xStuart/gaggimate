#include "LastShotSidePanel.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <display/main.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <widescreen/Layout.h>

namespace widescreen {

// Same series, colours and per-series normalisation as GraphSidePanel.
enum Series { PRESSURE, TARGET_PRESSURE, FLOW, TARGET_FLOW, TEMPERATURE, WEIGHT };

static constexpr lv_coord_t CHART_SCALE = 1000;
static constexpr float PUMP_RANGE_DEFAULT = 12.0f;
static constexpr float WEIGHT_RANGE_DEFAULT = 50.0f;
static constexpr float TEMP_HALF_RANGE = 10.0f;
static constexpr float TEMP_STEP = 5.0f;
static constexpr uint8_t Y_DIVISIONS = 5;
static constexpr uint8_t X_TICKS = 5;

static constexpr uint32_t POLL_INTERVAL_MS = 1000;
static constexpr uint32_t SAVE_WAIT_MS = 30000;         // covers the history plugin's extended recording after the shot
static constexpr uint32_t MIN_VALID_EPOCH = 1704067200; // 2024-01-01; older means the clock was never synced
static constexpr uint16_t LEGACY_HEADER_SIZE = 128;
static constexpr size_t SAMPLES_PER_READ = 32;

static constexpr lv_coord_t MARGIN_X = 24;
static constexpr lv_coord_t NAV_BUTTON_W = 44;
static constexpr lv_coord_t NAV_BUTTON_H = 36;
static constexpr lv_coord_t PAGE_LABEL_W = 64;
static constexpr lv_coord_t TILE_Y = 84;
static constexpr lv_coord_t TILE_H = 68;
static constexpr lv_coord_t TILE_GAP = 10;
static constexpr lv_coord_t CHART_X = 52;
static constexpr lv_coord_t CHART_Y = 172;
static constexpr lv_coord_t CHART_W = 408;
static constexpr lv_coord_t CHART_H = 340;
static constexpr lv_coord_t AXIS_LABEL_SIZE = 40;
static constexpr lv_coord_t X_LABEL_SIZE = 24;

// Record fields after the timestamp, common to every shot log version.
#pragma pack(push, 1)
struct SampleFields {
    uint16_t tt, ct, tp, cp;
    int16_t fl, tf, pf, vf;
    uint16_t v, ev;
};
#pragma pack(pop)

static fs::FS &historyFs() {
    if (controller.isSDCard())
        return SD_MMC;
    return LittleFS;
}

static lv_coord_t normalise(float value, float low, float high) {
    const float ratio = (value - low) / (high - low);
    return static_cast<lv_coord_t>(lroundf(LV_CLAMP(0.0f, ratio, 1.0f) * CHART_SCALE));
}

static float readDose(uint32_t id) {
    char path[32];
    snprintf(path, sizeof(path), "/h/%06lu.json", static_cast<unsigned long>(id));
    fs::FS &fs = historyFs();
    File file = fs.open(path, "r");
    if (!file) {
        snprintf(path, sizeof(path), "/h/%lu.json", static_cast<unsigned long>(id));
        file = fs.open(path, "r");
    }
    if (!file)
        return 0.0f;
    const String text = file.readString();
    file.close();
    JsonDocument notes;
    if (deserializeJson(notes, text))
        return 0.0f;
    JsonVariantConst dose = notes["doseIn"];
    return dose.is<const char *>() ? static_cast<float>(atof(dose.as<const char *>())) : dose.as<float>();
}

static void formatWhen(uint32_t epoch, char *buf, size_t size) {
    buf[0] = '\0';
    if (epoch < MIN_VALID_EPOCH)
        return;
    const time_t shotTime = epoch;
    const time_t now = time(nullptr);
    const time_t yesterdayTime = now - 24 * 60 * 60;
    tm shot{}, today{}, yesterday{};
    localtime_r(&shotTime, &shot);
    localtime_r(&now, &today);
    localtime_r(&yesterdayTime, &yesterday);
    char clock[8];
    strftime(clock, sizeof(clock), "%H:%M", &shot);
    if (shot.tm_year == today.tm_year && shot.tm_yday == today.tm_yday) {
        snprintf(buf, size, "Today %s", clock);
    } else if (shot.tm_year == yesterday.tm_year && shot.tm_yday == yesterday.tm_yday) {
        snprintf(buf, size, "Yesterday %s", clock);
    } else {
        strftime(buf, size, "%a %d %b %H:%M", &shot);
    }
}

static lv_obj_t *makeNavButton(lv_obj_t *parent, const char *symbol, lv_coord_t x, const SidePanelTheme &theme) {
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, 14);
    lv_obj_set_size(button, NAV_BUTTON_W, NAV_BUTTON_H);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, theme.muted, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_30, 0);
    lv_obj_set_style_opa(button, LV_OPA_30, LV_STATE_DISABLED);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, theme.text, 0);
    lv_obj_center(label);
    return button;
}

void LastShotSidePanel::create(lv_obj_t *parent, const SidePanelTheme &theme) {
    reload();
    selected = 0;
    loaded = loadShot();

    title = lv_label_create(parent);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, theme.text, 0);
    lv_obj_set_pos(title, MARGIN_X, 18);

    const lv_coord_t newerX = SIDE_PANEL_W - MARGIN_X - NAV_BUTTON_W;
    const lv_coord_t olderX = newerX - PAGE_LABEL_W - NAV_BUTTON_W;
    olderButton = makeNavButton(parent, LV_SYMBOL_LEFT, olderX, theme);
    newerButton = makeNavButton(parent, LV_SYMBOL_RIGHT, newerX, theme);
    lv_obj_add_event_cb(olderButton, navCb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(newerButton, navCb, LV_EVENT_CLICKED, this);

    pageLabel = lv_label_create(parent);
    lv_obj_set_width(pageLabel, PAGE_LABEL_W);
    lv_obj_set_style_text_align(pageLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(pageLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pageLabel, theme.muted, 0);
    lv_obj_set_pos(pageLabel, olderX + NAV_BUTTON_W, 22);

    subtitle = lv_label_create(parent);
    lv_obj_set_width(subtitle, SIDE_PANEL_W - 2 * MARGIN_X);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, theme.muted, 0);
    lv_obj_set_pos(subtitle, MARGIN_X, 54);

    emptyLabel = lv_label_create(parent);
    lv_obj_set_style_text_font(emptyLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(emptyLabel, theme.muted, 0);
    lv_obj_align(emptyLabel, LV_ALIGN_CENTER, 0, 0);

    body = lv_obj_create(parent);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, SIDE_PANEL_W, SIDE_PANEL_H);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    static const char *const captions[4] = {"TIME", "YIELD", "RATIO", "PEAK"};
    const lv_coord_t tileW = (SIDE_PANEL_W - 2 * MARGIN_X - 3 * TILE_GAP) / 4;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *tile = lv_obj_create(body);
        lv_obj_remove_style_all(tile);
        lv_obj_set_pos(tile, MARGIN_X + i * (tileW + TILE_GAP), TILE_Y);
        lv_obj_set_size(tile, tileW, TILE_H);
        lv_obj_set_style_radius(tile, 12, 0);
        lv_obj_set_style_bg_color(tile, theme.muted, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_20, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        tileValues[i] = lv_label_create(tile);
        lv_obj_set_style_text_font(tileValues[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(tileValues[i], theme.text, 0);
        lv_obj_align(tileValues[i], LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t *caption = lv_label_create(tile);
        lv_label_set_text(caption, captions[i]);
        lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(caption, theme.muted, 0);
        lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    chart = lv_chart_create(body);
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
    lv_chart_set_point_count(chart, POINT_COUNT);
    lv_chart_set_div_line_count(chart, Y_DIVISIONS, 0);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_SCALE);
    lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, CHART_SCALE);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 6, 0, Y_DIVISIONS, 1, true, AXIS_LABEL_SIZE);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_SECONDARY_Y, 6, 0, Y_DIVISIONS, 1, true, AXIS_LABEL_SIZE);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 6, 0, X_TICKS, 1, true, X_LABEL_SIZE);
    lv_obj_add_event_cb(chart, drawPartCb, LV_EVENT_DRAW_PART_BEGIN, this);

    const lv_color_t colors[SERIES_COUNT] = {theme.blue, theme.blue, theme.accent, theme.accent, theme.red, theme.amber};
    for (int i = 0; i < SERIES_COUNT; i++) {
        series[i] = lv_chart_add_series(chart, colors[i], LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_ext_y_array(chart, series[i], points[i]);
    }

    render();
}

void LastShotSidePanel::destroy() {
    title = subtitle = pageLabel = nullptr;
    olderButton = newerButton = nullptr;
    body = emptyLabel = chart = nullptr;
    for (lv_obj_t *&label : tileValues)
        label = nullptr;
    for (lv_chart_series_t *&s : series)
        s = nullptr;
}

void LastShotSidePanel::observe(const Telemetry &t) {
    if (wasBrewing && !t.brewing) {
        awaitingSave = true;
        brewEndMs = lv_tick_get();
    }
    wasBrewing = t.brewing;
}

void LastShotSidePanel::update(const Telemetry &) {
    if (!awaitingSave || lv_tick_elaps(lastPollMs) < POLL_INTERVAL_MS)
        return;
    lastPollMs = lv_tick_get();
    if (lv_tick_elaps(brewEndMs) > SAVE_WAIT_MS) {
        awaitingSave = false; // too short to keep (flush), or saving failed
        return;
    }
    ShotIndexEntry newest{};
    if (ShotHistory.readRecentEntries(&newest, 1) != 1 || !(newest.flags & SHOT_FLAG_COMPLETED))
        return;
    if (entryCount > 0 && newest.id == entries[0].id)
        return;
    awaitingSave = false;
    reload();
    select(0);
}

void LastShotSidePanel::reload() {
    // Static: the LVGL task's stack is small and this is only touched from that task.
    static ShotIndexEntry found[RECENT_COUNT + 4];
    const size_t count = ShotHistory.readRecentEntries(found, RECENT_COUNT + 4);
    entryCount = 0;
    for (size_t i = 0; i < count && entryCount < RECENT_COUNT; i++) {
        if (found[i].flags & SHOT_FLAG_COMPLETED)
            entries[entryCount++] = found[i];
    }
}

void LastShotSidePanel::select(size_t index) {
    selected = index;
    loaded = loadShot();
    render();
}

bool LastShotSidePanel::loadShot() {
    pointCount = 0;
    durationS = yieldG = doseG = peakBar = 0.0f;
    startEpoch = 0;
    if (selected >= entryCount)
        return false;
    const ShotIndexEntry &entry = entries[selected];

    char path[32];
    snprintf(path, sizeof(path), "/h/%06lu.slog", static_cast<unsigned long>(entry.id));
    File file = historyFs().open(path, "r");
    if (!file)
        return false;

    ShotLogHeader header{};
    const size_t headerRead = file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header));
    if (header.magic != SHOT_LOG_MAGIC || header.headerSize < LEGACY_HEADER_SIZE || header.headerSize > headerRead) {
        file.close();
        return false;
    }
    if (header.headerSize < sizeof(header))
        memset(reinterpret_cast<uint8_t *>(&header) + header.headerSize, 0, sizeof(header) - header.headerSize);

    const uint8_t sampleSize = shotLogSampleSize(header);
    const bool elapsedTimestamp = shotLogHasElapsedTimestamp(header);
    const size_t timestampSize = elapsedTimestamp ? 4 : 2;
    const uint32_t count = sampleSize > 0 ? (file.size() - header.headerSize) / sampleSize : 0;
    if (count < 2 || !file.seek(header.headerSize)) {
        file.close();
        return false;
    }
    pointCount = static_cast<uint16_t>(LV_MIN(count, static_cast<uint32_t>(POINT_COUNT)));

    static uint8_t buffer[SHOT_LOG_SAMPLE_SIZE * SAMPLES_PER_READ];
    float pumpMax = 0.0f, weightMax = 0.0f, targetTemp = 0.0f;
    float tempMin = INFINITY, tempMax = -INFINITY;
    uint32_t lastMs = 0;
    uint32_t index = 0;
    while (index < count) {
        const size_t batch = LV_MIN(count - index, static_cast<uint32_t>(SAMPLES_PER_READ));
        if (file.read(buffer, batch * sampleSize) != batch * sampleSize)
            break;
        for (size_t k = 0; k < batch; k++, index++) {
            const uint8_t *record = buffer + k * sampleSize;
            if (elapsedTimestamp) {
                memcpy(&lastMs, record, sizeof(uint32_t));
            } else {
                uint16_t sampleIndex;
                memcpy(&sampleIndex, record, sizeof(uint16_t));
                lastMs = static_cast<uint32_t>(sampleIndex) * header.sampleInterval;
            }
            SampleFields f;
            memcpy(&f, record + timestampSize, sizeof(f));

            const uint16_t p = static_cast<uint16_t>(static_cast<uint64_t>(index) * (pointCount - 1) / (count - 1));
            values[PRESSURE][p] = f.cp / 10.0f;
            values[TARGET_PRESSURE][p] = f.tp / 10.0f;
            values[FLOW][p] = f.pf / 100.0f;
            values[TARGET_FLOW][p] = f.tf / 100.0f;
            values[TEMPERATURE][p] = f.ct / 10.0f;
            values[WEIGHT][p] = (f.v > 0 ? f.v : f.ev) / 10.0f;

            for (int s : {PRESSURE, TARGET_PRESSURE, FLOW, TARGET_FLOW})
                pumpMax = LV_MAX(pumpMax, values[s][p]);
            peakBar = LV_MAX(peakBar, values[PRESSURE][p]);
            weightMax = LV_MAX(weightMax, values[WEIGHT][p]);
            if (f.ct > 0) {
                tempMin = LV_MIN(tempMin, values[TEMPERATURE][p]);
                tempMax = LV_MAX(tempMax, values[TEMPERATURE][p]);
            }
            if (targetTemp == 0.0f && f.tt > 0)
                targetTemp = f.tt / 10.0f;
        }
    }
    file.close();
    if (index < 2)
        return false;
    if (index < count)
        pointCount = static_cast<uint16_t>(static_cast<uint64_t>(index - 1) * (pointCount - 1) / (count - 1) + 1);

    durationS = (header.durationMs > 0 ? header.durationMs : lastMs) / 1000.0f;
    yieldG = (entry.volume > 0 ? entry.volume : header.finalWeight) / 10.0f;
    startEpoch = header.startEpoch > 0 ? header.startEpoch : entry.timestamp;
    doseG = readDose(entry.id);

    pumpRange = LV_MAX(PUMP_RANGE_DEFAULT, ceilf(pumpMax));
    weightRange = LV_MAX(WEIGHT_RANGE_DEFAULT, ceilf((weightMax + 5.0f) / 10.0f) * 10.0f);
    if (tempMin > tempMax) {
        tempMin = tempMax = targetTemp;
    }
    const float tempCentre = targetTemp > 0.0f ? targetTemp : (tempMin + tempMax) / 2.0f;
    tempLow = floorf(LV_MIN(tempCentre - TEMP_HALF_RANGE, tempMin) / TEMP_STEP) * TEMP_STEP;
    tempHigh = ceilf(LV_MAX(tempCentre + TEMP_HALF_RANGE, tempMax) / TEMP_STEP) * TEMP_STEP;
    return true;
}

void LastShotSidePanel::render() {
    if (title == nullptr)
        return;
    char buf[96];

    if (selected == 0) {
        lv_label_set_text(title, "Last shot");
    } else {
        snprintf(buf, sizeof(buf), "Shot #%lu", static_cast<unsigned long>(entries[selected].id));
        lv_label_set_text(title, buf);
    }
    if (entryCount > 0) {
        snprintf(buf, sizeof(buf), "%u / %u", static_cast<unsigned>(selected + 1), static_cast<unsigned>(entryCount));
        lv_label_set_text(pageLabel, buf);
    } else {
        lv_label_set_text(pageLabel, "");
    }
    lv_obj_add_state(olderButton, LV_STATE_DISABLED);
    lv_obj_add_state(newerButton, LV_STATE_DISABLED);
    if (selected + 1 < entryCount)
        lv_obj_clear_state(olderButton, LV_STATE_DISABLED);
    if (selected > 0)
        lv_obj_clear_state(newerButton, LV_STATE_DISABLED);

    if (!loaded) {
        lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(emptyLabel, entryCount == 0 ? "No saved shots yet" : "Could not read this shot");
        lv_label_set_text(subtitle, entryCount > 0 ? entries[selected].profileName : "");
        return;
    }
    lv_obj_clear_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);

    char when[32];
    formatWhen(startEpoch, when, sizeof(when));
    const char *profile = entries[selected].profileName;
    if (profile[0] != '\0' && when[0] != '\0')
        snprintf(buf, sizeof(buf), "%s  " LV_SYMBOL_BULLET "  %s", profile, when);
    else
        snprintf(buf, sizeof(buf), "%s", profile[0] != '\0' ? profile : when);
    lv_label_set_text(subtitle, buf);

    snprintf(buf, sizeof(buf), "%.1f s", durationS);
    lv_label_set_text(tileValues[0], buf);
    snprintf(buf, sizeof(buf), "%.1f g", yieldG);
    lv_label_set_text(tileValues[1], buf);
    if (doseG > 0.0f && yieldG > 0.0f)
        snprintf(buf, sizeof(buf), "1:%.1f", yieldG / doseG);
    else
        snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(tileValues[2], buf);
    snprintf(buf, sizeof(buf), "%.1f bar", peakBar);
    lv_label_set_text(tileValues[3], buf);

    lv_chart_set_point_count(chart, pointCount);
    for (uint16_t p = 0; p < pointCount; p++) {
        for (int i = 0; i < SERIES_COUNT; i++) {
            const float v = values[i][p];
            const bool isTarget = i == TARGET_PRESSURE || i == TARGET_FLOW;
            if ((isTarget || i == TEMPERATURE) && v <= 0.0f) {
                points[i][p] = LV_CHART_POINT_NONE;
            } else if (i == TEMPERATURE) {
                points[i][p] = normalise(v, tempLow, tempHigh);
            } else if (i == WEIGHT) {
                points[i][p] = normalise(v, 0.0f, weightRange);
            } else {
                points[i][p] = normalise(v, 0.0f, pumpRange);
            }
        }
    }
    lv_chart_refresh(chart);
}

void LastShotSidePanel::navCb(lv_event_t *event) {
    auto *panel = static_cast<LastShotSidePanel *>(lv_event_get_user_data(event));
    const bool older = lv_event_get_target(event) == panel->olderButton;
    if (older && panel->selected + 1 < panel->entryCount)
        panel->select(panel->selected + 1);
    else if (!older && panel->selected > 0)
        panel->select(panel->selected - 1);
}

void LastShotSidePanel::drawPartCb(lv_event_t *event) {
    auto *panel = static_cast<LastShotSidePanel *>(lv_event_get_user_data(event));
    auto *dsc = static_cast<lv_obj_draw_part_dsc_t *>(lv_event_get_param(event));

    if (dsc->part == LV_PART_TICKS && dsc->text != nullptr) {
        const float ratio = static_cast<float>(dsc->value) / CHART_SCALE;
        if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
            const float temp = panel->tempLow + ratio * (panel->tempHigh - panel->tempLow);
            lv_snprintf(dsc->text, dsc->text_length, "%d\xC2\xB0", static_cast<int>(lroundf(temp)));
        } else if (dsc->id == LV_CHART_AXIS_SECONDARY_Y) {
            lv_snprintf(dsc->text, dsc->text_length, "%d", static_cast<int>(lroundf(ratio * panel->pumpRange)));
        } else if (dsc->id == LV_CHART_AXIS_PRIMARY_X) {
            const float seconds = dsc->value * panel->durationS / (X_TICKS - 1);
            lv_snprintf(dsc->text, dsc->text_length, "%ds", static_cast<int>(lroundf(seconds)));
        }
        return;
    }

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
