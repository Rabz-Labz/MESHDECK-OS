// map_pack.cpp - see map_pack.h.
#include "formats/map_pack.h"
#include <string.h>
#include <algorithm>

namespace meshdeck {

bool unpackBits(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
    size_t i = 0, o = 0;
    while (i < inLen && o < outLen) {
        int8_t n = (int8_t)in[i++];
        if (n >= 0) {
            size_t count = (size_t)n + 1;
            if (i + count > inLen || o + count > outLen) return false;
            memcpy(out + o, in + i, count);
            i += count;
            o += count;
        } else if (n != -128) {
            size_t count = (size_t)(1 - n);
            if (i >= inLen || o + count > outLen) return false;
            memset(out + o, in[i++], count);
            o += count;
        }
    }
    return o == outLen;
}

static bool readAll(FILE* f, void* buf, size_t len) { return fread(buf, 1, len, f) == len; }

template <typename T> static T get(const uint8_t*& p) {
    T v;
    memcpy(&v, p, sizeof v);   // little-endian file, little-endian CPUs (ESP32-P4, x86)
    p += sizeof v;
    return v;
}

bool MapPack::open(const std::string& path, std::string& error) {
    close();
    file_ = fopen(path.c_str(), "rb");
    if (!file_) { error = "CAN'T OPEN"; return false; }
    uint8_t head[8];
    if (!readAll(file_, head, 8) || memcmp(head, "BMAP", 4) != 0) { error = "NOT A MAP PACK"; close(); return false; }
    uint16_t version, count;
    memcpy(&version, head + 4, 2);
    memcpy(&count, head + 6, 2);
    if (version != 1 || count == 0 || count > 16) { error = "UNSUPPORTED MAP PACK VERSION"; close(); return false; }
    uint8_t nameLen = 0;
    char text[256];
    if (!readAll(file_, &nameLen, 1) || !readAll(file_, text, nameLen)) { error = "DAMAGED HEADER"; close(); return false; }
    name_.assign(text, nameLen);
    uint16_t attrLen = 0;
    if (!readAll(file_, &attrLen, 2) || attrLen > sizeof text || !readAll(file_, text, attrLen)) { error = "DAMAGED HEADER"; close(); return false; }
    attribution_.assign(text, attrLen);
    std::vector<uint32_t> counts;
    for (int i = 0; i < count; i++) {
        uint8_t h[28];
        if (!readAll(file_, h, sizeof h)) { error = "DAMAGED LEVEL TABLE"; close(); return false; }
        const uint8_t* p = h;
        Level l;
        l.level = get<uint8_t>(p);
        get<uint8_t>(p);
        if (get<uint16_t>(p) != kTile) { error = "UNSUPPORTED TILE SIZE"; close(); return false; }
        l.mpp = get<float>(p);
        l.minTx = get<int32_t>(p);
        l.minTy = get<int32_t>(p);
        l.maxTx = get<int32_t>(p);
        l.maxTy = get<int32_t>(p);
        counts.push_back(get<uint32_t>(p));
        levels_.push_back(l);
    }
    for (int i = 0; i < count; i++) {
        levels_[i].index.resize(counts[i]);
        for (uint32_t k = 0; k < counts[i]; k++) {
            uint8_t e[12];
            if (!readAll(file_, e, sizeof e)) { error = "DAMAGED INDEX"; close(); return false; }
            const uint8_t* p = e;
            Entry& en = levels_[i].index[k];
            en.tx = get<int16_t>(p);
            en.ty = get<int16_t>(p);
            en.offset = get<uint32_t>(p);
            en.length = get<uint32_t>(p);
        }
    }
    return true;
}

void MapPack::close() {
    if (file_) fclose(file_);
    file_ = nullptr;
    levels_.clear();
    cache_.clear();
}

const uint8_t* MapPack::tile(int li, int tx, int ty, bool& covered) {
    covered = false;
    if (!file_ || li < 0 || li >= (int)levels_.size()) return nullptr;
    const Level& l = levels_[li];
    covered = tx >= l.minTx && tx <= l.maxTx && ty >= l.minTy && ty <= l.maxTy;
    if (!covered) return nullptr;
    for (auto it = cache_.begin(); it != cache_.end(); ++it)
        if (it->level == li && it->tx == tx && it->ty == ty) {
            if (it != cache_.begin()) cache_.splice(cache_.begin(), cache_, it);
            return cache_.front().px.empty() ? nullptr : cache_.front().px.data();
        }
    auto found = std::lower_bound(l.index.begin(), l.index.end(), std::make_pair(ty, tx),
                                  [](const Entry& e, const std::pair<int, int>& k) { return e.ty != k.first ? e.ty < k.first : e.tx < k.second; });
    Cached c{li, tx, ty, {}};
    if (found != l.index.end() && found->tx == tx && found->ty == ty && found->length < 200000) {
        reads_++;
        std::vector<uint8_t> packed(found->length);
        c.px.resize(kTile * kTile);
        if (fseek(file_, (long)found->offset, SEEK_SET) != 0 || !readAll(file_, packed.data(), packed.size()) ||
            !unpackBits(packed.data(), packed.size(), c.px.data(), c.px.size()))
            c.px.clear();   // unreadable: treat as missing rather than show garbage
    }
    cache_.push_front(std::move(c));
    if (cache_.size() > kCacheTiles) cache_.pop_back();
    return cache_.front().px.empty() ? nullptr : cache_.front().px.data();
}

} // namespace meshdeck
