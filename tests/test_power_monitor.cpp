// test_power_monitor.cpp - battery percent, smoothing and time-left estimates.
#include <initializer_list>
#include "doctest.h"
#include "services/power_monitor.h"

using meshdeck::PowerMonitor;

TEST_CASE("power: per-cell voltage curve") {
    CHECK(PowerMonitor::percentFromCell(2900) == 0);
    CHECK(PowerMonitor::percentFromCell(3700) == 40);
    CHECK(PowerMonitor::percentFromCell(3750) == 46);   // halfway between 40 and 52
    CHECK(PowerMonitor::percentFromCell(4200) == 100);
    CHECK(PowerMonitor::percentFromCell(4300) == 100);
}

TEST_CASE("power: unknown until measured") {
    PowerMonitor pm;
    CHECK(pm.millivolts() == -1);
    CHECK(pm.percent() == -1);
    CHECK(pm.minutesLeft(2000) == -1);
    pm.sample(false, 0, false, 0, false);
    CHECK(pm.percent() == -1);
    CHECK_FALSE(pm.haveCurrent());
}

TEST_CASE("power: one wild voltage reading is ignored (median of five)") {
    PowerMonitor pm;
    for (int mv : {7400, 7400, 4300, 7400, 7400}) pm.sample(true, mv, false, 0, false);
    CHECK(pm.millivolts() == 7400);
    CHECK(pm.percent() == 40);   // 3700 mV per cell
}

TEST_CASE("power: time left uses the one-minute average current") {
    PowerMonitor pm;
    pm.sample(true, 7400, true, 100, false);
    pm.sample(true, 7400, true, 300, false);   // average 200 mA
    CHECK(pm.averageMilliamps() == 200);
    // 2000 mAh x 40 % = 800 mAh; at 200 mA that is 4 hours.
    CHECK(pm.minutesLeft(2000) == 240);
    // The average covers only the last 60 samples.
    for (int i = 0; i < PowerMonitor::kAverageSamples; i++) pm.sample(true, 7400, true, 400, false);
    CHECK(pm.averageMilliamps() == 400);
    CHECK(pm.minutesLeft(2000) == 120);
}

TEST_CASE("power: no estimate on external power or at tiny currents") {
    PowerMonitor pm;
    pm.sample(true, 7400, true, 200, true);
    CHECK(pm.external());
    CHECK(pm.minutesLeft(2000) == -1);
    PowerMonitor charging;
    charging.sample(true, 7400, true, -500, false);   // negative: current into the battery
    CHECK(charging.minutesLeft(2000) == -1);
    PowerMonitor idle;
    idle.sample(true, 7400, true, 5, false);
    CHECK(idle.minutesLeft(2000) == -1);
}
