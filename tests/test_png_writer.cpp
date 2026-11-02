// test_png_writer.cpp - the PNG writer's output is decoded here independently (chunk CRCs, zlib
// stored blocks, Adler-32) and compared pixel for pixel.
#include <stdio.h>
#include <string>
#include <vector>
#include "doctest.h"
#include "formats/png_writer.h"
#include "kernel/journal.h"
#include "kernel/storage.h"
#include "test_util.h"

using meshdeck::PngWriter;

static uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// Minimal reader for exactly what PngWriter produces. Returns the RGB pixels, or empty on any error.
static std::vector<uint8_t> decode(const std::string& file, uint32_t& w, uint32_t& h) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(file.data());
    size_t n = file.size(), at = 8;
    std::vector<uint8_t> idat, out;
    if (n < 8 || file.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return {};
    bool sawEnd = false;
    while (at + 12 <= n) {
        uint32_t len = be32(p + at);
        std::string type(file, at + 4, 4);
        if (at + 12 + len > n) return {};
        uint32_t crc = meshdeck::Journal::crc32(p + at + 4, 4 + len);
        if (crc != be32(p + at + 8 + len)) return {};
        const uint8_t* d = p + at + 8;
        if (type == "IHDR") {
            w = be32(d);
            h = be32(d + 4);
            if (d[8] != 8 || d[9] != 2) return {};
        } else if (type == "IDAT") {
            idat.insert(idat.end(), d, d + len);
        } else if (type == "IEND") {
            sawEnd = true;
        }
        at += 12 + len;
    }
    if (!sawEnd || idat.size() < 6 || idat[0] != 0x78) return {};
    // zlib: stored blocks only.
    size_t i = 2;
    std::vector<uint8_t> raw;
    for (bool last = false; !last;) {
        if (i + 5 > idat.size()) return {};
        last = idat[i] & 1;
        if ((idat[i] >> 1) & 3) return {};   // only stored blocks expected
        uint16_t len = idat[i + 1] | (idat[i + 2] << 8), nlen = idat[i + 3] | (idat[i + 4] << 8);
        if (uint16_t(~len) != nlen || i + 5 + len > idat.size()) return {};
        raw.insert(raw.end(), idat.begin() + i + 5, idat.begin() + i + 5 + len);
        i += 5 + len;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    if (i + 4 != idat.size() || be32(&idat[i]) != ((b << 16) | a)) return {};
    // Each row: filter byte 0, then RGB.
    if (raw.size() != h * (1 + w * 3)) return {};
    for (uint32_t y = 0; y < h; y++) {
        if (raw[y * (1 + w * 3)] != 0) return {};
        out.insert(out.end(), raw.begin() + y * (1 + w * 3) + 1, raw.begin() + (y + 1) * (1 + w * 3));
    }
    return out;
}

TEST_CASE("png: a small image round-trips exactly") {
    std::string path = testutil::scratch("png-small") + "/test.png";
    const uint32_t W = 5, H = 3;
    std::vector<uint8_t> pixels(W * H * 3);
    for (size_t i = 0; i < pixels.size(); i++) pixels[i] = uint8_t(i * 37 + 11);
    FILE* f = fopen(path.c_str(), "wb");
    REQUIRE(f);
    PngWriter png;
    REQUIRE(png.begin(f, W, H));
    for (uint32_t y = 0; y < H; y++) REQUIRE(png.row(&pixels[y * W * 3]));
    REQUIRE(png.finish());
    fclose(f);

    std::string file;
    REQUIRE(meshdeck::readFile(path, file, 1 << 20));
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> back = decode(file, w, h);
    CHECK(w == W);
    CHECK(h == H);
    CHECK(back == pixels);
}

TEST_CASE("png: refuses missing or extra rows") {
    std::string path = testutil::scratch("png-rows") + "/test.png";
    FILE* f = fopen(path.c_str(), "wb");
    REQUIRE(f);
    PngWriter png;
    uint8_t row[6] = {0};
    REQUIRE(png.begin(f, 2, 2));
    REQUIRE(png.row(row));
    CHECK_FALSE(png.finish());   // one row short
    fclose(f);
    PngWriter empty;
    CHECK_FALSE(empty.begin(nullptr, 2, 2));
    FILE* g = fopen(path.c_str(), "wb");
    CHECK_FALSE(empty.begin(g, 30000, 1));   // a row wouldn't fit one stored block
    fclose(g);
}
