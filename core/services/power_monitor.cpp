// power_monitor.cpp - see power_monitor.h.
#include "services/power_monitor.h"
#include <algorithm>

namespace meshdeck {

void PowerMonitor::sample(bool haveMv, int mv, bool haveMa, int ma, bool external) {
    external_ = external;
    if (haveMv) {
        mv_[mvNext_] = mv;
        mvNext_ = (mvNext_ + 1) % 5;
        if (mvCount_ < 5) mvCount_++;
    }
    if (haveMa) {
        ma_[maNext_] = ma;
        maNext_ = (maNext_ + 1) % kAverageSamples;
        if (maCount_ < kAverageSamples) maCount_++;
    }
}

int PowerMonitor::millivolts() const {
    if (!mvCount_) return -1;
    int s[5];
    std::copy(mv_, mv_ + mvCount_, s);
    std::sort(s, s + mvCount_);
    return s[mvCount_ / 2];
}

int PowerMonitor::percentFromCell(int c) {
    static const int cell[] = {3000, 3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
    static const int pct[] = {0, 3, 8, 16, 28, 40, 52, 65, 78, 90, 100};
    if (c <= cell[0]) return 0;
    for (int i = 1; i < 11; i++)
        if (c <= cell[i]) return pct[i - 1] + (pct[i] - pct[i - 1]) * (c - cell[i - 1]) / (cell[i] - cell[i - 1]);
    return 100;
}

int PowerMonitor::percent() const {
    int mv = millivolts();
    return mv < 0 ? -1 : percentFromCell(mv / 2);   // two cells in series
}

int PowerMonitor::averageMilliamps() const {
    if (!maCount_) return 0;
    long sum = 0;
    for (int i = 0; i < maCount_; i++) sum += ma_[i];
    return (int)(sum / maCount_);
}

int PowerMonitor::minutesLeft(int capacityMah) const {
    int pct = percent(), ma = averageMilliamps();
    if (pct < 0 || !haveCurrent() || external_ || ma < 20 || capacityMah <= 0) return -1;
    return (int)((long)capacityMah * pct / 100 * 60 / ma);
}

} // namespace meshdeck
