// test_journal.cpp - the power-loss guarantees of the journal (see core/kernel/journal.h).
#include <stdio.h>
#include <vector>
#include "doctest.h"
#include "kernel/journal.h"
#include "test_util.h"

using namespace meshdeck;

static long fileSize(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

TEST_CASE("records survive closing and reopening, in order, with their fields") {
    std::string path = testutil::scratch("journal-basic") + "/j.jnl";
    {
        Journal j;
        REQUIRE(j.open(path));
        CHECK(j.append(Journal::Note, "first", 1793000000, 10));
        CHECK(j.append(Journal::Boot, "second", 0, 20));
    }
    Journal j;
    REQUIRE(j.open(path));
    CHECK(j.records() == 2);
    CHECK(j.discardedBytes() == 0);
    std::vector<Journal::Record> got;
    j.forEach([&](const Journal::Record& r) { got.push_back(r); });
    REQUIRE(got.size() == 2);
    CHECK(got[0].text == "first");
    CHECK(got[0].utc == 1793000000);
    CHECK(got[0].seq == 1);
    CHECK(got[1].type == Journal::Boot);
    CHECK(got[1].seq == 2);
}

TEST_CASE("a torn last record (power cut mid-write) is dropped and new records continue") {
    std::string path = testutil::scratch("journal-torn") + "/j.jnl";
    {
        Journal j;
        REQUIRE(j.open(path));
        j.append(Journal::Note, "kept 1", 0, 0);
        j.append(Journal::Note, "kept 2", 0, 0);
        j.append(Journal::Note, "torn record text", 0, 0);
    }
    // Simulate the power cut: cut the file in the middle of the last record.
    long size = fileSize(path);
    std::vector<char> data(size);
    FILE* f = fopen(path.c_str(), "rb");
    fread(data.data(), 1, size, f);
    fclose(f);
    f = fopen(path.c_str(), "wb");
    fwrite(data.data(), 1, size - 7, f);
    fclose(f);

    Journal j;
    REQUIRE(j.open(path));
    CHECK(j.records() == 2);
    CHECK(j.discardedBytes() > 0);
    CHECK(j.append(Journal::Note, "after", 0, 0));
    Journal again;
    REQUIRE(again.open(path));
    std::vector<std::string> texts;
    uint32_t lastSeq = 0;
    again.forEach([&](const Journal::Record& r) { texts.push_back(r.text); lastSeq = r.seq; });
    CHECK(texts == std::vector<std::string>{"kept 1", "kept 2", "after"});
    CHECK(lastSeq == 3);
}

TEST_CASE("damage in the middle stops the scan there (later records are not trusted)") {
    std::string path = testutil::scratch("journal-mid") + "/j.jnl";
    {
        Journal j;
        REQUIRE(j.open(path));
        for (int i = 0; i < 5; i++) j.append(Journal::Note, "record " + std::to_string(i), 0, 0);
    }
    // Flip one byte inside the third record's text.
    FILE* f = fopen(path.c_str(), "r+b");
    long third = 2 * (long)(Journal::kHeader + 8) + (long)Journal::kHeader + 2;
    fseek(f, third, SEEK_SET);
    fputc('X', f);
    fclose(f);
    Journal j;
    REQUIRE(j.open(path));
    CHECK(j.records() == 2);
}

TEST_CASE("CRC-32 matches the standard check value") {
    const char* s = "123456789";
    CHECK(Journal::crc32((const uint8_t*)s, 9) == 0xCBF43926u);
}
