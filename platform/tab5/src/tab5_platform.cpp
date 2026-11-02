// tab5_platform.cpp - see tab5_platform.h. Thin wrappers over M5Unified's drivers.
#include "tab5_platform.h"
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_random.h>
#include "kernel/clock.h"
#include "sd_card.h"

namespace tab5 {

uint32_t Tab5Platform::uptimeMs() const { return millis(); }

bool Tab5Platform::freeHeapBytes(uint32_t& bytes) {
    bytes = ESP.getFreeHeap();
    return true;
}

// The RX8130 real-time clock is kept in UTC. After its backup supply runs out it restarts at
// 2000-01-01, which the core's plausibleUtc() rejects, so the screen says "time not set".
bool Tab5Platform::rtcRead(int64_t& utc) {
    if (!M5.Rtc.isEnabled()) return false;
    m5::rtc_datetime_t dt;
    if (!M5.Rtc.getDateTime(&dt)) return false;
    utc = meshdeck::utcFromCivil({dt.date.year, dt.date.month, dt.date.date, dt.time.hours, dt.time.minutes, dt.time.seconds});
    return true;
}

bool Tab5Platform::rtcWrite(int64_t utc) {
    if (!M5.Rtc.isEnabled()) return false;
    meshdeck::CivilTime c = meshdeck::civilFromUtc(utc);
    m5::rtc_datetime_t dt;
    dt.date.year = c.year;
    dt.date.month = c.month;
    dt.date.date = c.day;
    dt.date.weekDay = meshdeck::weekday(utc);
    dt.time.hours = c.hour;
    dt.time.minutes = c.minute;
    dt.time.seconds = c.second;
    M5.Rtc.setDateTime(dt);
    return true;
}

std::string Tab5Platform::dataRoot() const { return sdReady_ ? kSdRoot : ""; }

// The Tab5's INA226 measures the 2S battery pack. Readings outside the 2S range (seen around
// 4.3 V on USB power in 1.x) are treated as "not measured".
bool Tab5Platform::batteryMillivolts(int& mv) {
    int v = M5.Power.getBatteryVoltage();
    if (v < 6000 || v > 8800) return false;
    mv = v;
    return true;
}

// M5Unified reports battery current positive while charging; the core's convention is positive
// while discharging (current out of the battery), so the sign is flipped here. Verified on the
// Tab5: about -212 mA raw on battery, +600 to +750 mA while charging.
bool Tab5Platform::batteryMilliamps(int& ma) {
    ma = -(int)M5.Power.getBatteryCurrent();
    return true;
}

// M5.Power.isCharging() reported "charging" while running on battery in the V0 power test, so
// external power is judged from the current instead: more than 20 mA flowing into the battery.
// (On USB with a full battery the current is near zero and this says "battery"; the time-left
// estimate ignores currents that small anyway.)
bool Tab5Platform::externalPower() {
    int ma = 0;
    return batteryMilliamps(ma) && ma < -20;
}

// Private records live in the system NVS partition, namespace "meshdeck".
bool Tab5Platform::loadSecret(const char* name, std::string& data) {
    Preferences p;
    if (!p.begin("meshdeck", true)) return false;   // read-only; fails if never written
    size_t len = p.getBytesLength(name);
    bool ok = len > 0 && len <= 4096;
    if (ok) {
        data.resize(len);
        ok = p.getBytes(name, &data[0], len) == len;
    }
    p.end();
    return ok;
}

bool Tab5Platform::saveSecret(const char* name, const std::string& data) {
    Preferences p;
    if (!p.begin("meshdeck", false)) return false;
    bool ok = p.putBytes(name, data.data(), data.size()) == data.size();
    p.end();
    return ok;
}

// The ESP32-P4's hardware random number generator.
void Tab5Platform::randomBytes(uint8_t* out, size_t len) { esp_fill_random(out, len); }

// The panel's backlight PWM (via M5GFX) takes 0-255.
void Tab5Platform::setBacklight(int percent) { M5.Display.setBrightness(255 * percent / 100); }

void Tab5Platform::log(const char* line) { Serial.println(line); }

} // namespace tab5
