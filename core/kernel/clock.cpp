// clock.cpp - see clock.h. Calendar conversion uses Howard Hinnant's public-domain algorithms
// (http://howardhinnant.github.io/date_algorithms.html), which are exact for any year and need no
// tables or time-zone database.
#include "clock.h"
#include <stdio.h>

namespace meshdeck {

// Days since 1970-01-01 for a date in the proleptic Gregorian calendar.
// Steps: shift the year to start in March (so the leap day is the last day of the "year"), split
// into 400-year eras, count days within the era, then subtract the days from year 0 to 1970.
static int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                          // year of era [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;     // day of year [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;              // day of era
    return era * 146097 + (int64_t)doe - 719468;
}

// The inverse of daysFromCivil: days since 1970 -> year, month, day.
static void civilFromDays(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (m <= 2));
}

int64_t utcFromCivil(const CivilTime& c) {
    return daysFromCivil(c.year, c.month, c.day) * 86400 + c.hour * 3600 + c.minute * 60 + c.second;
}

CivilTime civilFromUtc(int64_t utc) {
    // Floor division, so times before 1970 also land on the right day.
    int64_t days = utc >= 0 ? utc / 86400 : (utc - 86399) / 86400;
    int64_t rem = utc - days * 86400;
    CivilTime c;
    civilFromDays(days, c.year, c.month, c.day);
    c.hour = (int)(rem / 3600);
    c.minute = (int)(rem / 60 % 60);
    c.second = (int)(rem % 60);
    return c;
}

int weekday(int64_t utc) {
    int64_t days = utc >= 0 ? utc / 86400 : (utc - 86399) / 86400;
    return (int)(((days % 7) + 11) % 7);   // 1970-01-01 was a Thursday (4)
}

// 01:00 UTC on the last Sunday of March (month 3) or October (month 10): the UK change-over times.
// Steps: take 01:00 on the 31st, then step back to the Sunday on or before it.
static int64_t lastSundayOneAm(int year, int month) {
    int64_t t = utcFromCivil({year, month, 31, 1, 0, 0});
    return t - (int64_t)weekday(t) * 86400;
}

bool isBst(int64_t utc) {
    int year = civilFromUtc(utc).year;
    return utc >= lastSundayOneAm(year, 3) && utc < lastSundayOneAm(year, 10);
}

int64_t localFromUtc(int64_t utc) { return utc + (isBst(utc) ? 3600 : 0); }
const char* zoneName(int64_t utc) { return isBst(utc) ? "BST" : "GMT"; }

// Local -> UTC: treat the fields as UTC, then subtract an hour if BST applied an hour earlier.
// (In the repeated hour each October the earlier, BST, reading is chosen.)
int64_t utcFromLocal(const CivilTime& local) {
    int64_t asUtc = utcFromCivil(local);
    return isBst(asUtc - 3600) ? asUtc - 3600 : asUtc;
}

bool plausibleUtc(int64_t utc) {
    return utc >= utcFromCivil({2025, 1, 1, 0, 0, 0}) && utc < utcFromCivil({2100, 1, 1, 0, 0, 0});
}

bool parseLocal(const char* text, int64_t& utc) {
    // Step 1: read the fields (seconds optional).
    CivilTime c = {0, 0, 0, 0, 0, 0};
    int n = sscanf(text, "%d-%d-%d %d:%d:%d", &c.year, &c.month, &c.day, &c.hour, &c.minute, &c.second);
    if (n != 5 && n != 6) return false;
    // Step 2: check ranges (29 February allowed for now).
    static const int monthDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (c.month < 1 || c.month > 12 || c.day < 1 || c.day > monthDays[c.month - 1]) return false;
    if (c.hour < 0 || c.hour > 23 || c.minute < 0 || c.minute > 59 || c.second < 0 || c.second > 59) return false;
    // Step 3: a round trip through the calendar rejects 29 February in non-leap years.
    if (civilFromUtc(utcFromCivil(c)).day != c.day) return false;
    // Step 4: convert from UK local time and check the year is believable.
    utc = utcFromLocal(c);
    return plausibleUtc(utc);
}

void formatLocal(int64_t utc, char* out, int size, bool seconds) {
    CivilTime c = civilFromUtc(localFromUtc(utc));
    if (seconds) snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d", c.year, c.month, c.day, c.hour, c.minute, c.second);
    else snprintf(out, size, "%04d-%02d-%02d %02d:%02d", c.year, c.month, c.day, c.hour, c.minute);
}

} // namespace meshdeck
