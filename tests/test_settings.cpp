// test_settings.cpp - typed values with defaults, saving, and damage tolerance.
#include "doctest.h"
#include "kernel/settings.h"
#include "kernel/storage.h"
#include "test_util.h"

using namespace meshdeck;

TEST_CASE("missing file: defaults, and nothing to save until something changes") {
    Settings s;
    CHECK_FALSE(s.load(testutil::scratch("settings-none") + "/settings.txt"));
    CHECK(s.getInt("display.brightness", 60) == 60);
    CHECK(s.getBool("display.lowlight", false) == false);
    CHECK_FALSE(s.dirty());
    s.setInt("display.brightness", 60);
    CHECK(s.dirty());
}

TEST_CASE("values survive save and load") {
    std::string path = testutil::scratch("settings-save") + "/settings.txt";
    {
        Settings s;
        s.load(path);
        s.setInt("display.brightness", 40);
        s.setBool("display.lowlight", true);
        s.set("theme", "AMBER");
        REQUIRE(s.save());
        CHECK_FALSE(s.dirty());
    }
    Settings s;
    REQUIRE(s.load(path));
    CHECK(s.getInt("display.brightness", 60) == 40);
    CHECK(s.getBool("display.lowlight") == true);
    CHECK(s.getString("theme") == "AMBER");
}

TEST_CASE("setting the same value again doesn't mark a change") {
    Settings s;
    s.load("");
    s.set("theme", "GREEN");
    s.save();   // no path: fails, stays dirty
    CHECK(s.dirty());
    Settings t;
    std::string path = testutil::scratch("settings-same") + "/settings.txt";
    t.load(path);
    t.set("theme", "GREEN");
    t.save();
    t.set("theme", "GREEN");
    CHECK_FALSE(t.dirty());
}

TEST_CASE("bad keys, line breaks and damaged lines are refused or skipped") {
    std::string path = testutil::scratch("settings-bad") + "/settings.txt";
    writeFileAtomic(path, "# comment\ngood=1\nno equals sign\nbad key=2\nnum=12abc\n\x01\x02garbage\n");
    Settings s;
    REQUIRE(s.load(path));
    CHECK(s.getInt("good") == 1);
    CHECK(s.getInt("num", 7) == 7);          // unreadable number -> default
    CHECK(s.size() == 2);                     // good, num
    CHECK_FALSE(s.set("bad key", "x"));
    CHECK_FALSE(s.set("ok", "two\nlines"));
}
