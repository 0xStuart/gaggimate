// MIPI-DSI panel for the Waveshare ESP32-P4 7B (EK79007 1024x600).
// Exposes a 480x480 Display island so beginLvglHelper / DefaultUI stay unchanged;
// pushColors blits into the full framebuffer at the island origin.
#pragma once

#include "Layout.h"
#include <TouchDrvGT911.hpp>
#include <display/drivers/common/Display.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_ldo_regulator.h>

namespace waveshare_esp32_p4_7b {

enum class TouchOwner : uint8_t { None, Island, Side };

class P4DsiPanel : public Display {
  public:
    bool begin();
    void setBrightness(int level); // 0..16 (GaggiMate settings scale)

    void pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *data) override;
    void pushColorsSide(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *data);
    uint16_t width() override { return ISLAND_SIZE; }
    uint16_t height() override { return ISLAND_SIZE; }
    uint8_t getPoint(int16_t *x, int16_t *y, uint8_t get_point) override;
    bool supportsDirectMode() override { return false; }

    // Sample GT911 once; updates ownership on press/release. Returns true if finger down.
    bool sampleTouch();
    bool touchDown() const { return touched; }
    int16_t touchX() const { return rawX; }
    int16_t touchY() const { return rawY; }
    TouchOwner getTouchOwner() const { return touchOwner; }

    uint16_t *frameBuffer() { return fb; }

  private:
    bool initBacklight();
    bool initMipi();
    bool initTouch();
    void buildCircleEdges();
    void clearScreen();
    void blit(uint16_t dstX, uint16_t dstY, uint16_t w, uint16_t h, const uint16_t *data, bool roundClip);

    esp_ldo_channel_handle_t ldo = nullptr;
    esp_lcd_dsi_bus_handle_t dsiBus = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    uint16_t *fb = nullptr;

    TouchDrvGT911 touch;
    bool touchOk = false;
    TouchOwner touchOwner = TouchOwner::None;
    bool touched = false;
    bool wasTouched = false;
    int16_t rawX = 0;
    int16_t rawY = 0;

    // Per-row inclusive x range inside the island circle (island-local coords).
    int16_t circleLeft[ISLAND_SIZE]{};
    int16_t circleRight[ISLAND_SIZE]{};
};

} // namespace waveshare_esp32_p4_7b
