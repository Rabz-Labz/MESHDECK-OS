// fonts.cpp - see fonts.h. The fonts are created once, on first use, and never freed.
#include "ui/fonts.h"
#include <initializer_list>

namespace meshdeck {

const Fonts& fonts() {
    static const Fonts f = [] {
        // Kerning off: VT323 is monospaced, so kerning changes nothing, but looking it up for every
        // pair of letters in the TrueType tables made each label slow to measure and draw (over a
        // second to build the home screen on the Tab5). Glyphs are cached (256 per size).
        auto make = [](int px) {
            return lv_tiny_ttf_create_data_ex(kVt323Ttf, kVt323TtfSize, px, LV_FONT_KERNING_NONE, 256);
        };
        Fonts r;
        r.small = make(24);
        r.body = make(32);
        r.title = make(48);
        r.huge = make(112);
        // VT323 has no icon glyphs; LVGL's built-in font supplies them (the on-screen keyboard's
        // backspace, OK and arrow keys) whenever VT323 lacks a character.
        for (const lv_font_t* f : {r.small, r.body, r.title, r.huge})
            const_cast<lv_font_t*>(f)->fallback = &lv_font_montserrat_20;
        return r;
    }();
    return f;
}

} // namespace meshdeck
