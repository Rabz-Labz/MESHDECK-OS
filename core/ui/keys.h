// keys.h - how key presses reach the shell.
//
// Every keyboard (the Tab5's own, the simulator's PC keyboard, the serial KEY command) reports a
// key code and a set of modifier bits. Key codes are LVGL's (LV_KEY_UP, LV_KEY_ENTER, ...) for
// special keys and plain ASCII for characters. Letters arrive lower-case when Ctrl is held, so
// shortcuts compare against 'p', 'n', 'l'.
#pragma once
#include <stdint.h>

namespace meshdeck {

enum KeyMod : uint8_t {
    kModNone = 0,
    kModCtrl = 1,
    kModShift = 2,
    kModAlt = 4,
};

} // namespace meshdeck
