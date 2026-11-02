// pin_pad.cpp - see pin_pad.h.
#include "ui/pin_pad.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {

void PinPad::build(Shell& shell, lv_obj_t* parent, int x, int y, int w, int h) {
    const int g = ui::kGap / 2;
    // The dots line on top, the pad below it: 4 rows of 3.
    lv_obj_t* box = ui::panel(shell, parent, x, y, w, 72);
    lv_obj_set_style_pad_all(box, 0, 0);
    dots_ = ui::label(box, "", fonts().title, shell.th().ink);
    lv_obj_center(dots_);
    int top = y + 72 + ui::kGap;
    int bw = (w - 2 * g) / 3, bh = (h - 72 - ui::kGap - 3 * g) / 4;
    static const char* labels[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "DEL", "0", "OK"};
    static const char codes[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '<', '0', '>'};
    for (int i = 0; i < 12; i++) {
        char c = codes[i];
        lv_obj_t* b = ui::button(shell, parent, labels[i], [this, c] { press(c); }, bw, bh);
        lv_obj_set_pos(b, x + (i % 3) * (bw + g), top + (i / 3) * (bh + g));
        if (i < 9 || i == 10) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), fonts().title, 0);
    }
    refresh();
}

bool PinPad::key(uint32_t k) {
    if (k >= '0' && k <= '9') press((char)k);
    else if (k == LV_KEY_BACKSPACE || k == LV_KEY_DEL) press('<');
    else if (k == LV_KEY_ENTER) press('>');
    else return false;
    return true;
}

void PinPad::press(char c) {
    if (c == '<') {
        if (!entry_.empty()) entry_.pop_back();
    } else if (c == '>') {
        std::string pin = entry_;
        if (onSubmit) onSubmit(pin);
        return;
    } else if (entry_.size() < kMaxDigits) {
        entry_ += c;
    }
    refresh();
}

void PinPad::clear() {
    entry_.clear();
    refresh();
}

void PinPad::refresh() {
    if (!dots_) return;
    std::string s;
    for (size_t i = 0; i < entry_.size(); i++) s += i ? " *" : "*";
    lv_label_set_text(dots_, s.empty() ? "-" : s.c_str());
}

} // namespace meshdeck
