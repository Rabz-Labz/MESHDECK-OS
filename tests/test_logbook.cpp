// test_logbook.cpp - report forms and inventory, including lines written by MESHDECK 1.x.
#include <string>
#include <vector>
#include "doctest.h"
#include "formats/report.h"
#include "kernel/storage.h"
#include "services/inventory.h"
#include "test_util.h"

TEST_CASE("reports: a METHANE summary keeps the letters and skips empty fields") {
    const meshdeck::ReportForm* m = nullptr;
    for (const auto& f : meshdeck::reportForms())
        if (std::string(f.code) == "METHANE") m = &f;
    REQUIRE(m);
    std::vector<std::string> values = {"yes", "", "flood", "  river  \n rising", "", "", ""};
    std::string s = meshdeck::reportSummary(*m, values, "21:12");
    CHECK(s == "METHANE 21:12 M:YES T:FLOOD H:RIVER RISING");
}

TEST_CASE("reports: long summaries split into numbered parts within the limit") {
    std::string text = "SITREP 10:00 WHO:TD01 AT:NT 25 40 STATUS:OK NEEDS:WATER NEXT:22:00 NOTE:ALL WELL HERE AND CALM";
    auto parts = meshdeck::splitMessage(text, 40);
    REQUIRE(parts.size() >= 2);
    for (const auto& p : parts) CHECK(p.size() <= 40);
    CHECK(parts[0].find("(1/") != std::string::npos);
    CHECK(meshdeck::splitMessage("short", 40).size() == 1);
}

TEST_CASE("inventory: a 1.x inventory file loads, with expiry and tags") {
    std::string dir = testutil::scratch("inventory");
    std::string path = dir + "/inventory.txt";
    REQUIRE(meshdeck::writeFileAtomic(path,
        "I|TORCH|2|GRAB BAG|2026-11-10|WIND-UP\n"
        "I|WATER|6|STORE|2026-10-01|\n"
        "L|GRAB BAG|04AB12CD|1793542000\n"));
    meshdeck::Inventory inv;
    REQUIRE(inv.load(path));
    REQUIRE(inv.items().size() == 2);
    CHECK(inv.items()[0].name == "TORCH");
    CHECK(inv.items()[0].qty == 2);
    CHECK(inv.items()[0].note == "WIND-UP");
    const meshdeck::Inventory::Location* loc = inv.locationForTag("04AB12CD");
    REQUIRE(loc);
    CHECK(loc->name == "GRAB BAG");
    CHECK(loc->checkedUtc == 1793542000);
    const int64_t now = 1793542200;   // 2026-11-01 14:10 UTC
    CHECK(meshdeck::Inventory::expiryText("2026-11-10", now) == "9 DAYS LEFT");
    CHECK(meshdeck::Inventory::expiryText("2026-10-01", now) == "EXPIRED 31 DAYS AGO");
    CHECK(meshdeck::Inventory::expiryText("", now) == "NO DATE");
    CHECK(meshdeck::Inventory::expiryText("2026-02-30", now) == "BAD DATE");
    CHECK(inv.expiringCount(now, 30) == 2);
}

TEST_CASE("inventory: saves and loads back, and a tag belongs to one location") {
    std::string dir = testutil::scratch("inventory-save");
    std::string path = dir + "/inventory.txt";
    {
        meshdeck::Inventory inv;
        inv.load(path);
        meshdeck::Inventory::Item it;
        it.name = "RADIO";
        it.qty = 1;
        it.location = "CAR";
        inv.items().push_back(it);
        CHECK(inv.linkTag("CAR", "AAAA"));
        CHECK(inv.linkTag("HOUSE", "AAAA"));   // moves the tag
        CHECK(inv.save());
    }
    meshdeck::Inventory again;
    REQUIRE(again.load(path));
    CHECK(again.items().size() == 1);
    CHECK(again.locationForTag("AAAA")->name == "HOUSE");
    CHECK(again.location("CAR")->tag.empty());
    CHECK(again.addStarterKit("HOUSE") > 0);
    CHECK(again.addStarterKit("HOUSE") == 0);   // already there
}