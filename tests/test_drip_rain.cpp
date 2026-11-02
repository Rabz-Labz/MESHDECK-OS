// test_drip_rain.cpp - the boot splash's rain: the word forms, only the word's cells are lit, and the
// drips keep falling without jumping.
#include "doctest.h"
#include "ui/drip_rain.h"

TEST_CASE("boot rain: the drips light every cell of the word, and nothing else") {
    meshdeck::DripRain rain(91, 51, 2026);
    CHECK(rain.wordCells() == 165);           // the lit pixels of M E S H D E C K - O S, counted from the glyphs
    CHECK_FALSE(rain.done());
    for (int i = 0; i < 60 * 30 && !rain.done(); i++) rain.step(1.0 / 60);   // up to 30 seconds
    REQUIRE(rain.done());
    int lit = 0;
    for (int c = 0; c < rain.cols(); c++)
        for (int r = 0; r < rain.rows(); r++) {
            if (rain.lit(c, r)) {
                lit++;
                CHECK(rain.isWordCell(c, r));   // a lit cell is always a cell of the word
            }
        }
    CHECK(lit == rain.wordCells());
}

TEST_CASE("boot rain: a drip falls down its column, and never moves more than a step allows") {
    meshdeck::DripRain rain(91, 51, 7);
    double last = rain.headRow(10);
    CHECK(last < 0);                            // every drip starts above the screen
    bool moved = false;
    for (int i = 0; i < 60 * 10; i++) {
        rain.step(1.0 / 60);
        double now = rain.headRow(10);
        if (now >= 0 && last >= 0) {
            CHECK(now - last >= 0);             // it only goes down
            CHECK(now - last <= 28.0 / 60 * 1.0001 + 1e-9);   // at most 28 rows a second, at 1/60 s a step
            moved = true;
        }
        last = now;
    }
    CHECK(moved);
}

TEST_CASE("boot rain: a long pause is capped, so the drips don't jump through the word") {
    meshdeck::DripRain rain(91, 51, 3);
    for (int i = 0; i < 400 && rain.headRow(20) < 0; i++) rain.step(1.0 / 60);   // wait for a drip to start
    double before = rain.headRow(20);
    REQUIRE(before >= 0);
    rain.step(5.0);                             // a five-second stall counts as a tenth of a second
    CHECK(rain.headRow(20) - before <= 28 * 0.1 + 1e-9);
}

TEST_CASE("boot rain: once stopped, the drips run off the screen and no new ones start") {
    meshdeck::DripRain rain(91, 51, 11);
    for (int i = 0; i < 60 * 30 && !rain.done(); i++) rain.step(1.0 / 60);
    REQUIRE(rain.done());
    CHECK_FALSE(rain.drained());                // still raining until it is stopped
    rain.stop();
    for (int i = 0; i < 60 * 10 && !rain.drained(); i++) rain.step(1.0 / 60);   // 51 rows at 12 rows/s or more
    REQUIRE(rain.drained());
    for (int i = 0; i < 60 * 5; i++) rain.step(1.0 / 60);
    for (int c = 0; c < rain.cols(); c++) CHECK(rain.headRow(c) < 0);   // nothing starts again
    int lit = 0;
    for (int c = 0; c < rain.cols(); c++)
        for (int r = 0; r < rain.rows(); r++) lit += rain.lit(c, r) ? 1 : 0;
    CHECK(lit == rain.wordCells());             // the words stay lit
}
