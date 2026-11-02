// clock_setter.h - set a date and time by touch: five columns (day, month, year, hour, minute),
// each with a big + above and - below its value. Used by first-start setup and SETTINGS > CLOCK.
#pragma once
#include "kernel/clock.h"
#include "lvgl.h"

namespace meshdeck {

class Shell;

class ClockSetter {
public:
    // Starts from the current UK time if the clock is set, else midday on 1 January 2026.
    void start(int64_t nowUtc);
    // Builds the columns in parent at (0, y), centred in width w.
    void build(Shell& shell, lv_obj_t* parent, int y, int w);
    // The chosen time as UTC (UK local time converted, GMT or BST).
    int64_t utc() const { return utcFromLocal(time_); }

private:
    void adjust(int field, int delta);
    void show();

    CivilTime time_{2026, 1, 1, 12, 0, 0};
    lv_obj_t* fields_[5] = {nullptr};
};

} // namespace meshdeck
