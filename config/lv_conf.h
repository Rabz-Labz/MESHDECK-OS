/* lv_conf.h - LVGL settings shared by the Tab5 firmware, the simulator and the tests.
 * Anything not set here takes LVGL's default (third_party/lvgl/src/lv_conf_internal.h).
 * Every build defines LV_CONF_INCLUDE_SIMPLE and puts this folder on the include path. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16            /* RGB565: what the Tab5's display takes */

/* Memory: the C library allocator. On the Tab5, large blocks land in PSRAM. */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE         /* LVGL is only ever called from the main loop */
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 200

/* Fonts: the UI uses VT323, embedded as a TTF and drawn by Tiny TTF (core/ui/fonts.h).
 * Montserrat 20 is only LVGL's built-in fallback. */
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 256

#define LV_USE_LODEPNG 1             /* only its zlib inflate is used (book pages, V5) */
#define LV_USE_SNAPSHOT 1            /* screenshots (core/ui/screenshot.cpp) */
#define LV_USE_GIF 1                 /* animated logo on SETTINGS > ABOUT > CREDITS */
#define LV_USE_TJPGD 1               /* JPEG decoder for the image viewer (core/formats/image_decode.cpp) */
#define LV_USE_THEME_DEFAULT 1
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* The desktop simulator draws into an SDL window (platform/sim). */
#ifdef MESHDECK_CR_SDL
  #define LV_LOG_PRINTF 1
  #define LV_USE_SDL 1
  #define LV_SDL_INCLUDE_PATH <SDL.h>    /* SDL2's CMake package puts include/SDL2 on the path */
  #define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
  #define LV_SDL_BUF_COUNT 1
  #define LV_SDL_FULLSCREEN 0
  #define LV_SDL_DIRECT_EXIT 1
#endif

#endif /* LV_CONF_H */
