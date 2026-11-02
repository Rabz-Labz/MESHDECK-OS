// screenshot.cpp - see screenshot.h.
#include "ui/screenshot.h"
#include <stdio.h>
#include <vector>
#include "formats/png_writer.h"
#include "kernel/clock.h"
#include "kernel/storage.h"

namespace meshdeck {

bool saveScreenshot(lv_obj_t* obj, const std::string& path) {
    // Step 1: have LVGL render the object into a fresh RGB565 buffer (the display's own buffers
    // are partial, so they can't be read back).
    lv_draw_buf_t* snap = lv_snapshot_take(obj, LV_COLOR_FORMAT_RGB565);
    if (!snap) return false;
    const uint32_t w = snap->header.w, h = snap->header.h, stride = snap->header.stride;

    // Step 2: stream it to the file row by row, expanding 5-6-5 bits to 8-8-8.
    bool ok = false;
    FILE* f = fopen(path.c_str(), "wb");
    if (f) {
        PngWriter png;
        std::vector<uint8_t> rgb(w * 3);
        ok = png.begin(f, w, h);
        for (uint32_t y = 0; ok && y < h; y++) {
            const uint16_t* px = reinterpret_cast<const uint16_t*>(snap->data + y * stride);
            for (uint32_t x = 0; x < w; x++) {
                uint16_t p = px[x];
                uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
                rgb[x * 3] = uint8_t((r << 3) | (r >> 2));
                rgb[x * 3 + 1] = uint8_t((g << 2) | (g >> 4));
                rgb[x * 3 + 2] = uint8_t((b << 3) | (b >> 2));
            }
            ok = png.row(rgb.data());
        }
        ok = ok && png.finish();
        ok = (fflush(f) == 0) && ok && syncFile(f);
        fclose(f);
        if (!ok) remove(path.c_str());   // never leave a broken image behind
    }
    lv_draw_buf_destroy(snap);
    return ok;
}

std::string screenshotName(const std::string& folder, int64_t utc) {
    char base[40];
    if (plausibleUtc(utc)) {
        CivilTime c = civilFromUtc(localFromUtc(utc));
        snprintf(base, sizeof base, "%04d-%02d-%02d_%02d%02d", c.year, c.month, c.day, c.hour, c.minute);
        std::string name = folder + "/" + base + ".png";
        for (int n = 2; fileExists(name) && n < 1000; n++) name = folder + "/" + base + "_" + std::to_string(n) + ".png";
        return name;
    }
    for (int n = 1; n < 100000; n++) {
        std::string name = folder + "/screenshot_" + std::to_string(n) + ".png";
        if (!fileExists(name)) return name;
    }
    return folder + "/screenshot.png";
}

} // namespace meshdeck
