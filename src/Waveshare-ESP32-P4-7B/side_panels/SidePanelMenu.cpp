#include "SidePanelMenu.h"

#include "SidePanelHost.h"
#include <cstdint>
#include <string>
#include <Waveshare-ESP32-P4-7B/Layout.h>

namespace waveshare_esp32_p4_7b {

static constexpr lv_coord_t MARGIN = 24;
static constexpr lv_coord_t HEADER_BUTTON_W = 110;
static constexpr lv_coord_t HEADER_BUTTON_H = 48;
static constexpr lv_coord_t LIST_TOP = 96;
static constexpr lv_coord_t LIST_ROW_H = 72;
static constexpr lv_coord_t LIST_ROW_GAP = 12;
static constexpr lv_coord_t COLUMN_HEADER_Y = 90;
static constexpr lv_coord_t ROWS_TOP = 124;
static constexpr lv_coord_t ROW_H = 72;
static constexpr lv_coord_t SWITCH_W = 60;
static constexpr lv_coord_t SWITCH_H = 32;
static constexpr lv_coord_t SHOW_COLUMN_X = 330;
static constexpr lv_coord_t DEFAULT_COLUMN_X = 430;

enum Column { SHOW, DEFAULT };

static std::string defaultSummary(const SidePanelEntry &entry) {
    std::string summary;
    for (int m = 0; m < SIDE_PANEL_MODE_COUNT; m++) {
        if (entry.defaults & modeBit(static_cast<SidePanelMode>(m))) {
            summary += summary.empty() ? "Default: " : ", ";
            summary += modeName(static_cast<SidePanelMode>(m));
        }
    }
    return summary.empty() ? "Not a default" : summary;
}

void SidePanelMenu::open(lv_obj_t *screen, SidePanelHost &panelHost) {
    host = &panelHost;
    theme = host->currentTheme();
    root = lv_obj_create(screen);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, SIDE_PANEL_W, SIDE_PANEL_H);
    lv_obj_set_style_bg_color(root, theme.background, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    showList();
}

void SidePanelMenu::close() {
    if (root == nullptr)
        return;
    lv_obj_del(root);
    root = nullptr;
    for (auto &row : switches)
        row[SHOW] = row[DEFAULT] = nullptr;
}

lv_obj_t *SidePanelMenu::addButton(lv_obj_t *parent, const char *text, lv_coord_t width, lv_coord_t height) {
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_mix(theme.text, theme.background, LV_OPA_10), 0);
    lv_obj_set_style_bg_color(button, lv_color_mix(theme.text, theme.background, LV_OPA_30), LV_STATE_PRESSED);
    if (text != nullptr) {
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, theme.text, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_center(label);
    }
    return button;
}

lv_obj_t *SidePanelMenu::addHeader(const char *title, const char *buttonText, lv_event_cb_t onButton) {
    lv_obj_t *label = lv_label_create(root);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, theme.text, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(label, MARGIN, 26);

    lv_obj_t *button = addButton(root, buttonText, HEADER_BUTTON_W, HEADER_BUTTON_H);
    lv_obj_set_pos(button, SIDE_PANEL_W - MARGIN - HEADER_BUTTON_W, 16);
    lv_obj_add_event_cb(button, onButton, LV_EVENT_CLICKED, this);
    return button;
}

void SidePanelMenu::showList() {
    lv_obj_clean(root);
    for (auto &row : switches)
        row[SHOW] = row[DEFAULT] = nullptr;
    addHeader("Side panels", "Done", onDone);

    const auto &entries = host->entries();
    for (size_t i = 0; i < entries.size(); i++) {
        lv_obj_t *button = addButton(root, nullptr, SIDE_PANEL_W - 2 * MARGIN, LIST_ROW_H);
        lv_obj_set_pos(button, MARGIN, LIST_TOP + static_cast<lv_coord_t>(i) * (LIST_ROW_H + LIST_ROW_GAP));
        lv_obj_set_user_data(button, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(button, onPanelSelected, LV_EVENT_CLICKED, this);

        lv_obj_t *name = lv_label_create(button);
        lv_label_set_text(name, entries[i].panel->name());
        lv_obj_set_style_text_color(name, theme.text, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 4, 2);

        lv_obj_t *summary = lv_label_create(button);
        lv_label_set_text(summary, defaultSummary(entries[i]).c_str());
        lv_obj_set_style_text_color(summary, theme.muted, 0);
        lv_obj_set_style_text_font(summary, &lv_font_montserrat_14, 0);
        lv_obj_align(summary, LV_ALIGN_BOTTOM_LEFT, 4, -2);

        lv_obj_t *arrow = lv_label_create(button);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arrow, theme.muted, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -4, 0);
    }
}

void SidePanelMenu::showPanel(size_t index) {
    selected = index;
    lv_obj_clean(root);
    addHeader(host->entries()[index].panel->name(), LV_SYMBOL_LEFT " Back", onBack);

    const char *columns[2] = {"Show", "Default"};
    const lv_coord_t columnX[2] = {SHOW_COLUMN_X, DEFAULT_COLUMN_X};
    for (int c = 0; c < 2; c++) {
        lv_obj_t *label = lv_label_create(root);
        lv_label_set_text(label, columns[c]);
        lv_obj_set_style_text_color(label, theme.muted, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, columnX[c], COLUMN_HEADER_Y);
        lv_obj_update_layout(label);
        lv_obj_set_x(label, columnX[c] + (SWITCH_W - lv_obj_get_width(label)) / 2);
    }

    for (int m = 0; m < SIDE_PANEL_MODE_COUNT; m++) {
        const lv_coord_t rowY = ROWS_TOP + m * ROW_H;
        lv_obj_t *label = lv_label_create(root);
        lv_label_set_text(label, modeName(static_cast<SidePanelMode>(m)));
        lv_obj_set_style_text_color(label, theme.text, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_set_pos(label, MARGIN, rowY + 6);

        for (int c = 0; c < 2; c++) {
            lv_obj_t *sw = lv_switch_create(root);
            lv_obj_set_size(sw, SWITCH_W, SWITCH_H);
            lv_obj_set_pos(sw, columnX[c], rowY);
            lv_obj_set_style_bg_color(sw, lv_color_mix(theme.muted, theme.background, LV_OPA_40), LV_PART_MAIN);
            lv_obj_set_style_bg_color(sw, theme.accent, LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(sw, theme.text, LV_PART_KNOB);
            lv_obj_set_user_data(sw, reinterpret_cast<void *>(static_cast<uintptr_t>(m * 2 + c)));
            lv_obj_add_event_cb(sw, onSwitch, LV_EVENT_VALUE_CHANGED, this);
            switches[m][c] = sw;
        }
    }
    refreshSwitches();
}

void SidePanelMenu::refreshSwitches() {
    const SidePanelEntry &entry = host->entries()[selected];
    for (int m = 0; m < SIDE_PANEL_MODE_COUNT; m++) {
        const uint8_t bit = modeBit(static_cast<SidePanelMode>(m));
        const bool flags[2] = {(entry.shown & bit) != 0, (entry.defaults & bit) != 0};
        for (int c = 0; c < 2; c++) {
            if (flags[c])
                lv_obj_add_state(switches[m][c], LV_STATE_CHECKED);
            else
                lv_obj_clear_state(switches[m][c], LV_STATE_CHECKED);
        }
    }
}

// Page changes rebuild the overlay, which deletes the button being clicked, so defer them.
void SidePanelMenu::onDone(lv_event_t *event) {
    lv_async_call(
        [](void *data) {
            auto *menu = static_cast<SidePanelMenu *>(data);
            menu->close();
            menu->host->menuClosed();
        },
        lv_event_get_user_data(event));
}

void SidePanelMenu::onBack(lv_event_t *event) {
    lv_async_call([](void *data) { static_cast<SidePanelMenu *>(data)->showList(); }, lv_event_get_user_data(event));
}

void SidePanelMenu::onPanelSelected(lv_event_t *event) {
    auto *menu = static_cast<SidePanelMenu *>(lv_event_get_user_data(event));
    menu->selected = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target(event))));
    lv_async_call(
        [](void *data) {
            auto *m = static_cast<SidePanelMenu *>(data);
            m->showPanel(m->selected);
        },
        menu);
}

void SidePanelMenu::onSwitch(lv_event_t *event) {
    auto *menu = static_cast<SidePanelMenu *>(lv_event_get_user_data(event));
    lv_obj_t *sw = lv_event_get_target(event);
    const auto code = static_cast<int>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(sw)));
    const auto mode = static_cast<SidePanelMode>(code / 2);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (code % 2 == SHOW)
        menu->host->setShown(menu->selected, mode, on);
    else
        menu->host->setDefault(menu->selected, mode, on);
    menu->refreshSwitches();
}

} // namespace waveshare_esp32_p4_7b
