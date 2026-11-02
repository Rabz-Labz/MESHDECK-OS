// power_monitor.h - turns raw battery readings into "87 %, about 6 h left".
//
// The platform is sampled about once a second (Shell::tick). Two things make raw readings
// unreliable, so this smooths them:
//   - the INA226 sometimes returns a wild voltage; the shown voltage is the median of the last 5;
//   - current jumps with every screen redraw and radio burst; time left uses the average current
//     over the last minute.
// Percent comes from the voltage of a 2S Li-ion pack at rest, so it is an estimate, not a fuel
// gauge: it reads high while charging and low under heavy load.
#pragma once
#include <stdint.h>

namespace meshdeck {

class PowerMonitor {
public:
    static constexpr int kAverageSamples = 60;   // one minute at one sample per second

    // One reading. Pass have* = false for values the platform couldn't measure.
    void sample(bool haveMv, int mv, bool haveMa, int ma, bool external);

    int millivolts() const;        // median of recent readings, -1 unknown
    int percent() const;           // 0-100, -1 unknown
    bool haveCurrent() const { return maCount_ > 0; }
    int averageMilliamps() const;  // out of the battery (positive = discharging), 0 if unknown
    bool external() const { return external_; }
    // Minutes of battery left at the recent average current, for a pack of capacityMah. -1 when
    // it can't be estimated: unknown percent or current, on external power, or a current too
    // small to mean anything (under 20 mA).
    int minutesLeft(int capacityMah) const;

    // Per-cell voltage (mV) to state of charge, from a typical Li-ion rest curve.
    static int percentFromCell(int cellMv);

private:
    int mv_[5] = {0};
    int mvCount_ = 0, mvNext_ = 0;
    int ma_[kAverageSamples] = {0};
    int maCount_ = 0, maNext_ = 0;
    bool external_ = false;
};

} // namespace meshdeck
