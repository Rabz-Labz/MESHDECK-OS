// gazetteer.h - finds places by name for MAP's search.
//
// The index is a text file, MESHDECK/maps/places.txt, made by tools/build_gazetteer.py from OS Open
// Zoomstack: one place per line, "NAME<TAB>TYPE<TAB>EASTING<TAB>NORTHING". The default build
// covers the Scottish Borders, East Lothian and Northumberland: about 25,000 towns, villages,
// hamlets, small settlements, hills and lochs/rivers (900 KB, held in memory: PSRAM on the Tab5).
//
// Search ignores case and ranks results:
//   1. how the name matches: exactly, then starting with what was typed, then a later word
//      starting with it ("BERWICK" finds "North Berwick"), then anywhere inside it;
//   2. then the kind of place: city, town, village, suburb, hamlet, settlement, hill, water;
//   3. then the shorter name.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace meshdeck {

class Gazetteer {
public:
    struct Place {
        std::string name;
        std::string type;   // CITY, TOWN, VILLAGE, SUBURB, HAMLET, SETTLEMENT, HILL, WATER
        int easting = 0, northing = 0;
    };

    // Loads the index. False if the file is missing or empty.
    bool load(const std::string& path);
    bool loaded() const { return !entries_.empty(); }
    size_t size() const { return entries_.size(); }
    // Up to `max` best matches for what was typed (at least 2 letters).
    std::vector<Place> search(const std::string& query, size_t max = 30) const;

private:
    struct Entry {
        uint32_t offset;   // name position in text_
        uint16_t length;
        uint8_t type;      // index into kTypes
        int32_t e, n;
    };
    std::string text_;
    std::vector<Entry> entries_;
};

} // namespace meshdeck
