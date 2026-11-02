// clock_setter.cpp - see clock_setter.h.
#include "ui/clock_setter.h"
#include <stdio.h>
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {

static int daysInMonth(int year, int month) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : d[month - 1];
}

void ClockSetter::start(int64_t nowUtc) {
    time_ = nowUtc ? civilFromUtc(localFromUtc(nowUtc)) : CivilTime{2026, 1, 1, 12, 0, 0};
    time_.second = 0;
}

void ClockSetter::build(Shell& shell, lv_obj_t* parent, int y, int w) {
    const Theme& t = shell.th();
    static const char* names[] = {"DAY", "MONTH", "YEAR", "HOUR", "MIN"};
    const int colW = 200, g = ui::kGap, left = (w - 5 * colW - 4 * g) / 2, bh = ui::kTouchBig - 16;
    for (int i = 0; i < 5; i++) {
        int x = left + i * (colW + g);
        lv_obj_t* n = ui::label(parent, names[i], fonts().small, t.muted);
        lv_obj_set_pos(n, x + 4, y);
        lv_obj_t* up = ui::button(shell, parent, "+", [this, i] { adjust(i, +1); }, colW, bh);
        lv_obj_set_pos(up, x, y + 32);
        fields_[i] = ui::label(parent, "", fonts().title, t.ink);
        lv_obj_set_width(fields_[i], colW);
        lv_obj_set_style_text_align(fields_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(fields_[i], x, y + 32 + bh + 12);
        lv_obj_t* down = ui::button(shell, parent, "-", [this, i] { adjust(i, -1); }, colW, bh);
        lv_obj_set_pos(down, x, y + 32 + bh + 76);
    }
    show();
}

// Each column wraps round (minute 59 + 1 = 0). The day is kept valid for the month and year.
void ClockSetter::adjust(int field, int delta) {
    int* f[] = {&time_.day, &time_.month, &time_.year, &time_.hour, &time_.minute};
    static const int lo[] = {1, 1, 2025, 0, 0}, hi[] = {31, 12, 2099, 23, 59};
    int v = *f[field] + delta;
    if (field == 0) {
        int dim = daysInMonth(time_.year, time_.month);
        v = v < 1 ? dim : v > dim ? 1 : v;
    } else {
        v = v < lo[field] ? hi[field] : v > hi[field] ? lo[field] : v;
    }
    *f[field] = v;
    int dim = daysInMonth(time_.year, time_.month);   // 31 Jan -> Feb becomes 28/29 Feb
    if (time_.day > dim) time_.day = dim;
    show();
}

void ClockSetter::show() {
    int v[] = {time_.day, time_.month, time_.year, time_.hour, time_.minute};
    for (int i = 0; i < 5; i++) {
        if (!fields_[i]) continue;
        char buf[8];
        snprintf(buf, sizeof buf, i == 2 ? "%04d" : "%02d", v[i]);
        lv_label_set_text(fields_[i], buf);
    }
}

} // namespace meshdeck
