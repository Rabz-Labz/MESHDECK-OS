// display.cpp - see display.h.
#include "display.h"
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#include "driver/ppa.h"

namespace tab5 {
namespace {

// How the panel is laid out and how landscape maps onto it.
struct Layout {
    uint16_t* fb = nullptr;        // the panel's framebuffer (RGB565, panel orientation)
    size_t fbBytes = 0;
    int panelW = 0, panelH = 0;    // native: 720 x 1280
    int W = 0, H = 0;              // as MESHDECK sees it: 1280 x 720
    bool flipY = false, flipX = false, swap = false;   // M5GFX's mapping, in this order
};

Layout L;
// The P4's PPA rotates each band into the panel's framebuffer, which is fast, but on the Tab5 it
// hangs the device. 1 November 2026: TERMINAL froze while redrawing. 2 November 2026: DIAGNOSTICS
// froze within seconds every time, inside the blocking PPA call on a small label update; with every
// area aligned to 16 pixels (alignArea) it lasted 85 s and then froze again. On the CPU rotation
// through M5GFX the same screens ran for 15 minutes without a fault. Keep this false until the
// PPA fault is understood.
constexpr bool kUsePpa = false;
ppa_client_handle_t ppa = nullptr;
ppa_srm_rotation_angle_t ppaAngle = PPA_SRM_ROTATION_ANGLE_0;
bool ppaMirrorX = false, ppaMirrorY = false;
const char* path = "M5GFX";
uint32_t flushUs = 0;

// Where landscape pixel (x, y) of a w x h picture lands in the rotated picture, as M5GFX's
// Panel_FrameBufferBase does it: flip y, flip x, then swap the axes.
void mapPixel(int x, int y, int w, int h, int& px, int& py) {
    if (L.flipY) y = h - 1 - y;
    if (L.flipX) x = w - 1 - x;
    if (L.swap) { px = y; py = x; }
    else { px = x; py = y; }
}

bool readLayout() {
    if (M5.getBoard() != m5::board_t::board_M5Tab5 && M5.getBoard() != m5::board_t::board_M5Tab5X) return false;
    auto* panel = M5.Display.getPanel();
    if (!panel || !panel->getBus() || panel->getBus()->busType() != lgfx::bus_type_t::bus_dsi) return false;
    auto* dsi = static_cast<lgfx::Panel_DSI*>(panel);
    L.fb = static_cast<uint16_t*>(dsi->config_detail().buffer);
    L.panelW = panel->config().panel_width;
    L.panelH = panel->config().panel_height;
    L.fbBytes = (size_t)L.panelW * L.panelH * 2;
    // M5GFX's internal rotation (Panel_FrameBufferBase::setRotation) and what it does to pixels.
    uint8_t r = M5.Display.getRotation(), off = panel->config().offset_rotation;
    uint8_t ir = ((r + off) & 3) | ((r & 4) ^ (off & 4));
    L.flipY = ((1u << ir) & 0b10010110) != 0;
    L.flipX = (ir & 2) != 0;
    L.swap = (ir & 1) != 0;
    L.W = L.swap ? L.panelH : L.panelW;
    L.H = L.swap ? L.panelW : L.panelH;
    return L.fb && (L.fbBytes % 128) == 0;
}

// Puts an 8 x 4 pattern through every PPA rotation and mirror, and keeps the first combination
// that reproduces mapPixel exactly.
bool chooseRotation() {
    const int w = 8, h = 4;
    uint16_t* in = (uint16_t*)heap_caps_aligned_calloc(128, 1, 128, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    uint16_t* out = (uint16_t*)heap_caps_aligned_calloc(128, 1, 128, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    uint16_t expect[w * h];
    bool found = false;
    if (in && out) {
        int ow = L.swap ? h : w, oh = L.swap ? w : h;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                in[y * w + x] = (uint16_t)(0x1000 + y * w + x);
                int px, py;
                mapPixel(x, y, w, h, px, py);
                expect[py * ow + px] = in[y * w + x];
            }
        static const ppa_srm_rotation_angle_t angles[] = {PPA_SRM_ROTATION_ANGLE_0, PPA_SRM_ROTATION_ANGLE_90,
                                                          PPA_SRM_ROTATION_ANGLE_180, PPA_SRM_ROTATION_ANGLE_270};
        for (int a = 0; a < 4 && !found; a++)
            for (int m = 0; m < 4 && !found; m++) {
                bool turns = angles[a] == PPA_SRM_ROTATION_ANGLE_90 || angles[a] == PPA_SRM_ROTATION_ANGLE_270;
                if (turns != L.swap) continue;   // must give the right output shape
                memset(out, 0, 128);
                ppa_srm_oper_config_t op = {};
                op.in.buffer = in;
                op.in.pic_w = op.in.block_w = w;
                op.in.pic_h = op.in.block_h = h;
                op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
                op.out.buffer = out;
                op.out.buffer_size = 128;
                op.out.pic_w = ow;
                op.out.pic_h = oh;
                op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
                op.rotation_angle = angles[a];
                op.scale_x = op.scale_y = 1.0f;
                op.mirror_x = m & 1;
                op.mirror_y = m & 2;
                op.mode = PPA_TRANS_MODE_BLOCKING;
                if (ppa_do_scale_rotate_mirror(ppa, &op) != ESP_OK) continue;
                if (memcmp(out, expect, sizeof expect) == 0) {
                    found = true;
                    ppaAngle = angles[a];
                    ppaMirrorX = op.mirror_x;
                    ppaMirrorY = op.mirror_y;
                }
            }
    }
    heap_caps_free(in);
    heap_caps_free(out);
    return found;
}

// A finished band, rotated into the framebuffer by the PPA.
void flushPpa(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
    uint32_t t0 = micros();
    int x = area->x1, y = area->y1, w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    // The band's top-left corner in the panel's own orientation.
    int bx = x, by = y;
    if (L.flipY) by = L.H - (y + h);
    if (L.flipX) bx = L.W - (x + w);
    if (L.swap) std::swap(bx, by);
    ppa_srm_oper_config_t op = {};
    op.in.buffer = px;
    op.in.pic_w = op.in.block_w = w;
    op.in.pic_h = op.in.block_h = h;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer = L.fb;
    op.out.buffer_size = L.fbBytes;
    op.out.pic_w = L.panelW;
    op.out.pic_h = L.panelH;
    op.out.block_offset_x = bx;
    op.out.block_offset_y = by;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.rotation_angle = ppaAngle;
    op.scale_x = op.scale_y = 1.0f;
    op.mirror_x = ppaMirrorX;
    op.mirror_y = ppaMirrorY;
    op.mode = PPA_TRANS_MODE_BLOCKING;
    if (ppa_do_scale_rotate_mirror(ppa, &op) != ESP_OK)   // never leave a band undrawn
        M5.Display.pushImage(x, y, w, h, reinterpret_cast<const lgfx::rgb565_t*>(px));
    flushUs += micros() - t0;
    lv_display_flush_ready(disp);
}

// Widens every area LVGL redraws to a 16-pixel grid, so the PPA only ever gets aligned blocks
// (PPA path only). Without it a 381 x 134 block at x 26 hung the PPA's blocking call on its second
// refresh; with it the hang came later but still came (see kUsePpa). Kept for when the PPA is tried
// again. 1280 x 720 divides by 16, so the grid stays on the screen.
void alignArea(lv_event_t* e) {
    lv_area_t* a = static_cast<lv_area_t*>(lv_event_get_param(e));
    a->x1 &= ~15;
    a->y1 &= ~15;
    a->x2 |= 15;
    a->y2 |= 15;
}

// The old way: M5GFX rotates the band into the framebuffer on the CPU.
void flushM5gfx(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
    uint32_t t0 = micros();
    int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    M5.Display.pushImage(area->x1, area->y1, w, h, reinterpret_cast<const lgfx::rgb565_t*>(px));
    flushUs += micros() - t0;
    lv_display_flush_ready(disp);
}

} // namespace

lv_display_t* startDisplay() {
    // Step 1: how bands get to the panel.
    bool usePpa = false;
    if (!readLayout()) {
        path = "M5GFX (NO DSI FRAMEBUFFER)";
    } else {
        ppa_client_config_t cfg = {};
        cfg.oper_type = PPA_OPERATION_SRM;
        cfg.max_pending_trans_num = 1;
        if (ppa_register_client(&cfg, &ppa) != ESP_OK) path = "M5GFX (PPA UNAVAILABLE)";
        else if (!chooseRotation()) path = "M5GFX (PPA ROTATION SELF-TEST FAILED)";
        else if (kUsePpa) { path = "PPA"; usePpa = true; }
        else path = "M5GFX (CPU ROTATION)";   // see kUsePpa
    }

    // Step 2: LVGL's two band buffers, in fast internal RAM if there is room (40 lines each),
    // else in PSRAM (80 lines each). Aligned for the PPA's DMA.
    const int w = M5.Display.width(), h = M5.Display.height();
    int lines = 40;
    size_t bytes = (size_t)w * lines * 2;
    void* buf1 = heap_caps_aligned_alloc(128, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void* buf2 = buf1 ? heap_caps_aligned_alloc(128, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) : nullptr;
    if (!buf1 || !buf2) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        lines = 80;
        bytes = (size_t)w * lines * 2;
        buf1 = heap_caps_aligned_alloc(128, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        buf2 = heap_caps_aligned_alloc(128, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!buf1 || !buf2) return nullptr;

    lv_display_t* display = lv_display_create(w, h);
    lv_display_set_flush_cb(display, usePpa ? flushPpa : flushM5gfx);
    if (usePpa) lv_display_add_event_cb(display, alignArea, LV_EVENT_INVALIDATE_AREA, nullptr);
    lv_display_set_buffers(display, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    return display;
}

const char* displayPath() { return path; }

uint32_t takeFlushMicros() {
    uint32_t us = flushUs;
    flushUs = 0;
    return us;
}


} // namespace tab5
