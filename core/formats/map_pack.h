// map_pack.h - reads MESHDECK map packs (.map, made by tools/build_map.py from OS Open Zoomstack).
// Ported unchanged from MESHDECK 1.x. Format notes: docs/formats/map-pack.md.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <list>
#include <string>
#include <vector>

namespace meshdeck {

// Reads a MESHDECK map pack (made by tools/build_map.py from OS Open Zoomstack): 256 x 256,
// 8-bit brightness tiles on the OS National Grid, PackBits-compressed, with an index per level.
// Tile (tx, ty) covers eastings [tx*256*mpp, (tx+1)*256*mpp) and northings likewise; row 0 is the
// north edge. Missing tiles inside a level's area are blank (sea or nothing mapped).
class MapPack {
public:
    struct Entry { int16_t tx, ty; uint32_t offset, length; };
    struct Level {
        int level = 0;
        float mpp = 0;                 // metres per pixel
        int minTx = 0, minTy = 0, maxTx = 0, maxTy = 0;
        std::vector<Entry> index;      // sorted by (ty, tx)
    };
    static constexpr int kTile = 256;

    ~MapPack() { close(); }
    bool open(const std::string& path, std::string& error);
    void close();
    const std::string& name() const { return name_; }
    const std::string& attribution() const { return attribution_; }
    const std::vector<Level>& levels() const { return levels_; }

    // The tile's pixels, or nullptr when there is none. covered says whether (tx, ty) lies inside
    // the level's area (so a missing tile there is blank, not unknown).
    const uint8_t* tile(int levelIndex, int tx, int ty, bool& covered);
    uint32_t tilesRead() const { return reads_; }   // tiles read from the card so far (cache misses)

private:
    struct Cached { int level, tx, ty; std::vector<uint8_t> px; };
    FILE* file_ = nullptr;
    std::string name_, attribution_;
    std::vector<Level> levels_;
    std::list<Cached> cache_;          // most recently used first
    uint32_t reads_ = 0;
    static constexpr size_t kCacheTiles = 64;   // 4 MB (PSRAM on the Tab5): a whole view plus room to zoom back
};

// PackBits decoder (exposed for tests). Returns false on malformed or short data.
bool unpackBits(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen);

} // namespace meshdeck
