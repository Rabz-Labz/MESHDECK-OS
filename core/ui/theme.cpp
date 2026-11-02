// theme.cpp - see theme.h. Colours carried over from MESHDECK 1.x.
#include "ui/theme.h"

namespace meshdeck {

const Theme& theme(ThemeId id) {
    static const Theme themes[] = {
        {"GREEN", lv_color_hex(0x030803), lv_color_hex(0x071207), lv_color_hex(0x33FF66), lv_color_hex(0x22B04A),
         lv_color_hex(0x145A24), lv_color_hex(0xB8FFC8)},
        {"AMBER", lv_color_hex(0x0A0700), lv_color_hex(0x140E00), lv_color_hex(0xFFB000), lv_color_hex(0xB88000),
         lv_color_hex(0x5A3E00), lv_color_hex(0xFFE2A0)},
        // Red uses the red channel only, so it never drifts towards pink and is kind to night vision.
        {"RED", lv_color_hex(0x070000), lv_color_hex(0x120000), lv_color_hex(0xE00000), lv_color_hex(0xB80000),
         lv_color_hex(0x4A0000), lv_color_hex(0xFF2A2A)},
    };
    int i = static_cast<int>(id);
    return themes[i >= 0 && i < static_cast<int>(ThemeId::Count) ? i : 0];
}

ThemeId nextTheme(ThemeId id) {
    return static_cast<ThemeId>((static_cast<int>(id) + 1) % static_cast<int>(ThemeId::Count));
}

} // namespace meshdeck
