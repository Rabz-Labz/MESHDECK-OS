// test_images.cpp - the image decoders: a BMP built by hand, a PNG from our own writer, and
// refusals for damaged, unknown and too-large files.
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "doctest.h"
#include "formats/image_decode.h"
#include "formats/png_writer.h"
#include "kernel/storage.h"
#include "test_util.h"

namespace {

void put16(std::string& s, uint16_t v) { s.append((const char*)&v, 2); }
void put32(std::string& s, uint32_t v) { s.append((const char*)&v, 4); }

// A 24-bit BMP, 2 x 2: top row red, green; bottom row blue, white (stored bottom row first, BGR).
std::string bmp2x2() {
    std::string s = "BM";
    put32(s, 54 + 16);       // file size: headers + two padded rows of 8 bytes
    put32(s, 0);
    put32(s, 54);            // pixel data offset
    put32(s, 40);            // BITMAPINFOHEADER
    put32(s, 2);             // width
    put32(s, 2);             // height (positive: bottom row first)
    put16(s, 1);             // planes
    put16(s, 24);            // bits per pixel
    put32(s, 0);             // no compression
    put32(s, 16);            // image size
    put32(s, 2835);
    put32(s, 2835);
    put32(s, 0);
    put32(s, 0);
    std::string rows;
    rows += std::string("\xFF\x00\x00\xFF\xFF\xFF\x00\x00", 8);   // bottom: blue, white (+ padding)
    rows += std::string("\x00\x00\xFF\x00\xFF\x00\x00\x00", 8);   // top: red, green (+ padding)
    return s + rows;
}

uint8_t* pixel(meshdeck::Image& img, uint32_t x, uint32_t y) { return &img.rgb[((size_t)y * img.width + x) * 3]; }

} // namespace

TEST_CASE("images: a 24-bit BMP decodes with its rows the right way up") {
    std::string data = bmp2x2();
    CHECK(std::string(meshdeck::imageKind((const uint8_t*)data.data(), data.size())) == "BMP");
    meshdeck::Image img;
    std::string err;
    REQUIRE(meshdeck::decodeImage((const uint8_t*)data.data(), data.size(), img, err));
    CHECK(img.width == 2);
    CHECK(img.height == 2);
    CHECK(pixel(img, 0, 0)[0] == 255);   // top-left red
    CHECK(pixel(img, 0, 0)[1] == 0);
    CHECK(pixel(img, 1, 0)[1] == 255);   // top-right green
    CHECK(pixel(img, 0, 1)[2] == 255);   // bottom-left blue
    CHECK(pixel(img, 1, 1)[0] == 255);   // bottom-right white
    CHECK(pixel(img, 1, 1)[2] == 255);
}

TEST_CASE("images: a PNG written by our own writer decodes back exactly") {
    std::string path = testutil::scratch("images-png") + "/t.png";
    const uint32_t W = 3, H = 2;
    std::vector<uint8_t> px = {10, 20, 30,  40, 50, 60,  70, 80, 90,
                               100, 110, 120, 130, 140, 150, 160, 170, 180};
    FILE* f = fopen(path.c_str(), "wb");
    REQUIRE(f);
    meshdeck::PngWriter png;
    REQUIRE(png.begin(f, W, H));
    for (uint32_t y = 0; y < H; y++) REQUIRE(png.row(&px[y * W * 3]));
    REQUIRE(png.finish());
    fclose(f);
    std::string data;
    REQUIRE(meshdeck::readFile(path, data, 1 << 20));
    meshdeck::Image img;
    std::string err;
    REQUIRE(meshdeck::decodeImage((const uint8_t*)data.data(), data.size(), img, err));
    CHECK(img.width == W);
    CHECK(img.height == H);
    CHECK(img.rgb == px);
}

TEST_CASE("images: a damaged JPEG, an unknown file and a huge image are refused with a reason") {
    meshdeck::Image img;
    std::string err;
    std::string junkJpeg = std::string("\xFF\xD8\xFF", 3) + "not really a jpeg at all";
    CHECK_FALSE(meshdeck::decodeImage((const uint8_t*)junkJpeg.data(), junkJpeg.size(), img, err));
    CHECK(err.find("JPEG") != std::string::npos);

    std::string text = "hello, this is a text file";
    CHECK_FALSE(meshdeck::decodeImage((const uint8_t*)text.data(), text.size(), img, err));
    CHECK(err == "NOT A PNG, JPEG OR BMP FILE");

    std::string big = bmp2x2();
    uint32_t huge = 5000;
    memcpy(&big[18], &huge, 4);   // width 5000
    memcpy(&big[22], &huge, 4);   // height 5000
    CHECK_FALSE(meshdeck::decodeImage((const uint8_t*)big.data(), big.size(), img, err));
    CHECK(err.find("TOO LARGE") != std::string::npos);
}
