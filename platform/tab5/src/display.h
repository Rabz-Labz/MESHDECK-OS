// display.h - LVGL's display on the Tab5's 5" panel, drawn as fast as the hardware allows.
//
// The panel is a 720 x 1280 portrait MIPI-DSI display whose picture lives in a framebuffer in
// PSRAM; MESHDECK runs it landscape (1280 x 720). LVGL draws the screen in bands; each finished
// band has to be rotated into the framebuffer. Doing that pixel by pixel on the CPU (M5GFX's
// pushImage) took 90 ms per full screen, so here the ESP32-P4's 2D engine (the PPA) rotates each
// band straight into the framebuffer by DMA, and LVGL draws into buffers in fast internal RAM.
//
// The PPA's rotate/mirror settings are chosen by a self-test at start-up: a small asymmetric
// pattern is put through every combination and the one that matches M5GFX's own landscape
// mapping is kept. If none matches or the PPA isn't available, bands go through M5GFX as before,
// so the picture can never come out wrong.
#pragma once
#include "lvgl.h"

namespace tab5 {

// Creates LVGL's display (call after lv_init and M5.begin with the rotation set).
lv_display_t* startDisplay();
// How bands reach the panel: "PPA" or "M5GFX", and why if it fell back (for STATUS).
const char* displayPath();
// Microseconds spent getting bands to the panel since the last call (for tuning logs).
uint32_t takeFlushMicros();

} // namespace tab5
