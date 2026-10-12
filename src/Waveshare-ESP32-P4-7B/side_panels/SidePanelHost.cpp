#include "SidePanelHost.h"

#include "ClockSidePanel.h"
#include "GaugesSidePanel.h"
#include "GraphSidePanel.h"
#include "LastShotSidePanel.h"
#include <Preferences.h>
#include <display/core/constants.h>
#include <display/main.h>
#include <display/ui/default/eez/screens.h>
#include <string>
#include <Waveshare-ESP32-P4-7B/Layout.h>

namespace waveshare_esp32_p4_7b {

static constexpr uint32_t SAMPLE_INTERVAL_MS = 100;
static constexpr uint32_t LONG_PRESS_MS = 2000;
static constexpr lv_coord_t LONG_PRESS_SLOP = 24; // finger drift still counted as holding
static constexpr uint32_t BREW_LINGER_MS = 15000; // stay in Brewing after the shot so its graph stays up
static constexpr int DOT_SIZE = 10;
static constexpr int DOT_GAP = 14;
static constexpr int DOTS_BOTTOM_MARGIN = 22;
static constexpr int THEME_COUNT = 3;
// NVS namespaces are limited to 15 characters. This is the Waveshare ESP32-P4 7B.
static constexpr const char *PREFS_NAMESPACE = "ws_p4_7b";

static constexpr uint8_t ALL_ACTIVE_MODES = modeBit(SidePanelMode::BrewReady) | modeBit(SidePanelMode::Brewing) |
                                            modeBit(SidePanelMode::Steam) | modeBit(SidePanelMode::Water) |
                                            modeBit(SidePanelMode::Grind);

SidePanelHost &SidePanelHost::instance() {
    static SidePanelHost host;
    return host;
}

void SidePanelHost::add(SidePanel *panel, uint8_t shown, uint8_t defaults) {
    panels.push_back(SidePanelEntry{panel, static_cast<uint8_t>(shown | defaults), defaults});
}

void SidePanelHost::begin(lv_disp_t *disp) {
    if (panels.empty()) {
        add(new ClockSidePanel(), modeBit(SidePanelMode::Standby), modeBit(SidePanelMode::Standby));
        add(new GaugesSidePanel(), ALL_ACTIVE_MODES,
            modeBit(SidePanelMode::BrewReady) | modeBit(SidePanelMode::Steam) | modeBit(SidePanelMode::Water) |
                modeBit(SidePanelMode::Grind));
        add(new GraphSidePanel(),
            modeBit(SidePanelMode::BrewReady) | modeBit(SidePanelMode::Brewing) | modeBit(SidePanelMode::Steam) |
                modeBit(SidePanelMode::Water),
            modeBit(SidePanelMode::Brewing));
        add(new LastShotSidePanel(), modeBit(SidePanelMode::Standby) | modeBit(SidePanelMode::BrewReady), 0);
    }
    loadFlags();

    screen = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, onGesture, LV_EVENT_GESTURE, this);
    lv_obj_add_event_cb(screen, onPress, LV_EVENT_ALL, this);

    dots = lv_obj_create(screen);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, DOT_SIZE);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dots, DOT_GAP, 0);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -DOTS_BOTTOM_MARGIN);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);

    mode = detectMode();
    showDefault();
    lv_timer_create(onTimer, SAMPLE_INTERVAL_MS, this);
}

void SidePanelHost::loadFlags() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    for (SidePanelEntry &entry : panels) {
        const std::string id = entry.panel->id();
        entry.shown = static_cast<uint8_t>(prefs.getUInt((id + "_s").c_str(), entry.shown));
        entry.defaults = static_cast<uint8_t>(prefs.getUInt((id + "_d").c_str(), entry.defaults));
    }
    prefs.end();
}

void SidePanelHost::saveFlags(size_t index) const {
    const SidePanelEntry &entry = panels[index];
    const std::string id = entry.panel->id();
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUInt((id + "_s").c_str(), entry.shown);
    prefs.putUInt((id + "_d").c_str(), entry.defaults);
    prefs.end();
}

void SidePanelHost::setShown(size_t index, SidePanelMode m, bool on) {
    SidePanelEntry &entry = panels[index];
    if (on) {
        entry.shown |= modeBit(m);
    } else {
        entry.shown &= ~modeBit(m);
        entry.defaults &= ~modeBit(m);
    }
    saveFlags(index);
}

// A mode has at most one default side panel, and a default side panel is always shown.
void SidePanelHost::setDefault(size_t index, SidePanelMode m, bool on) {
    if (on) {
        for (size_t i = 0; i < panels.size(); i++) {
            if (i != index && (panels[i].defaults & modeBit(m))) {
                panels[i].defaults &= ~modeBit(m);
                saveFlags(i);
            }
        }
        panels[index].defaults |= modeBit(m);
        panels[index].shown |= modeBit(m);
    } else {
        panels[index].defaults &= ~modeBit(m);
    }
    saveFlags(index);
}

SidePanelMode SidePanelHost::detectMode() {
    const uint32_t now = lv_tick_get();
    if (controller.isBrewActive()) {
        hasBrewed = true;
        lastBrewMs = now;
        return SidePanelMode::Brewing;
    }
    switch (controller.getMode()) {
    case MODE_BREW:
        return hasBrewed && now - lastBrewMs < BREW_LINGER_MS ? SidePanelMode::Brewing : SidePanelMode::BrewReady;
    case MODE_STEAM:
        return SidePanelMode::Steam;
    case MODE_WATER:
        return SidePanelMode::Water;
    case MODE_GRIND:
        return SidePanelMode::Grind;
    default:
        return SidePanelMode::Standby;
    }
}

bool SidePanelHost::isShown(int index) const {
    return index >= 0 && index < static_cast<int>(panels.size()) && (panels[index].shown & modeBit(mode));
}

void SidePanelHost::showDefault() {
    int fallback = -1;
    for (size_t i = 0; i < panels.size(); i++) {
        if (panels[i].defaults & modeBit(mode)) {
            show(static_cast<int>(i));
            return;
        }
        if (fallback < 0 && isShown(static_cast<int>(i)))
            fallback = static_cast<int>(i);
    }
    show(fallback);
}

SidePanelTheme SidePanelHost::currentTheme() const {
    const uint32_t *c = theme_colors[themeIndex < 0 ? 0 : themeIndex];
    return SidePanelTheme{
        .text = lv_color_hex(c[0]),
        .background = lv_color_hex(c[1]),
        .accent = lv_color_hex(c[2]),
        .muted = lv_color_hex(c[3]),
        .red = lv_color_hex(c[6]),
        .blue = lv_color_hex(c[7]),
        .amber = lv_color_hex(c[8]),
    };
}

void SidePanelHost::hidePanel() {
    if (content != nullptr) {
        panels[active].panel->destroy();
        lv_obj_del(content);
        content = nullptr;
    }
}

void SidePanelHost::show(int index) {
    hidePanel();
    if (themeIndex < 0) {
        const int themeMode = controller.getSettings().getThemeMode();
        themeIndex = themeMode >= 0 && themeMode < THEME_COUNT ? themeMode : 0;
    }
    active = index;
    if (active >= 0) {
        content = lv_obj_create(screen);
        lv_obj_remove_style_all(content);
        lv_obj_set_size(content, SIDE_PANEL_W, SIDE_PANEL_H);
        lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_background(content);
        panels[active].panel->create(content, currentTheme());
    }
    updateDots();
}

void SidePanelHost::step(int direction) {
    const int count = static_cast<int>(panels.size());
    for (int offset = 1; offset < count; offset++) {
        const int candidate = ((active < 0 ? 0 : active) + direction * offset + count * count) % count;
        if (isShown(candidate)) {
            show(candidate);
            return;
        }
    }
}

void SidePanelHost::updateDots() {
    lv_obj_clean(dots);
    int shownCount = 0;
    const SidePanelTheme theme = currentTheme();
    for (size_t i = 0; i < panels.size(); i++) {
        if (!isShown(static_cast<int>(i)))
            continue;
        shownCount++;
        const bool current = static_cast<int>(i) == active;
        lv_obj_t *dot = lv_obj_create(dots);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, current ? theme.accent : theme.muted, 0);
        lv_obj_set_style_bg_opa(dot, current ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
    if (shownCount < 2 || menu.isOpen())
        lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_clear_flag(dots, LV_OBJ_FLAG_HIDDEN);
}

void SidePanelHost::menuClosed() {
    if (isShown(active))
        show(active);
    else
        showDefault();
}

void SidePanelHost::sample(Telemetry &t) {
    t.temp = controller.getCurrentTemp();
    t.targetTemp = controller.getTargetTemp();
    t.pressure = controller.getCurrentPressure();
    t.targetPressure = controller.getTargetPressure();
    t.flow = controller.getCurrentPuckFlow();
    t.targetFlow = controller.getTargetFlow();
    t.weight = controller.getCurrentCoffeeVolume();
    t.brewing = controller.isBrewActive();

    const uint32_t now = lv_tick_get();
    if (t.brewing && !wasBrewing)
        brewStartMs = now;
    wasBrewing = t.brewing;
    t.brewMs = t.brewing ? now - brewStartMs : 0;
}

void SidePanelHost::onTimer(lv_timer_t *timer) {
    auto *host = static_cast<SidePanelHost *>(timer->user_data);

    Telemetry telemetry;
    host->sample(telemetry);
    for (SidePanelEntry &entry : host->panels)
        entry.panel->observe(telemetry);

    const SidePanelMode newMode = host->detectMode();
    if (host->menu.isOpen()) {
        host->mode = newMode;
        return;
    }

    const int themeMode = controller.getSettings().getThemeMode();
    const int theme = themeMode >= 0 && themeMode < THEME_COUNT ? themeMode : 0;
    if (theme != host->themeIndex) {
        host->themeIndex = theme;
        host->show(host->active);
    }
    if (newMode != host->mode) {
        host->mode = newMode;
        host->showDefault();
    }
    if (host->active >= 0)
        host->panels[host->active].panel->update(telemetry);
}

void SidePanelHost::onGesture(lv_event_t *event) {
    auto *host = static_cast<SidePanelHost *>(lv_event_get_user_data(event));
    if (host->menu.isOpen())
        return;
    lv_indev_t *indev = lv_indev_get_act();
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT)
        return;
    host->longPressArmed = false;
    // Deleting the old side panel resets the gesture state, so the rest of this drag would swipe again.
    lv_indev_wait_release(indev);
    host->step(dir == LV_DIR_LEFT ? 1 : -1);
}

void SidePanelHost::onPress(lv_event_t *event) {
    auto *host = static_cast<SidePanelHost *>(lv_event_get_user_data(event));
    const lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == nullptr)
        return;

    if (code == LV_EVENT_PRESSED) {
        host->longPressArmed = !host->menu.isOpen();
        host->pressStartMs = lv_tick_get();
        lv_indev_get_point(indev, &host->pressStart);
    } else if (code == LV_EVENT_PRESSING && host->longPressArmed) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        if (LV_ABS(point.x - host->pressStart.x) > LONG_PRESS_SLOP || LV_ABS(point.y - host->pressStart.y) > LONG_PRESS_SLOP) {
            host->longPressArmed = false;
        } else if (lv_tick_elaps(host->pressStartMs) >= LONG_PRESS_MS) {
            host->longPressArmed = false;
            lv_indev_wait_release(indev);
            host->hidePanel();
            host->menu.open(host->screen, *host);
            host->updateDots();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        host->longPressArmed = false;
    }
}

} // namespace waveshare_esp32_p4_7b
