// Hardware driver for the Waveshare ESP32-P4 7B: island LVGL + side-panel LVGL on one MIPI FB.
#pragma once

#include "P4DsiPanel.h"
#include <display/drivers/Driver.h>
#include <lvgl.h>

namespace waveshare_esp32_p4_7b {

class WaveshareEsp32P47BDriver : public Driver {
  public:
    bool isCompatible() override;
    void init() override;
    void setBrightness(int brightness) override;
    bool supportsSDCard() override { return false; }
    bool installSDCard() override { return false; }

    static WaveshareEsp32P47BDriver *getInstance();

  private:
    WaveshareEsp32P47BDriver() = default;

    void registerSideDisplay();
    static void sideFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
    static void sideTouchRead(lv_indev_drv_t *drv, lv_indev_data_t *data);

    static WaveshareEsp32P47BDriver *instance;
    P4DsiPanel panel;

    lv_disp_draw_buf_t sideDrawBuf{};
    lv_disp_drv_t sideDispDrv{};
    lv_indev_drv_t sideIndevDrv{};
    lv_color_t *sideBuf = nullptr;
    lv_disp_t *sideDisp = nullptr;
};

} // namespace waveshare_esp32_p4_7b
