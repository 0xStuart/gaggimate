#include "MachineStatsSidePanel.h"

#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <display/config.h>
#include <display/core/Controller.h>
#include <display/main.h>
#include <version.h>
#include <Waveshare-ESP32-P4-7B/Layout.h>

namespace waveshare_esp32_p4_7b {

namespace {

constexpr lv_coord_t MARGIN_X = 28;
constexpr lv_coord_t ROW_H = 32;
constexpr lv_coord_t LABEL_W = 140;
constexpr lv_coord_t QR_SIZE = 200;
constexpr uint32_t REFRESH_MS = 1000;

lv_obj_t *makeLabel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, const lv_font_t *font, lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

void makeRow(lv_obj_t *parent, lv_coord_t y, const char *key, const SidePanelTheme &theme, lv_obj_t **valueOut) {
    lv_obj_t *keyLabel = makeLabel(parent, MARGIN_X, y, LABEL_W, &lv_font_montserrat_16, theme.muted);
    lv_label_set_text(keyLabel, key);
    *valueOut = makeLabel(parent, MARGIN_X + LABEL_W, y, SIDE_PANEL_W - MARGIN_X * 2 - LABEL_W, &lv_font_montserrat_16,
                          theme.text);
    lv_label_set_text(*valueOut, "—");
}

String wifiJoinQr(const String &password) {
    String escaped;
    escaped.reserve(password.length() + 4);
    for (size_t i = 0; i < password.length(); i++) {
        const char c = password.charAt(i);
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
            escaped += '\\';
        }
        escaped += c;
    }
    if (escaped.isEmpty()) {
        return String("WIFI:S:") + WIFI_AP_SSID + ";;;;";
    }
    return String("WIFI:S:") + WIFI_AP_SSID + ";T:WPA;P:" + escaped + ";;";
}

} // namespace

void MachineStatsSidePanel::create(lv_obj_t *parent, const SidePanelTheme &theme) {
    title = makeLabel(parent, MARGIN_X, 28, SIDE_PANEL_W - MARGIN_X * 2, &lv_font_montserrat_24, theme.text);
    lv_label_set_text(title, "Machine");

    lv_coord_t y = 88;
    makeRow(parent, y, "Display", theme, &displayVersion);
    y += ROW_H;
    makeRow(parent, y, "Controller", theme, &controllerVersion);
    y += ROW_H;
    makeRow(parent, y, "Network", theme, &network);
    y += ROW_H;
    makeRow(parent, y, "IP", theme, &ip);
    y += ROW_H;
    makeRow(parent, y, "AP pass", theme, &apPassword);
    y += ROW_H + 12;

    qr = lv_qrcode_create(parent, QR_SIZE, theme.text, theme.background);
    lv_obj_set_pos(qr, (SIDE_PANEL_W - QR_SIZE) / 2, y);
    lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);

    qrHint = makeLabel(parent, MARGIN_X, y + QR_SIZE + 10, SIDE_PANEL_W - MARGIN_X * 2, &lv_font_montserrat_14, theme.muted);
    lv_obj_set_style_text_align(qrHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(qrHint, "");
    lv_obj_add_flag(qrHint, LV_OBJ_FLAG_HIDDEN);

    lastAp = false;
    lastDisplay[0] = lastController[0] = lastNetwork[0] = lastIp[0] = lastApPassword[0] = lastQr[0] = '\0';
    lastRefreshMs = 0;
    refresh();
}

void MachineStatsSidePanel::destroy() {
    title = displayVersion = controllerVersion = network = ip = apPassword = qr = qrHint = nullptr;
}

void MachineStatsSidePanel::update(const Telemetry &) {
    const uint32_t now = millis();
    if (lastRefreshMs != 0 && now - lastRefreshMs < REFRESH_MS) {
        return;
    }
    lastRefreshMs = now;
    refresh();
}

void MachineStatsSidePanel::refresh() {
    if (displayVersion == nullptr) {
        return;
    }

    const bool ap = controller.isAccessPoint();
    const bool wifiSta = !ap && WiFi.status() == WL_CONNECTED;
    const auto info = controller.getSystemInfo();
    const auto &settings = controller.getSettings();

    char displayBuf[40];
    snprintf(displayBuf, sizeof(displayBuf), "%s", BUILD_GIT_VERSION);

    char controllerBuf[40];
    if (controller.getClientController()->isConnected() && info.version.length() > 0) {
        snprintf(controllerBuf, sizeof(controllerBuf), "%s", info.version.c_str());
    } else {
        snprintf(controllerBuf, sizeof(controllerBuf), "%s", "—");
    }

    char networkBuf[48];
    if (ap) {
        snprintf(networkBuf, sizeof(networkBuf), "%s (AP)", WIFI_AP_SSID);
    } else if (wifiSta) {
        snprintf(networkBuf, sizeof(networkBuf), "%s", settings.getWifiSsid().c_str());
    } else {
        snprintf(networkBuf, sizeof(networkBuf), "%s", "Disconnected");
    }

    char ipBuf[24];
    if (ap) {
        const String apIp = WIFI_AP_IP.toString();
        snprintf(ipBuf, sizeof(ipBuf), "%s", apIp.c_str());
    } else if (wifiSta) {
        const String staIp = WiFi.localIP().toString();
        snprintf(ipBuf, sizeof(ipBuf), "%s", staIp.c_str());
    } else {
        snprintf(ipBuf, sizeof(ipBuf), "%s", "—");
    }

    char apPassBuf[64];
    if (ap) {
        const String apPw = settings.getWifiApPassword();
        snprintf(apPassBuf, sizeof(apPassBuf), "%s", apPw.c_str());
    } else {
        snprintf(apPassBuf, sizeof(apPassBuf), "%s", "—");
    }

    if (strcmp(lastDisplay, displayBuf) != 0) {
        strncpy(lastDisplay, displayBuf, sizeof(lastDisplay) - 1);
        lv_label_set_text(displayVersion, lastDisplay);
    }
    if (strcmp(lastController, controllerBuf) != 0) {
        strncpy(lastController, controllerBuf, sizeof(lastController) - 1);
        lv_label_set_text(controllerVersion, lastController);
    }
    if (strcmp(lastNetwork, networkBuf) != 0) {
        strncpy(lastNetwork, networkBuf, sizeof(lastNetwork) - 1);
        lv_label_set_text(network, lastNetwork);
    }
    if (strcmp(lastIp, ipBuf) != 0) {
        strncpy(lastIp, ipBuf, sizeof(lastIp) - 1);
        lv_label_set_text(ip, lastIp);
    }
    if (strcmp(lastApPassword, apPassBuf) != 0) {
        strncpy(lastApPassword, apPassBuf, sizeof(lastApPassword) - 1);
        lastApPassword[sizeof(lastApPassword) - 1] = '\0';
        lv_label_set_text(apPassword, lastApPassword);
    }

    if (ap) {
        const String qrContent = wifiJoinQr(settings.getWifiApPassword());
        if (!lastAp || strcmp(lastQr, qrContent.c_str()) != 0) {
            strncpy(lastQr, qrContent.c_str(), sizeof(lastQr) - 1);
            lastQr[sizeof(lastQr) - 1] = '\0';
            lv_qrcode_update(qr, lastQr, strlen(lastQr));
        }
        lv_obj_clear_flag(qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(qrHint, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(qrHint, "Scan to join Wi-Fi\nthen open http://4.4.4.1/");
    } else {
        lastQr[0] = '\0';
        lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(qrHint, LV_OBJ_FLAG_HIDDEN);
    }
    lastAp = ap;
}

} // namespace waveshare_esp32_p4_7b
