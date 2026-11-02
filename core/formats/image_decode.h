// image_decode.h - turns BMP, PNG and JPEG files into plain RGB pixels for the image viewer.
//
// The format is worked out from the file itself (its first bytes), not its name. Each decoder
// refuses what it doesn't support, with a short reason for the screen:
//   BMP   uncompressed 8-, 24- and 32-bit files
//   PNG   8-bit and 16-bit (taken to 8), greyscale, palette and colour, with or without alpha
//         (alpha is dropped); not interlaced; not 1, 2 or 4-bit colour
//   JPEG  baseline and progressive as far as LVGL's TJPGD decoder goes (no scaling)
// Images bigger than kMaxPixels are refused, so a huge photo can't exhaust the deck's memory.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace meshdeck {

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgb;   // width x height x 3 bytes, top row first
};

constexpr uint32_t kMaxImagePixels = 8 * 1024 * 1024;   // about 2900 x 2900

// Decodes the file in memory. False with a reason in error.
bool decodeImage(const uint8_t* data, size_t size, Image& out, std::string& error);
// The kind by its first bytes: "PNG", "JPEG", "BMP", or "" if none.
const char* imageKind(const uint8_t* data, size_t size);

} // namespace meshdeck
