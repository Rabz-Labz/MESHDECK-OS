// book_pack.cpp - see book_pack.h.
#include "formats/book_pack.h"
#include <ctype.h>
#include <string.h>
#include "lvgl.h"
#define LODEPNG_NO_COMPILE_CPP   // only the C API (its C++ wrappers clash inside LVGL's extern "C")
#include "src/libs/lodepng/lodepng.h"

namespace meshdeck {

bool BookPack::readAt(uint32_t off, void* buf, size_t len) {
    return file_ && fseek(file_, (long)off, SEEK_SET) == 0 && fread(buf, 1, len, file_) == len;
}

static bool readU16Str(FILE* f, std::string& out) {
    uint16_t n = 0;
    if (fread(&n, 2, 1, f) != 1 || n > 1000) return false;
    out.resize(n);
    return n == 0 || fread(&out[0], 1, n, f) == n;
}

bool BookPack::open(const std::string& path, std::string& error, bool headerOnly) {
    close();
    file_ = fopen(path.c_str(), "rb");
    if (!file_) { error = "CAN'T OPEN"; return false; }
    uint8_t head[8];
    if (fread(head, 1, 8, file_) != 8 || memcmp(head, "BBOK", 4) != 0) { error = "NOT A BOOK PACK"; close(); return false; }
    uint16_t version;
    memcpy(&version, head + 4, 2);
    if (version != 2 || (head[6] != Pages && head[6] != Text)) { error = "UNSUPPORTED BOOK VERSION (CONVERT AGAIN)"; close(); return false; }
    kind_ = (Kind)head[6];
    uint32_t count = 0, tocCount = 0;
    if (!readU16Str(file_, title_) || !readU16Str(file_, author_) || !readU16Str(file_, category_) || !readU16Str(file_, source_) ||
        fread(&count, 4, 1, file_) != 1 || fread(&tocCount, 4, 1, file_) != 1 || count > 200000 || tocCount > 20000) {
        error = "DAMAGED HEADER";
        close();
        return false;
    }
    if (headerOnly) {   // count only: no contents or index read
        index_.assign(count, Entry{0, 0, 0, 0});
        fclose(file_);
        file_ = nullptr;
        return true;
    }
    toc_.clear();
    for (uint32_t i = 0; i < tocCount; i++) {
        TocEntry t;
        if (fread(&t.index, 4, 1, file_) != 1 || fread(&t.level, 1, 1, file_) != 1 || !readU16Str(file_, t.title)) {
            error = "DAMAGED CONTENTS";
            close();
            return false;
        }
        if (t.index < count) toc_.push_back(t);
    }
    index_.resize(count);
    if (count && fread(index_.data(), sizeof(Entry), count, file_) != count) { error = "DAMAGED INDEX"; close(); return false; }
    return true;
}

void BookPack::close() {
    if (file_) fclose(file_);
    file_ = nullptr;
    index_.clear();
    toc_.clear();
}

bool BookPack::pageImage(uint32_t i, std::vector<uint8_t>& px, uint32_t& w, uint32_t& h, std::string& error) {
    if (kind_ != Pages || i >= index_.size() || !index_[i].imgLen) { error = "NO PAGE IMAGE"; return false; }
    // u16 width, u16 height, then zlib of 4-bit grey levels packed two per byte (high nibble first).
    std::vector<uint8_t> blob(index_[i].imgLen);
    if (blob.size() < 6 || !readAt(index_[i].imgOff, blob.data(), blob.size())) { error = "CAN'T READ THE PAGE"; return false; }
    uint16_t bw, bh;
    memcpy(&bw, blob.data(), 2);
    memcpy(&bh, blob.data() + 2, 2);
    if (!bw || !bh || bw % 2 || bw > 4096 || bh > 8192) { error = "PAGE IMAGE DAMAGED (SIZE)"; return false; }
    unsigned char* out = nullptr;
    size_t outSize = 0;
    unsigned e = lodepng_zlib_decompress(&out, &outSize, blob.data() + 4, blob.size() - 4, &lodepng_default_decompress_settings);
    if (e || !out || outSize != (size_t)bw * bh / 2) {
        if (out) lv_free(out);
        error = "PAGE IMAGE DAMAGED (" + std::to_string(e) + ")";
        return false;
    }
    px.resize((size_t)bw * bh);
    for (size_t k = 0; k < outSize; k++) {
        px[k * 2] = (uint8_t)((out[k] >> 4) * 17);
        px[k * 2 + 1] = (uint8_t)((out[k] & 15) * 17);
    }
    lv_free(out);
    w = bw;
    h = bh;
    return true;
}

bool BookPack::text(uint32_t i, std::string& out) {
    out.clear();
    if (i >= index_.size()) return false;
    out.resize(index_[i].txtLen);
    return out.empty() || readAt(index_[i].txtOff, &out[0], out.size());
}

std::vector<BookPack::Hit> BookPack::search(const std::string& query, size_t maxHits) {
    std::vector<Hit> hits;
    std::string q;
    for (char c : query) q += (char)tolower((unsigned char)c);
    if (q.empty() || !file_) return hits;
    std::string t;
    for (uint32_t i = 0; i < index_.size() && hits.size() < maxHits; i++) {
        if (!text(i, t)) continue;
        std::string low = t;
        for (char& c : low) c = (char)tolower((unsigned char)c);
        size_t at = low.find(q);
        if (at == std::string::npos) continue;
        size_t from = at > 40 ? at - 40 : 0;
        std::string snip = t.substr(from, 110);
        for (char& c : snip) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        hits.push_back({i, (from ? "..." : "") + snip + "..."});
    }
    return hits;
}

} // namespace meshdeck
