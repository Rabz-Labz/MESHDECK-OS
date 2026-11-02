// fonts.h - the VT323 terminal font at the four sizes the UI uses.
//
// The TTF file is compiled in (fonts/vt323_ttf.cpp) and LVGL's Tiny TTF draws it at any size, so
// there are no pre-rendered bitmap fonts to regenerate. Call fonts() only after lv_init().
#pragma once
#include "lvgl.h"

namespace meshdeck {

extern const unsigned char kVt323Ttf[];
extern const unsigned int kVt323TtfSize;

struct Fonts {
    const lv_font_t* small;   // 24 px: status bar, hints, subtitles
    const lv_font_t* body;    // 32 px: body text, buttons
    const lv_font_t* title;   // 48 px: screen titles, tile names
    const lv_font_t* huge;    // 112 px: the clock
};

const Fonts& fonts();

} // namespace meshdeck
