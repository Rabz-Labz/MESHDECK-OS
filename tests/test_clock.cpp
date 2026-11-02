// test_clock.cpp - calendar maths and UK GMT/BST rules.
#include <string>
#include "doctest.h"
#include "kernel/clock.h"

using namespace meshdeck;

TEST_CASE("civil <-> UTC round trip, including leap days and before 1970") {
    CivilTime c = {2028, 2, 29, 23, 59, 58};
    int64_t t = utcFromCivil(c);
    CivilTime back = civilFromUtc(t);
    CHECK(back.year == 2028);
    CHECK(back.month == 2);
    CHECK(back.day == 29);
    CHECK(back.second == 58);
    CHECK(utcFromCivil({1970, 1, 1, 0, 0, 0}) == 0);
    CHECK(civilFromUtc(-1).year == 1969);
    CHECK(weekday(0) == 4);   // Thursday 1 January 1970
}

TEST_CASE("BST starts and ends at 01:00 UTC on the last Sundays of March and October") {
    // 2026: BST from Sunday 29 March 01:00 UTC to Sunday 25 October 01:00 UTC.
    CHECK_FALSE(isBst(utcFromCivil({2026, 3, 29, 0, 59, 59})));
    CHECK(isBst(utcFromCivil({2026, 3, 29, 1, 0, 0})));
    CHECK(isBst(utcFromCivil({2026, 10, 25, 0, 59, 59})));
    CHECK_FALSE(isBst(utcFromCivil({2026, 10, 25, 1, 0, 0})));
    CHECK(std::string(zoneName(utcFromCivil({2026, 7, 1, 12, 0, 0}))) == "BST");
    CHECK(localFromUtc(utcFromCivil({2026, 7, 1, 12, 0, 0})) == utcFromCivil({2026, 7, 1, 13, 0, 0}));
}

TEST_CASE("parsing typed UK local time") {
    int64_t t = 0;
    REQUIRE(parseLocal("2026-07-01 13:00", t));
    CHECK(t == utcFromCivil({2026, 7, 1, 12, 0, 0}));   // 13:00 BST = 12:00 UTC
    REQUIRE(parseLocal("2026-12-01 13:00:30", t));
    CHECK(t == utcFromCivil({2026, 12, 1, 13, 0, 30}));  // GMT
    CHECK_FALSE(parseLocal("2027-02-29 10:00", t));      // not a leap year
    CHECK_FALSE(parseLocal("2026-04-31 10:00", t));      // no 31 April
    CHECK_FALSE(parseLocal("2026-01-01 24:00", t));
    CHECK_FALSE(parseLocal("2000-01-01 00:00", t));      // an RTC that lost power: implausible
    CHECK_FALSE(parseLocal("tomorrow", t));
}

TEST_CASE("formatting") {
    char buf[32];
    formatLocal(utcFromCivil({2026, 7, 1, 12, 5, 9}), buf, sizeof buf, true);
    CHECK(std::string(buf) == "2026-07-01 13:05:09");
    formatLocal(utcFromCivil({2026, 12, 1, 12, 5, 9}), buf, sizeof buf);
    CHECK(std::string(buf) == "2026-12-01 12:05");
}
