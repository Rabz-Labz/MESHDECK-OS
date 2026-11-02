// png_writer.cpp - see png_writer.h.
#include "formats/png_writer.h"
#include "kernel/journal.h"   // Journal::crc32 is the same CRC-32 PNG uses

namespace meshdeck {

// Writes bytes and adds them to the chunk CRC (and, for image bytes, the zlib Adler-32).
bool PngWriter::put(const void* data, size_t len, bool imageData) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    crc_ = Journal::crc32(p, len, crc_);
    if (imageData) {
        for (size_t i = 0; i < len; i++) {
            adlerA_ = (adlerA_ + p[i]) % 65521;
            adlerB_ = (adlerB_ + adlerA_) % 65521;
        }
    }
    if (fwrite(p, 1, len, f_) != len) ok_ = false;
    return ok_;
}

bool PngWriter::putU32(uint32_t v) {
    uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    return put(b, 4, false);
}

// Writes a chunk's length (not covered by the CRC) and starts its CRC with the type.
static bool startChunk(FILE* f, uint32_t len, const char* type, uint32_t& crc) {
    uint8_t b[4] = {uint8_t(len >> 24), uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len)};
    crc = Journal::crc32(reinterpret_cast<const uint8_t*>(type), 4, 0);
    return fwrite(b, 1, 4, f) == 4 && fwrite(type, 1, 4, f) == 4;
}

static bool endChunk(FILE* f, uint32_t crc) {
    uint8_t b[4] = {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc)};
    return fwrite(b, 1, 4, f) == 4;
}

bool PngWriter::begin(FILE* f, uint32_t width, uint32_t height) {
    f_ = f;
    w_ = width;
    h_ = height;
    rows_ = 0;
    adlerA_ = 1;
    adlerB_ = 0;
    ok_ = f && width > 0 && height > 0 && 1 + width * 3 <= 65535;
    if (!ok_) return false;

    // Step 1: the 8-byte PNG signature.
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    ok_ = fwrite(sig, 1, 8, f) == 8;

    // Step 2: IHDR - size, 8 bits per channel, colour type 2 (RGB), no interlace.
    ok_ = ok_ && startChunk(f, 13, "IHDR", crc_);
    putU32(width);
    putU32(height);
    const uint8_t rest[5] = {8, 2, 0, 0, 0};
    put(rest, 5, false);
    ok_ = ok_ && endChunk(f, crc_);

    // Step 3: open the single IDAT. Its length is known: the zlib header (2), then per row a stored
    // block header (5) plus the filter byte and pixels, then the Adler-32 (4).
    uint32_t rowLen = 1 + width * 3;
    uint32_t idatLen = 2 + height * (5 + rowLen) + 4;
    ok_ = ok_ && startChunk(f, idatLen, "IDAT", crc_);
    const uint8_t zlibHeader[2] = {0x78, 0x01};   // deflate, 32 KB window, no preset dictionary
    put(zlibHeader, 2, false);
    return ok_;
}

bool PngWriter::row(const uint8_t* rgb) {
    if (!ok_ || rows_ >= h_) return ok_ = false;
    rows_++;
    // A stored block: final-block flag, then the length and its ones' complement (little-endian).
    uint16_t len = uint16_t(1 + w_ * 3), nlen = uint16_t(~len);
    const uint8_t header[5] = {uint8_t(rows_ == h_ ? 1 : 0), uint8_t(len), uint8_t(len >> 8), uint8_t(nlen),
                               uint8_t(nlen >> 8)};
    put(header, 5, false);
    const uint8_t filterNone = 0;
    put(&filterNone, 1, true);
    return put(rgb, w_ * 3, true);
}

bool PngWriter::finish() {
    if (!ok_ || rows_ != h_) return false;
    // Step 4: close the zlib stream with its Adler-32 and end the IDAT chunk.
    putU32((adlerB_ << 16) | adlerA_);
    ok_ = ok_ && endChunk(f_, crc_);
    // Step 5: the empty IEND chunk.
    ok_ = ok_ && startChunk(f_, 0, "IEND", crc_) && endChunk(f_, crc_);
    return ok_;
}

} // namespace meshdeck
