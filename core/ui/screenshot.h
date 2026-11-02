// screenshot.h - saves what is on the screen as a PNG.
#pragma once
#include <string>
#include "lvgl.h"

namespace meshdeck {

// Renders obj (normally the active screen) and writes it to path as a PNG. Needs about
// width*height*2 bytes of free memory for the rendered copy. False if either step fails.
bool saveScreenshot(lv_obj_t* obj, const std::string& path);

// A new file name in folder for a screenshot taken at utc (UK local time):
// "2026-11-01_1430.png", then "2026-11-01_1430_2.png" and so on if taken in the same minute.
// Without a clock: "screenshot_<n>.png" with the next free number.
std::string screenshotName(const std::string& folder, int64_t utc);

} // namespace meshdeck
