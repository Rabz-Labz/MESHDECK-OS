// theme.h - the colour schemes. Screens use only these tokens, never their own colours.
//
// MESHDECK looks like a phosphor terminal: one bright colour on near-black. Each theme has:
//   bg       screen background
//   surface  panels, tiles and buttons
//   ink      main text and focus outlines
//   muted    secondary text (subtitles, hints)
//   line     borders
//   warn     the brightest tone, for alerts (always with a word, never colour alone)
#pragma once
#include "lvgl.h"

namespace meshdeck {

enum class ThemeId { Green, Amber, Red, Count };

struct Theme {
    const char* name;
    lv_color_t bg, surface, ink, muted, line, warn;
};

const Theme& theme(ThemeId id);
ThemeId nextTheme(ThemeId id);

} // namespace meshdeck
