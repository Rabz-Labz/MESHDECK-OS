// png_writer.h - writes an RGB image as a PNG file, one row at a time.
//
// Used for screenshots. It streams straight to the file, so a full 1280x720 screen needs only one
// row of memory. The image data is stored uncompressed (zlib "stored" blocks, one per row): the
// file is larger (2.7 MB for a full screen) but the code is short, fast and needs no compression
// library, and every PNG viewer opens it. Format reference: https://www.w3.org/TR/png/
//
// Layout written: signature, IHDR (8-bit RGB), one IDAT whose length is known in advance, IEND.
#pragma once
#include <stdint.h>
#include <stdio.h>

namespace meshdeck {

class PngWriter {
public:
    // Starts the file. Width up to 21844 (so a row fits one stored block). False on a write error.
    bool begin(FILE* f, uint32_t width, uint32_t height);
    // Writes the next row: width * 3 bytes, R G B. Exactly `height` rows must follow begin().
    bool row(const uint8_t* rgb);
    // Writes the checksum and the end chunk. False if rows were missing or a write failed.
    bool finish();

private:
    bool put(const void* data, size_t len, bool imageData);
    bool putU32(uint32_t v);   // big-endian, part of the current chunk

    FILE* f_ = nullptr;
    uint32_t w_ = 0, h_ = 0, rows_ = 0;
    uint32_t crc_ = 0;                 // CRC of the chunk being written
    uint32_t adlerA_ = 1, adlerB_ = 0; // zlib checksum of the image data
    bool ok_ = false;
};

} // namespace meshdeck
