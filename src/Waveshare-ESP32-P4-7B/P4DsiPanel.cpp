#include "P4DsiPanel.h"

#include <Arduino.h>
#include <Wire.h>
#include <cmath>
#include <cstring>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_vendor.h>

namespace waveshare_esp32_p4_7b {

namespace {

constexpr int BL_GPIO = 32;
constexpr int LCD_RST_GPIO = 33;
constexpr int TOUCH_SDA = 7;
constexpr int TOUCH_SCL = 8;
constexpr int TOUCH_RST = 23;
constexpr int LDO_CHAN = 3;
constexpr int LDO_MV = 2500;
constexpr uint32_t LANE_MBPS = 1000;
constexpr uint32_t DPI_MHZ = 52;
constexpr int BL_FREQ = 5000;
constexpr int BL_RES_BITS = 10;
constexpr uint32_t BL_MAX = (1u << BL_RES_BITS) - 1u;

struct InitCmd {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned delay_ms;
};

// Waveshare LCD-7B EK79007 sequence (2-lane pad control + vendor regs + sleep-out).
// R36h default is 0x01 (UPDN=0, SHLR=1). 0x02 = UPDN=1, SHLR=0 → 180° vs default.
const InitCmd kInitCmds[] = {
    {0xB2, (const uint8_t[]){0x10}, 1, 0},
    {0x80, (const uint8_t[]){0x8B}, 1, 0},
    {0x81, (const uint8_t[]){0x78}, 1, 0},
    {0x82, (const uint8_t[]){0x84}, 1, 0},
    {0x83, (const uint8_t[]){0x88}, 1, 0},
    {0x84, (const uint8_t[]){0xA8}, 1, 0},
    {0x85, (const uint8_t[]){0xE3}, 1, 0},
    {0x86, (const uint8_t[]){0x88}, 1, 0},
#if ROTATE_180
    {0x36, (const uint8_t[]){0x02}, 1, 0},
#endif
    {0x11, nullptr, 0, 120},
};

} // namespace

bool P4DsiPanel::begin() {
    if (fb) {
        return true;
    }

    buildCircleEdges();
    if (!initBacklight()) {
        Serial.println(F("P4DsiPanel: backlight init failed"));
        return false;
    }
    setBrightness(16);

    if (!initMipi()) {
        Serial.println(F("P4DsiPanel: MIPI init failed"));
        return false;
    }
    clearScreen();

    touchOk = initTouch();
    if (!touchOk) {
        Serial.println(F("P4DsiPanel: GT911 not found (display still usable)"));
    }
    return true;
}

void P4DsiPanel::buildCircleEdges() {
    const double c = ISLAND_SIZE / 2.0;
    const double r2 = c * c;
    for (int y = 0; y < ISLAND_SIZE; y++) {
        const double dy = y + 0.5 - c;
        const double rem = r2 - dy * dy;
        if (rem <= 0) {
            circleLeft[y] = 1;
            circleRight[y] = 0; // empty row
            continue;
        }
        const double dx = std::sqrt(rem);
        int left = static_cast<int>(std::ceil(c - dx - 0.5));
        int right = static_cast<int>(std::floor(c + dx - 0.5));
        if (left < 0)
            left = 0;
        if (right >= ISLAND_SIZE)
            right = ISLAND_SIZE - 1;
        circleLeft[y] = static_cast<int16_t>(left);
        circleRight[y] = static_cast<int16_t>(right);
    }
}

bool P4DsiPanel::initBacklight() {
    if (!ledcAttach(BL_GPIO, BL_FREQ, BL_RES_BITS)) {
        return false;
    }
    // Active-low backlight: invert so high duty = bright (matches Waveshare BSP).
    if (!ledcOutputInvert(BL_GPIO, true)) {
        return false;
    }
    return ledcWrite(BL_GPIO, 0);
}

void P4DsiPanel::setBrightness(int level) {
    if (level < 0)
        level = 0;
    if (level > 16)
        level = 16;
    const uint32_t duty = (BL_MAX * static_cast<uint32_t>(level) + 8) / 16;
    ledcWrite(BL_GPIO, duty);
}

bool P4DsiPanel::initMipi() {
    esp_ldo_channel_config_t ldoCfg = {
        .chan_id = LDO_CHAN,
        .voltage_mv = LDO_MV,
    };
    if (esp_ldo_acquire_channel(&ldoCfg, &ldo) != ESP_OK) {
        Serial.println(F("P4DsiPanel: LDO3 acquire failed"));
        return false;
    }

    // Active-low reset (Waveshare LCD-7B).
    pinMode(LCD_RST_GPIO, OUTPUT);
    digitalWrite(LCD_RST_GPIO, LOW);
    delay(10);
    digitalWrite(LCD_RST_GPIO, HIGH);
    delay(20);

    esp_lcd_dsi_bus_config_t busCfg = {};
    busCfg.bus_id = 0;
    busCfg.num_data_lanes = 2;
    busCfg.phy_clk_src = static_cast<mipi_dsi_phy_pllref_clock_source_t>(0);
    busCfg.lane_bit_rate_mbps = LANE_MBPS;
    if (esp_lcd_new_dsi_bus(&busCfg, &dsiBus) != ESP_OK) {
        Serial.println(F("P4DsiPanel: DSI bus failed"));
        return false;
    }

    esp_lcd_dbi_io_config_t dbiCfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_dbi(dsiBus, &dbiCfg, &io) != ESP_OK) {
        Serial.println(F("P4DsiPanel: DBI IO failed"));
        return false;
    }

    esp_lcd_dpi_panel_config_t dpiCfg = {};
    dpiCfg.virtual_channel = 0;
    dpiCfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpiCfg.dpi_clock_freq_mhz = DPI_MHZ;
    dpiCfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpiCfg.num_fbs = 1;
    dpiCfg.video_timing.h_size = SCREEN_W;
    dpiCfg.video_timing.v_size = SCREEN_H;
    dpiCfg.video_timing.hsync_pulse_width = 10;
    dpiCfg.video_timing.hsync_back_porch = 160;
    dpiCfg.video_timing.hsync_front_porch = 160;
    dpiCfg.video_timing.vsync_pulse_width = 1;
    dpiCfg.video_timing.vsync_back_porch = 23;
    dpiCfg.video_timing.vsync_front_porch = 12;
    dpiCfg.flags.use_dma2d = true;

    if (esp_lcd_new_panel_dpi(dsiBus, &dpiCfg, &panel) != ESP_OK) {
        Serial.println(F("P4DsiPanel: DPI panel failed"));
        return false;
    }

    for (const InitCmd &cmd : kInitCmds) {
        if (esp_lcd_panel_io_tx_param(io, cmd.cmd, cmd.data, cmd.data_bytes) != ESP_OK) {
            Serial.printf("P4DsiPanel: init cmd 0x%02X failed\n", cmd.cmd);
            return false;
        }
        if (cmd.delay_ms) {
            delay(cmd.delay_ms);
        }
    }

    if (esp_lcd_panel_init(panel) != ESP_OK) {
        Serial.println(F("P4DsiPanel: panel_init failed"));
        return false;
    }

    void *fb0 = nullptr;
    if (esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb0) != ESP_OK || fb0 == nullptr) {
        Serial.println(F("P4DsiPanel: get_frame_buffer failed"));
        return false;
    }
    fb = static_cast<uint16_t *>(fb0);
    Serial.printf("P4DsiPanel: FB %p %dx%d RGB565\n", fb, SCREEN_W, SCREEN_H);
    return true;
}

bool P4DsiPanel::initTouch() {
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(50);

    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(400000);

    // Probe both GT911 addresses (Waveshare leaves INT floating; RST wired to GPIO23).
    if (touch.begin(Wire, GT911_SLAVE_ADDRESS_L, TOUCH_SDA, TOUCH_SCL)) {
        Serial.println(F("P4DsiPanel: GT911 @ 0x5D"));
        return true;
    }
    if (touch.begin(Wire, GT911_SLAVE_ADDRESS_H, TOUCH_SDA, TOUCH_SCL)) {
        Serial.println(F("P4DsiPanel: GT911 @ 0x14"));
        return true;
    }
    return false;
}

void P4DsiPanel::clearScreen() {
    if (!fb) {
        return;
    }
    const size_t bytes = static_cast<size_t>(SCREEN_W) * SCREEN_H * sizeof(uint16_t);
    memset(fb, 0, bytes);
    esp_cache_msync(fb, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

void P4DsiPanel::blit(uint16_t dstX, uint16_t dstY, uint16_t w, uint16_t h, const uint16_t *data, bool roundClip) {
    if (!fb || !data || w == 0 || h == 0) {
        return;
    }
    if (dstX >= SCREEN_W || dstY >= SCREEN_H) {
        return;
    }
    if (dstX + w > SCREEN_W)
        w = SCREEN_W - dstX;
    if (dstY + h > SCREEN_H)
        h = SCREEN_H - dstY;

    for (uint16_t row = 0; row < h; row++) {
        const uint16_t absY = dstY + row;
        const uint16_t *src = data + static_cast<size_t>(row) * w;
        uint16_t *dst = fb + static_cast<size_t>(absY) * SCREEN_W + dstX;

        if (!roundClip) {
            memcpy(dst, src, w * sizeof(uint16_t));
            continue;
        }

        // Island-local Y for the circle table.
        const int islandY = static_cast<int>(absY) - ISLAND_Y;
        if (islandY < 0 || islandY >= ISLAND_SIZE) {
            continue;
        }
        const int left = circleLeft[islandY];
        const int right = circleRight[islandY];
        if (left > right) {
            continue;
        }
        for (uint16_t col = 0; col < w; col++) {
            const int islandX = static_cast<int>(dstX + col) - ISLAND_X;
            if (islandX < left || islandX > right) {
                continue;
            }
            dst[col] = src[col];
        }
    }

    uint16_t *line0 = fb + static_cast<size_t>(dstY) * SCREEN_W + dstX;
    const size_t span = static_cast<size_t>(h) * SCREEN_W * sizeof(uint16_t);
    esp_cache_msync(line0, span, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

void P4DsiPanel::pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *data) {
    // LV_Helper passes x2+1 / y2+1 as width/height (exclusive end), matching LilyGo.
    const uint16_t w = width > x ? static_cast<uint16_t>(width - x) : 0;
    const uint16_t h = height > y ? static_cast<uint16_t>(height - y) : 0;
    blit(static_cast<uint16_t>(ISLAND_X + x), static_cast<uint16_t>(ISLAND_Y + y), w, h, data, true);
}

void P4DsiPanel::pushColorsSide(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *data) {
    const uint16_t w = width > x ? static_cast<uint16_t>(width - x) : 0;
    const uint16_t h = height > y ? static_cast<uint16_t>(height - y) : 0;
    blit(static_cast<uint16_t>(SIDE_PANEL_X + x), y, w, h, data, false);
}

bool P4DsiPanel::sampleTouch() {
    bool down = false;
    int16_t tx = 0, ty = 0;
    if (touchOk && touch.getPoint(&tx, &ty, 1) > 0) {
        down = true;
        if (ROTATE_180) {
            rawX = static_cast<int16_t>(SCREEN_W - 1 - tx);
            rawY = static_cast<int16_t>(SCREEN_H - 1 - ty);
        } else {
            rawX = tx;
            rawY = ty;
        }
    }
    touched = down;

    if (down && !wasTouched) {
        if (rawX >= ISLAND_X && rawX < ISLAND_X + ISLAND_SIZE && rawY >= ISLAND_Y && rawY < ISLAND_Y + ISLAND_SIZE) {
            touchOwner = TouchOwner::Island;
        } else if (rawX >= SIDE_PANEL_X && rawX < SCREEN_W && rawY >= 0 && rawY < SCREEN_H) {
            touchOwner = TouchOwner::Side;
        } else {
            touchOwner = TouchOwner::None;
        }
    }
    if (!down) {
        touchOwner = TouchOwner::None;
    }
    wasTouched = down;
    return down;
}

uint8_t P4DsiPanel::getPoint(int16_t *x, int16_t *y, uint8_t get_point) {
    (void)get_point;
    sampleTouch();
    if (!touched || touchOwner != TouchOwner::Island) {
        return 0;
    }
    *x = rawX - ISLAND_X;
    *y = rawY - ISLAND_Y;
    if (*x < 0)
        *x = 0;
    if (*y < 0)
        *y = 0;
    if (*x >= ISLAND_SIZE)
        *x = ISLAND_SIZE - 1;
    if (*y >= ISLAND_SIZE)
        *y = ISLAND_SIZE - 1;
    return 1;
}

} // namespace waveshare_esp32_p4_7b
