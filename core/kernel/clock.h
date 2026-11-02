// clock.h - calendar maths and UK local time, with no dependence on the C library's time zone.
//
// MESHDECK keeps time in UTC (seconds since 1970-01-01, as a 64-bit integer) and shows it as UK
// local time: GMT in winter, BST (UTC+1) from 01:00 UTC on the last Sunday of March to 01:00 UTC
// on the last Sunday of October. All functions here are pure: they work the same on the Tab5,
// the simulator and in tests.
#pragma once
#include <stdint.h>

namespace meshdeck {

// A date and time broken into fields. Month 1-12, day 1-31, hour 0-23.
struct CivilTime {
    int year, month, day, hour, minute, second;
};

int64_t utcFromCivil(const CivilTime& c);        // fields (taken as UTC) -> seconds since 1970
CivilTime civilFromUtc(int64_t utc);             // seconds since 1970 -> fields (UTC)
int weekday(int64_t utc);                        // 0 = Sunday ... 6 = Saturday

bool isBst(int64_t utc);                         // is British Summer Time in force at this moment?
int64_t localFromUtc(int64_t utc);               // shifts UTC by the UK offset in force (0 or 1 h)
int64_t utcFromLocal(const CivilTime& local);    // UK local fields -> UTC
const char* zoneName(int64_t utc);               // "GMT" or "BST"

// A clock reading is trusted only between 2025 and 2099. A Tab5 whose RTC lost power reads
// 2000-01-01, which this rejects, so the screen can say "time not set".
bool plausibleUtc(int64_t utc);

// Parses UK local time typed as "YYYY-MM-DD HH:MM" or "YYYY-MM-DD HH:MM:SS". Returns false for
// impossible dates (31 April, 29 February in a non-leap year) or implausible years.
bool parseLocal(const char* text, int64_t& utc);

// Writes UK local time as "YYYY-MM-DD HH:MM" (or with ":SS") into out.
void formatLocal(int64_t utc, char* out, int size, bool seconds = false);

} // namespace meshdeck
