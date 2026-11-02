// pin_pad.h - a PIN entry box with a big on-screen number pad.
//
// Touch: 1-9, 0, DEL and OK buttons (each at least 96 px). Keyboard: digits, BACKSPACE, ENTER.
// The digits typed are shown as dots. OK or ENTER calls onSubmit with what was typed.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "lvgl.h"

namespace meshdeck {

class Shell;

class PinPad {
public:
    static constexpr size_t kMaxDigits = 12;

    // Builds the dots line and the 3 x 4 pad in the given area (at least 330 x 450 px suits).
    void build(Shell& shell, lv_obj_t* parent, int x, int y, int w, int h);
    // Keyboard input. Returns true if the key was a digit, BACKSPACE or ENTER.
    bool key(uint32_t key);
    void clear();
    const std::string& entry() const { return entry_; }

    std::function<void(const std::string&)> onSubmit;

private:
    void press(char c);   // '0'-'9', '<' delete, '>' OK
    void refresh();

    std::string entry_;
    lv_obj_t* dots_ = nullptr;
};

} // namespace meshdeck
