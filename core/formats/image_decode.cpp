// image_decode.cpp - see image_decode.h.
//
// PNG decompression uses LVGL's zlib (lodepng_zlib_decompress), the same routine the book reader
// uses on the device. JPEG uses LVGL's TJPGD decoder. Both live in the UI library, which is why
// this file is built there and not in the portable core.
#include "formats/image_decode.h"
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#define LODEPNG_NO_COMPILE_CPP   // only the C API (its C++ wrappers clash inside LVGL's extern "C")
#include "src/libs/lodepng/lodepng.h"
#include "src/libs/tjpgd/tjpgd.h"

namespace meshdeck {
namespace {

uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool sizeOk(uint32_t w, uint32_t h, std::string& error) {
    if (!w || !h) { error = "IMAGE HAS NO SIZE"; return false; }
    if ((uint64_t)w * h > kMaxImagePixels) { error = "IMAGE TOO LARGE FOR THE DECK (ABOUT 8 MEGAPIXELS MAX)"; return false; }
    return true;
}

// ---------------------------------------------------------------- BMP

bool decodeBmp(const uint8_t* d, size_t n, Image& out, std::string& error) {
    if (n < 54) { error = "BMP HEADER DAMAGED"; return false; }
    uint32_t dataAt = le32(d + 10), dibSize = le32(d + 14);
    if (dibSize < 40 || 14 + dibSize > n) { error = "BMP HEADER NOT SUPPORTED"; return false; }
    int32_t w = (int32_t)le32(d + 18), h = (int32_t)le32(d + 22);
    uint16_t bpp = le16(d + 28);
    uint32_t compression = le32(d + 30);
    if (w <= 0 || h == 0) { error = "BMP SIZE NOT SUPPORTED"; return false; }
    bool topDown = h < 0;
    uint32_t uw = (uint32_t)w, uh = topDown ? (uint32_t)-h : (uint32_t)h;
    if (!sizeOk(uw, uh, error)) return false;
    if (compression != 0 && compression != 3) { error = "COMPRESSED BMP NOT SUPPORTED"; return false; }
    if (bpp != 8 && bpp != 24 && bpp != 32) { error = "BMP DEPTH NOT SUPPORTED (8, 24 OR 32 BIT ONLY)"; return false; }

    // An 8-bit file has a palette of 4-byte entries (B, G, R, 0) after the headers.
    const uint8_t* palette = d + 14 + dibSize;
    uint32_t paletteCount = bpp == 8 ? (le32(d + 46) ? le32(d + 46) : 256) : 0;
    if (bpp == 8 && 14 + dibSize + paletteCount * 4 > n) { error = "BMP PALETTE DAMAGED"; return false; }

    uint32_t stride = ((uw * bpp + 31) / 32) * 4;
    if (dataAt + (uint64_t)stride * uh > n) { error = "BMP PIXELS ARE INCOMPLETE"; return false; }
    out.width = uw;
    out.height = uh;
    out.rgb.assign((size_t)uw * uh * 3, 0);
    for (uint32_t y = 0; y < uh; y++) {
        const uint8_t* row = d + dataAt + (size_t)stride * y;
        uint32_t dstRow = topDown ? y : uh - 1 - y;   // BMP files are usually bottom row first
        uint8_t* dst = out.rgb.data() + (size_t)dstRow * uw * 3;
        for (uint32_t x = 0; x < uw; x++) {
            uint8_t r, g, b;
            if (bpp == 8) {
                uint8_t idx = row[x];
                if (idx >= paletteCount) idx = 0;
                b = palette[idx * 4];
                g = palette[idx * 4 + 1];
                r = palette[idx * 4 + 2];
            } else {
                const uint8_t* px = row + x * (bpp / 8);
                b = px[0];
                g = px[1];
                r = px[2];
            }
            dst[x * 3] = r;
            dst[x * 3 + 1] = g;
            dst[x * 3 + 2] = b;
        }
    }
    return true;
}

// ---------------------------------------------------------------- PNG

// Undoes the per-line filters (None, Sub, Up, Average, Paeth) of one image in place. `bpp` is the
// filter byte distance in bytes (at least 1). Returns false on an unknown filter type.
bool unfilter(uint8_t* data, uint32_t rows, uint32_t rowBytes, uint32_t bpp) {
    std::vector<uint8_t> prev(rowBytes, 0);
    for (uint32_t y = 0; y < rows; y++) {
        uint8_t* line = data + (size_t)y * (rowBytes + 1);
        uint8_t type = line[0];
        uint8_t* cur = line + 1;
        if (type > 4) return false;
        for (uint32_t i = 0; i < rowBytes; i++) {
            uint8_t a = i >= bpp ? cur[i - bpp] : 0;
            uint8_t b = prev[i];
            uint8_t c = i >= bpp ? prev[i - bpp] : 0;
            uint8_t add = 0;
            switch (type) {
            case 0: add = 0; break;
            case 1: add = a; break;
            case 2: add = b; break;
            case 3: add = (uint8_t)((a + b) / 2); break;
            case 4: {
                int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                add = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            }
            cur[i] = (uint8_t)(cur[i] + add);
        }
        memcpy(prev.data(), cur, rowBytes);
    }
    return true;
}

bool decodePng(const uint8_t* d, size_t n, Image& out, std::string& error) {
    if (n < 33) { error = "PNG HEADER DAMAGED"; return false; }
    uint32_t w = 0, h = 0;
    uint8_t depth = 0, colorType = 0, interlace = 0;
    std::vector<uint8_t> idat, palette;
    size_t at = 8;
    bool sawEnd = false;
    while (at + 12 <= n) {
        uint32_t len = be32(d + at);
        if (at + 12 + len > n) break;
        const char* type = (const char*)(d + at + 4);
        const uint8_t* body = d + at + 8;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(body);
            h = be32(body + 4);
            depth = body[8];
            colorType = body[9];
            interlace = body[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            palette.assign(body, body + len);
        } else if (!memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), body, body + len);
        } else if (!memcmp(type, "IEND", 4)) {
            sawEnd = true;
            break;
        }
        at += 12 + len;
    }
    if (!w || !h) { error = "PNG HEADER DAMAGED"; return false; }
    if (interlace) { error = "INTERLACED PNG NOT SUPPORTED"; return false; }
    if (!sizeOk(w, h, error)) return false;
    int channels;
    switch (colorType) {
    case 0: channels = 1; break;   // grey
    case 2: channels = 3; break;   // colour
    case 3: channels = 1; break;   // palette
    case 4: channels = 2; break;   // grey + alpha
    case 6: channels = 4; break;   // colour + alpha
    default: error = "PNG COLOUR TYPE NOT SUPPORTED"; return false;
    }
    if (depth != 8 && depth != 16 && !((depth == 1 || depth == 2 || depth == 4) && (colorType == 0 || colorType == 3))) {
        error = "PNG BIT DEPTH NOT SUPPORTED";
        return false;
    }
    if (colorType == 3 && palette.empty()) { error = "PNG PALETTE MISSING"; return false; }
    if (idat.size() < 2) { error = "PNG DATA MISSING"; return false; }

    // Step 1: decompress the image data (zlib, as in every PNG).
    unsigned char* raw = nullptr;
    size_t rawSize = 0;
    unsigned e = lodepng_zlib_decompress(&raw, &rawSize, idat.data(), idat.size(), &lodepng_default_decompress_settings);
    uint32_t bitsPerPixel = (uint32_t)channels * depth;
    uint32_t rowBytes = (w * bitsPerPixel + 7) / 8;
    size_t expect = (size_t)h * (rowBytes + 1);
    if (e || !raw || rawSize < expect) {
        if (raw) lv_free(raw);
        error = "PNG DATA DAMAGED";
        return false;
    }
    // Step 2: undo the filters. The filter distance is one whole pixel, at least one byte.
    uint32_t filterBpp = bitsPerPixel < 8 ? 1 : bitsPerPixel / 8;
    if (!unfilter(raw, h, rowBytes, filterBpp)) {
        lv_free(raw);
        error = "PNG FILTER DAMAGED";
        return false;
    }
    // Step 3: take each pixel's colour (grey, palette or RGB; alpha dropped) to 8-bit RGB.
    out.width = w;
    out.height = h;
    out.rgb.assign((size_t)w * h * 3, 0);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* line = raw + (size_t)y * (rowBytes + 1) + 1;
        uint8_t* dst = out.rgb.data() + (size_t)y * w * 3;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t rgb[3] = {0, 0, 0};
            if (depth < 8) {
                uint32_t bit = x * depth;
                uint8_t v = (line[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1);
                if (colorType == 3) {
                    if ((size_t)v * 3 + 2 < palette.size()) memcpy(rgb, &palette[v * 3], 3);
                } else {
                    uint8_t g = (uint8_t)(v * 255 / ((1 << depth) - 1));
                    rgb[0] = rgb[1] = rgb[2] = g;
                }
            } else {
                size_t step = (size_t)channels * (depth / 8);
                const uint8_t* px = line + x * step;   // 16-bit samples: the high byte is kept
                if (colorType == 3) {
                    uint8_t idx = px[0];
                    if ((size_t)idx * 3 + 2 < palette.size()) memcpy(rgb, &palette[idx * 3], 3);
                } else if (channels <= 2) {
                    rgb[0] = rgb[1] = rgb[2] = px[0];
                } else {
                    rgb[0] = px[0];
                    rgb[1] = px[1];
                    rgb[2] = px[2];
                }
            }
            dst[x * 3] = rgb[0];
            dst[x * 3 + 1] = rgb[1];
            dst[x * 3 + 2] = rgb[2];
        }
    }
    lv_free(raw);
    (void)sawEnd;
    return true;
}

// ---------------------------------------------------------------- JPEG

struct JpegSource {
    const uint8_t* data;
    size_t size, pos;
    Image* out;
};

size_t jpegInput(JDEC* jd, uint8_t* buf, size_t len) {
    JpegSource* s = static_cast<JpegSource*>(jd->device);
    size_t left = s->size - s->pos;
    if (len > left) len = left;
    if (buf) memcpy(buf, s->data + s->pos, len);
    s->pos += len;
    return len;
}

int jpegOutput(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegSource* s = static_cast<JpegSource*>(jd->device);
    Image* img = s->out;
    const uint8_t* src = static_cast<const uint8_t*>(bitmap);   // RGB888, the block's own width
    uint32_t bw = rect->right - rect->left + 1;
    for (uint32_t y = rect->top; y <= rect->bottom && y < img->height; y++) {
        const uint8_t* row = src + (size_t)(y - rect->top) * bw * 3;
        uint8_t* dst = img->rgb.data() + ((size_t)y * img->width + rect->left) * 3;
        for (uint32_t x = 0; x < bw && rect->left + x < img->width; x++) {
            dst[x * 3] = row[x * 3];
            dst[x * 3 + 1] = row[x * 3 + 1];
            dst[x * 3 + 2] = row[x * 3 + 2];
        }
    }
    return 1;
}

bool decodeJpeg(const uint8_t* d, size_t n, Image& out, std::string& error) {
    std::vector<uint8_t> pool(4096);   // TJPGD's working memory
    JpegSource src{d, n, 0, &out};
    JDEC jd;
    JRESULT r = jd_prepare(&jd, jpegInput, pool.data(), pool.size(), &src);
    if (r != JDR_OK) { error = "JPEG NOT SUPPORTED OR DAMAGED (CODE " + std::to_string((int)r) + ")"; return false; }
    if (!sizeOk(jd.width, jd.height, error)) return false;
    out.width = jd.width;
    out.height = jd.height;
    out.rgb.assign((size_t)jd.width * jd.height * 3, 0);
    src.pos = 0;   // jd_prepare read the headers; the decode starts again from the top
    r = jd_decomp(&jd, jpegOutput, 0);
    if (r != JDR_OK) { error = "JPEG DATA DAMAGED (CODE " + std::to_string((int)r) + ")"; return false; }
    return true;
}

} // namespace

const char* imageKind(const uint8_t* d, size_t n) {
    if (n >= 8 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return "PNG";
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return "JPEG";
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return "BMP";
    return "";
}

bool decodeImage(const uint8_t* data, size_t size, Image& out, std::string& error) {
    out = Image();
    std::string kind = imageKind(data, size);
    if (kind == "PNG") return decodePng(data, size, out, error);
    if (kind == "JPEG") return decodeJpeg(data, size, out, error);
    if (kind == "BMP") return decodeBmp(data, size, out, error);
    error = "NOT A PNG, JPEG OR BMP FILE";
    return false;
}

} // namespace meshdeck
