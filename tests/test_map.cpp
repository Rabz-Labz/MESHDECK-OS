// test_map.cpp - OS grid maths, map packs, the place index and the position service.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "doctest.h"
#include "fake_radio.h"
#include "formats/map_pack.h"
#include "formats/osgrid.h"
#include "kernel/storage.h"
#include "services/gazetteer.h"
#include "services/mesh.h"
#include "services/position.h"
#include "test_util.h"

namespace og = meshdeck::osgrid;

TEST_CASE("osgrid: the Ordnance Survey worked example") {
    // OS guide: OSGB36 52 39' 27.2531" N, 1 43' 4.5177" E -> E 651409.903, N 313177.270.
    double e, n;
    og::projectOsgb36(52 + 39 / 60.0 + 27.2531 / 3600.0, 1 + 43 / 60.0 + 4.5177 / 3600.0, e, n);
    CHECK(fabs(e - 651409.903) < 0.01);
    CHECK(fabs(n - 313177.270) < 0.01);
    // The same point in WGS84 lands within ~5 m after the Helmert transform.
    og::toGrid(52 + 39 / 60.0 + 28.71 / 3600.0, 1 + 42 / 60.0 + 57.79 / 3600.0, e, n);
    CHECK(fabs(e - 651409.903) < 5);
    CHECK(fabs(n - 313177.270) < 5);
    CHECK(og::format(651409.903, 313177.270) == "TG 51409 13177");
    CHECK(og::format(651409.903, 313177.270, 6) == "TG 514 131");
}

TEST_CASE("osgrid: references parse, refuse nonsense, and round-trip") {
    double e, n;
    CHECK(og::parse("TG 51409 13177", e, n));
    CHECK(e == 651409);
    CHECK(og::parse("nt253404", e, n));   // Peebles, any case, no spaces
    CHECK(e == 325300);
    CHECK(n == 640400);
    CHECK(og::parse("325300 640400", e, n));
    CHECK_FALSE(og::parse("NI 123 456", e, n));    // no letter I
    CHECK_FALSE(og::parse("NT 1234 567", e, n));   // odd digit count
    CHECK_FALSE(og::parse("PEEBLES", e, n));
    double lat, lon, e2, n2;
    // Test points are generic (southern England), not real addresses: the maths is what is checked.
    REQUIRE(og::toLatLon(400000, 300000, lat, lon));
    og::toGrid(lat, lon, e2, n2);
    CHECK(hypot(e2 - 400000, n2 - 300000) < 0.5);
    CHECK(og::parseLatLon("51.10, -1.20", lat, lon));
    CHECK(lat == doctest::Approx(51.10));
    CHECK(lon == doctest::Approx(-1.20));
    CHECK(og::bearingDeg(51.0, -1.0, 51.1, -1.0) == doctest::Approx(0).epsilon(0.01));
}

TEST_CASE("map pack: PackBits and a small pack read back") {
    const uint8_t packed[] = {0xFD, 7, 0x02, 1, 2, 3, 0x80, 0xFF, 9};   // 4 x 7, literal 1 2 3, no-op, 2 x 9
    uint8_t out[9];
    CHECK(meshdeck::unpackBits(packed, sizeof packed, out, 9));
    CHECK(out[3] == 7);
    CHECK(out[6] == 3);
    CHECK(out[8] == 9);
    CHECK_FALSE(meshdeck::unpackBits(packed, 4, out, 9));

    // A one-level pack with one tile: every pixel 200 (PackBits runs of 128).
    std::vector<uint8_t> tile;
    for (int i = 0; i < 65536 / 128; i++) { tile.push_back(0x81); tile.push_back(200); }
    auto put = [](std::vector<uint8_t>& v, const void* p, size_t n) { v.insert(v.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
    std::vector<uint8_t> f;
    put(f, "BMAP", 4);
    uint16_t version = 1, count = 1, tileSize = 256, attrLen = 4;
    put(f, &version, 2);
    put(f, &count, 2);
    uint8_t nameLen = 4;
    put(f, &nameLen, 1);
    put(f, "TEST", 4);
    put(f, &attrLen, 2);
    put(f, "(C)X", 4);
    uint8_t level = 0, pad = 0;
    float mpp = 100;
    int32_t minTx = 10, minTy = 20, maxTx = 11, maxTy = 21;
    uint32_t entries = 1;
    put(f, &level, 1); put(f, &pad, 1); put(f, &tileSize, 2); put(f, &mpp, 4);
    put(f, &minTx, 4); put(f, &minTy, 4); put(f, &maxTx, 4); put(f, &maxTy, 4); put(f, &entries, 4);
    int16_t tx = 10, ty = 20;
    uint32_t offset = (uint32_t)f.size() + 12, length = (uint32_t)tile.size();
    put(f, &tx, 2); put(f, &ty, 2); put(f, &offset, 4); put(f, &length, 4);
    put(f, tile.data(), tile.size());
    std::string path = testutil::scratch("mappack") + "/test.map";
    FILE* fp = fopen(path.c_str(), "wb");
    REQUIRE(fp);
    fwrite(f.data(), 1, f.size(), fp);
    fclose(fp);

    meshdeck::MapPack pack;
    std::string err;
    REQUIRE(pack.open(path, err));
    CHECK(pack.name() == "TEST");
    REQUIRE(pack.levels().size() == 1);
    bool covered = false;
    const uint8_t* px = pack.tile(0, 10, 20, covered);
    REQUIRE(px);
    CHECK(covered);
    CHECK(px[0] == 200);
    CHECK(px[65535] == 200);
    CHECK(pack.tile(0, 11, 21, covered) == nullptr);   // inside the area, no tile: blank
    CHECK(covered);
    pack.tile(0, 50, 50, covered);
    CHECK_FALSE(covered);                               // outside the area: unknown
    CHECK(pack.tilesRead() == 1);
}

TEST_CASE("gazetteer: case-insensitive search, best matches first") {
    std::string path = testutil::scratch("gazetteer") + "/places.txt";
    REQUIRE(meshdeck::writeFileAtomic(path,
        "Berwick Law\tHILL\t355600\t684200\n"
        "Berwick-upon-Tweed\tTOWN\t399822\t653107\n"
        "North Berwick\tTOWN\t355400\t685300\n"
        "Peebles\tTOWN\t325371\t640462\n"
        "Peebles Hydro\tSETTLEMENT\t326000\t641000\n"
        "Upper Peebles\tHAMLET\t300000\t600000\n"));
    meshdeck::Gazetteer g;
    REQUIRE(g.load(path));
    CHECK(g.size() == 6);
    auto r = g.search("peebles");
    REQUIRE(r.size() == 3);
    CHECK(r[0].name == "Peebles");          // exact
    CHECK(r[1].name == "Peebles Hydro");    // starts with it
    CHECK(r[2].name == "Upper Peebles");    // a later word
    CHECK(r[0].easting == 325371);
    auto b = g.search("BERWICK");
    REQUIRE(b.size() == 3);
    CHECK(b[0].name == "Berwick-upon-Tweed");   // starts with it, and a town before a hill
    CHECK(b[1].name == "Berwick Law");
    CHECK(b[2].name == "North Berwick");
    CHECK(g.search("p").empty());               // too short
    CHECK(g.search("xyzzy").empty());
}

TEST_CASE("position: typed in, from a node, and kept across a restart") {
    struct P : meshdeck::Platform {
        FakeRadio radio;
        uint32_t ms = 1000;
        const char* name() const override { return "Test"; }
        uint32_t uptimeMs() const override { return ms; }
        bool radioPresent() const override { return radio.present; }
        int radioRead(uint8_t* b, int m) override { return radio.read(b, m); }
        bool radioWrite(const uint8_t* d, int l) override { return radio.write(d, l); }
        void randomBytes(uint8_t* o, size_t n) override { memset(o, 7, n); }
    } p;
    p.radio.chatter = false;
    meshdeck::MeshService mesh(p, [] { return (int64_t)1793542200; });
    mesh.begin("");
    for (int i = 0; i < 5; i++) { p.ms += 100; p.radio.tick(p.ms); mesh.tick(); }
    std::string path = testutil::scratch("position") + "/position.txt";
    {
        meshdeck::PositionService pos(p, mesh, [] { return (int64_t)1793542200; });
        pos.begin(path);
        CHECK_FALSE(pos.own().valid);
        CHECK(pos.sourceText() == "NOT SET");
        REQUIRE(pos.setManual(51.10, -1.20));
        CHECK(pos.own().valid);
        CHECK(pos.sourceText() == "TYPED IN");
    }
    {
        meshdeck::PositionService pos(p, mesh, [] { return (int64_t)1793542200 + 7200; });
        pos.begin(path);
        REQUIRE(pos.own().valid);
        CHECK(pos.own().lat == doctest::Approx(51.10));
        CHECK(pos.ageText(pos.own()) == "SET 2 H AGO");
        REQUIRE(pos.useNode(0x1A01));          // the fake Cardputer, which has a position
        CHECK(pos.own().valid);
        CHECK(pos.sourceText() == "TS01 GPS (VIA MESH)");
    }
    meshdeck::PositionService again(p, mesh, [] { return (int64_t)0; });
    again.begin(path);
    CHECK(again.source() == meshdeck::PositionService::Source::Node);
    CHECK(again.node() == 0x1A01);
}
