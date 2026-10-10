// Widescreen variant of the SDL simulator display (env display-sim-wide): a 1024x600
// window like the ESP32-P4-WIFI6-Touch-LCD-7B. The official UI keeps its own
// 480x480 LVGL display, masked round and centred in the left half; the right half
// is a second LVGL display owned by the side panel host. Replaces SdlDriver.cpp.
#include "SdlDriver.h"

#include <SDL.h>
#include <cstdlib>
#include <display/main.h>
#include <lvgl.h>
#include <vector>
#include <widescreen/Layout.h>
#include <widescreen/side_panels/SidePanelHost.h>

using namespace widescreen;

SdlDriver *SdlDriver::instance = nullptr;

namespace {

enum class TouchOwner { None, Island, Panel };

struct Surface {
    SDL_Texture *texture = nullptr;
    lv_disp_draw_buf_t drawBuf{};
    lv_disp_drv_t dispDrv{};
    lv_indev_drv_t indevDrv{};
    lv_color_t *buf = nullptr;
    SDL_Rect rect{};
    TouchOwner owner = TouchOwner::None;
};

SDL_Window *s_window = nullptr;
SDL_Renderer *s_renderer = nullptr;
SDL_Texture *s_mask = nullptr; // black outside the island circle, transparent inside
Surface s_island;
Surface s_panel;

int s_mouseX = 0;
int s_mouseY = 0;
bool s_mousePressed = false;
TouchOwner s_touchOwner = TouchOwner::None;
bool s_quit = false;

void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    auto *surface = static_cast<Surface *>(drv->user_data);
    SDL_Rect r{area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1};
    SDL_UpdateTexture(surface->texture, &r, color_p, r.w * (int)sizeof(lv_color_t));
    lv_disp_flush_ready(drv);
}

// A touch belongs to the surface it started on, so a drag that leaves the island
// does not leak into the side panel (and vice versa).
void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    auto *surface = static_cast<Surface *>(drv->user_data);
    const int x = s_mouseX - surface->rect.x;
    const int y = s_mouseY - surface->rect.y;
    data->point.x = LV_CLAMP(0, x, surface->rect.w - 1);
    data->point.y = LV_CLAMP(0, y, surface->rect.h - 1);
    data->state = s_mousePressed && s_touchOwner == surface->owner ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

bool inside(const SDL_Rect &r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

lv_disp_t *registerSurface(Surface &surface, TouchOwner owner, SDL_Rect rect) {
    surface.owner = owner;
    surface.rect = rect;
    surface.texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, rect.w, rect.h);
    surface.buf = (lv_color_t *)malloc(rect.w * rect.h * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&surface.drawBuf, surface.buf, nullptr, rect.w * rect.h);

    lv_disp_drv_init(&surface.dispDrv);
    surface.dispDrv.hor_res = rect.w;
    surface.dispDrv.ver_res = rect.h;
    surface.dispDrv.flush_cb = disp_flush;
    surface.dispDrv.draw_buf = &surface.drawBuf;
    surface.dispDrv.user_data = &surface;
    lv_disp_t *disp = lv_disp_drv_register(&surface.dispDrv);

    lv_indev_drv_init(&surface.indevDrv);
    surface.indevDrv.type = LV_INDEV_TYPE_POINTER;
    surface.indevDrv.read_cb = touch_read;
    surface.indevDrv.disp = disp;
    surface.indevDrv.user_data = &surface;
    lv_indev_drv_register(&surface.indevDrv);
    return disp;
}

void buildIslandMask() {
    s_mask = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ISLAND_SIZE, ISLAND_SIZE);
    SDL_SetTextureBlendMode(s_mask, SDL_BLENDMODE_BLEND);
    std::vector<uint32_t> px(ISLAND_SIZE * ISLAND_SIZE);
    const double c = ISLAND_SIZE / 2.0;
    for (int y = 0; y < ISLAND_SIZE; y++) {
        for (int x = 0; x < ISLAND_SIZE; x++) {
            const double dx = x + 0.5 - c, dy = y + 0.5 - c;
            px[y * ISLAND_SIZE + x] = (dx * dx + dy * dy <= c * c) ? 0x00000000u : 0xFF000000u;
        }
    }
    SDL_UpdateTexture(s_mask, nullptr, px.data(), ISLAND_SIZE * (int)sizeof(uint32_t));
}

void renderFrame() {
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 0xFF);
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_island.texture, nullptr, &s_island.rect);
    SDL_RenderCopy(s_renderer, s_mask, nullptr, &s_island.rect);
    SDL_RenderCopy(s_renderer, s_panel.texture, nullptr, &s_panel.rect);
}

} // namespace

void SdlDriver::init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }
    s_window = SDL_CreateWindow("GaggiMate Simulator (widescreen)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W,
                                SCREEN_H, SDL_WINDOW_SHOWN);
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    buildIslandMask();

    lv_init();
    // The island registers first so it stays the default display the official UI draws on.
    registerSurface(s_island, TouchOwner::Island, SDL_Rect{ISLAND_X, ISLAND_Y, ISLAND_SIZE, ISLAND_SIZE});
    lv_disp_t *panelDisp = registerSurface(s_panel, TouchOwner::Panel, SDL_Rect{SIDE_PANEL_X, 0, SIDE_PANEL_W, SIDE_PANEL_H});
    SidePanelHost::instance().begin(panelDisp);
}

void SdlDriver::pumpAndRender() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            s_quit = true;
            break;
        case SDL_MOUSEMOTION:
            s_mouseX = e.motion.x;
            s_mouseY = e.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                s_mousePressed = true;
                s_mouseX = e.button.x;
                s_mouseY = e.button.y;
                s_touchOwner = inside(s_island.rect, s_mouseX, s_mouseY)  ? TouchOwner::Island
                               : inside(s_panel.rect, s_mouseX, s_mouseY) ? TouchOwner::Panel
                                                                          : TouchOwner::None;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT)
                s_mousePressed = false;
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: // keys 1/2 act as the brew/steam inputs of the controller board
            if (e.key.repeat == 0 && (e.key.keysym.sym == SDLK_1 || e.key.keysym.sym == SDLK_2))
                controller.getClientController()->simulateButton(e.key.keysym.sym == SDLK_1 ? 0 : 1, e.type == SDL_KEYDOWN);
            break;
        default:
            break;
        }
    }
    renderFrame();
    SDL_RenderPresent(s_renderer);
}

bool SdlDriver::shouldQuit() const { return s_quit; }

void SdlDriver::screenshot(const char *path) {
    renderFrame();
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 16, SDL_PIXELFORMAT_RGB565);
    if (surface) {
        SDL_RenderReadPixels(s_renderer, nullptr, SDL_PIXELFORMAT_RGB565, surface->pixels, surface->pitch);
        SDL_SaveBMP(surface, path);
        SDL_FreeSurface(surface);
    }
    SDL_RenderPresent(s_renderer);
}
