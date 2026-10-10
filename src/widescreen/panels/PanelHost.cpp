#include "PanelHost.h"

#include "ClockPanel.h"
#include "GaugesPanel.h"
#include "GraphPanel.h"
#include <Preferences.h>
#include <cstring>
#include <display/main.h>
#include <display/ui/default/eez/screens.h>
#include <widescreen/Layout.h>

namespace widescreen {

static constexpr uint32_t SAMPLE_INTERVAL_MS = 100;
static constexpr int DOT_SIZE = 10;
static constexpr int DOT_GAP = 14;
static constexpr int DOTS_BOTTOM_MARGIN = 22;
static constexpr int THEME_COUNT = 3;
static constexpr const char *PREFS_NAMESPACE = "widescreen";
static constexpr const char *PREFS_PANEL_KEY = "panel";

PanelHost &PanelHost::instance() {
    static PanelHost host;
    return host;
}

void PanelHost::add(SidePanel *panel) { panels.push_back(panel); }

void PanelHost::begin(lv_disp_t *disp) {
    if (panels.empty()) {
        add(new ClockPanel());
        add(new GaugesPanel());
        add(new GraphPanel());
    }

    screen = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, onGesture, LV_EVENT_GESTURE, this);

    dots = lv_obj_create(screen);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, DOT_SIZE);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dots, DOT_GAP, 0);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -DOTS_BOTTOM_MARGIN);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);
    for (size_t i = 0; i < panels.size(); i++) {
        lv_obj_t *dot = lv_obj_create(dots);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
    if (panels.size() < 2)
        lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);

    int initial = 0;
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    const String saved = prefs.getString(PREFS_PANEL_KEY, "");
    prefs.end();
    for (size_t i = 0; i < panels.size(); i++) {
        if (saved == panels[i]->id())
            initial = static_cast<int>(i);
    }
    show(initial);

    lv_timer_create(onTimer, SAMPLE_INTERVAL_MS, this);
}

PanelTheme PanelHost::currentTheme() const {
    const uint32_t *c = theme_colors[themeIndex];
    return PanelTheme{
        .text = lv_color_hex(c[0]),
        .background = lv_color_hex(c[1]),
        .accent = lv_color_hex(c[2]),
        .muted = lv_color_hex(c[3]),
        .red = lv_color_hex(c[6]),
        .blue = lv_color_hex(c[7]),
        .amber = lv_color_hex(c[8]),
    };
}

void PanelHost::show(int index) {
    if (content != nullptr) {
        panels[active]->destroy();
        lv_obj_del(content);
        content = nullptr;
    }
    if (themeIndex < 0) {
        const int mode = controller.getSettings().getThemeMode();
        themeIndex = mode >= 0 && mode < THEME_COUNT ? mode : 0;
    }

    content = lv_obj_create(screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, PANEL_W, PANEL_H);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(content);

    const bool changed = index != active;
    active = index;
    panels[active]->create(content, currentTheme());
    updateDots();

    if (changed) {
        Preferences prefs;
        prefs.begin(PREFS_NAMESPACE, false);
        prefs.putString(PREFS_PANEL_KEY, panels[active]->id());
        prefs.end();
    }
}

void PanelHost::updateDots() {
    const PanelTheme theme = currentTheme();
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(dots); i++) {
        lv_obj_t *dot = lv_obj_get_child(dots, i);
        lv_obj_set_style_bg_color(dot, static_cast<int>(i) == active ? theme.accent : theme.muted, 0);
        lv_obj_set_style_bg_opa(dot, static_cast<int>(i) == active ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

void PanelHost::sample(Telemetry &t) {
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

void PanelHost::onTimer(lv_timer_t *timer) {
    auto *host = static_cast<PanelHost *>(timer->user_data);
    if (host->active < 0)
        return;

    const int mode = controller.getSettings().getThemeMode();
    const int theme = mode >= 0 && mode < THEME_COUNT ? mode : 0;
    if (theme != host->themeIndex) {
        host->themeIndex = theme;
        host->show(host->active);
    }

    Telemetry telemetry;
    host->sample(telemetry);
    for (SidePanel *panel : host->panels)
        panel->observe(telemetry);
    host->panels[host->active]->update(telemetry);
}

void PanelHost::onGesture(lv_event_t *event) {
    auto *host = static_cast<PanelHost *>(lv_event_get_user_data(event));
    const int count = static_cast<int>(host->panels.size());
    if (count < 2)
        return;
    lv_indev_t *indev = lv_indev_get_act();
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT)
        return;
    // Deleting the old panel resets the gesture state, so the rest of this drag would swipe again.
    lv_indev_wait_release(indev);
    host->show(dir == LV_DIR_LEFT ? (host->active + 1) % count : (host->active + count - 1) % count);
}

} // namespace widescreen
