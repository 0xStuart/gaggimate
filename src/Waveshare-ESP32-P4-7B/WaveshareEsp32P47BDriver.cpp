#include "WaveshareEsp32P47BDriver.h"

#include "Layout.h"
#include "side_panels/SidePanelHost.h"
#include <Arduino.h>
#include <display/drivers/common/LV_Helper.h>
#include <esp_heap_caps.h>

namespace waveshare_esp32_p4_7b {

WaveshareEsp32P47BDriver *WaveshareEsp32P47BDriver::instance = nullptr;

WaveshareEsp32P47BDriver *WaveshareEsp32P47BDriver::getInstance() {
    if (instance == nullptr) {
        instance = new WaveshareEsp32P47BDriver();
    }
    return instance;
}

bool WaveshareEsp32P47BDriver::isCompatible() {
#ifdef WAVESHARE_ESP32_P4_7B
    return true;
#else
    return false;
#endif
}

void WaveshareEsp32P47BDriver::setBrightness(int brightness) { panel.setBrightness(brightness); }

void WaveshareEsp32P47BDriver::init() {
    Serial.println(F("WaveshareEsp32P47BDriver: init"));
    if (!panel.begin()) {
        for (uint8_t i = 0; i < 20; i++) {
            Serial.println(F("WaveshareEsp32P47BDriver: panel begin failed"));
            delay(1000);
        }
        ESP.restart();
    }

    // Island first so it remains LVGL's default display for DefaultUI / EEZ.
    beginLvglHelper(panel);
    registerSideDisplay();
    SidePanelHost::instance().begin(sideDisp);
    Serial.println(F("WaveshareEsp32P47BDriver: ready"));
}

void WaveshareEsp32P47BDriver::registerSideDisplay() {
    const size_t pixels = static_cast<size_t>(SIDE_PANEL_W) * SIDE_PANEL_H;
    sideBuf = static_cast<lv_color_t *>(heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!sideBuf) {
        sideBuf = static_cast<lv_color_t *>(malloc(pixels * sizeof(lv_color_t)));
    }
    assert(sideBuf);

    lv_disp_draw_buf_init(&sideDrawBuf, sideBuf, nullptr, pixels);

    lv_disp_drv_init(&sideDispDrv);
    sideDispDrv.hor_res = SIDE_PANEL_W;
    sideDispDrv.ver_res = SIDE_PANEL_H;
    sideDispDrv.flush_cb = sideFlush;
    sideDispDrv.draw_buf = &sideDrawBuf;
    sideDispDrv.user_data = this;
    sideDisp = lv_disp_drv_register(&sideDispDrv);

    lv_indev_drv_init(&sideIndevDrv);
    sideIndevDrv.type = LV_INDEV_TYPE_POINTER;
    sideIndevDrv.read_cb = sideTouchRead;
    sideIndevDrv.disp = sideDisp;
    sideIndevDrv.user_data = this;
    lv_indev_drv_register(&sideIndevDrv);
}

void WaveshareEsp32P47BDriver::sideFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    auto *self = static_cast<WaveshareEsp32P47BDriver *>(drv->user_data);
    // Match LV_Helper / LilyGo: pass exclusive end as width/height.
    self->panel.pushColorsSide(area->x1, area->y1, area->x2 + 1, area->y2 + 1, reinterpret_cast<uint16_t *>(color_p));
    lv_disp_flush_ready(drv);
}

void WaveshareEsp32P47BDriver::sideTouchRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    auto *self = static_cast<WaveshareEsp32P47BDriver *>(drv->user_data);
    self->panel.sampleTouch();
    data->point.x = LV_CLAMP(0, self->panel.touchX() - SIDE_PANEL_X, SIDE_PANEL_W - 1);
    data->point.y = LV_CLAMP(0, self->panel.touchY(), SIDE_PANEL_H - 1);
    data->state =
        (self->panel.touchDown() && self->panel.getTouchOwner() == TouchOwner::Side) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

} // namespace waveshare_esp32_p4_7b
