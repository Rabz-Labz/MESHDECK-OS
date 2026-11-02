// credits.h - what SETTINGS > ABOUT > CREDITS shows: the author's logo (a GIF, still or animated)
// across the top and a short sentence below it.
//
// To change them:
//   - sentence: edit kCreditsText in core/ui/credits/credits_text.cpp;
//   - logo: run  python tools/embed_gif.py path/to/logo.gif
//     which rewrites core/ui/credits/credits_logo.cpp with the GIF's bytes. Keep the logo within
//     about 1200 x 220 px: LVGL holds one decoded frame at 4 bytes per pixel.
// With no logo embedded (size 0) the page shows an empty frame instead.
#pragma once

namespace meshdeck {

extern const unsigned char kCreditsLogoGif[];
extern const unsigned int kCreditsLogoGifSize;   // 0 = no logo yet
extern const char* const kCreditsText;

} // namespace meshdeck
