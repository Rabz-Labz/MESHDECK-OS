// inventory.h - kit and supply caches (items, locations, expiry dates). Ported from 1.x.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace meshdeck {

// Kit and supply caches. Items have a quantity, a location (grab bag, car, store cupboard...), an
// optional best-before/expiry date and a note. A location also records when it was last checked.
// The tag field is kept so 1.x files open unchanged; MESHDECK 2.0 does not link tags on the device.
//
// Saved to MESHDECK/inventory/inventory.txt with a power-safe replace (the old file or the new one,
// never half of each). Lines: "I|name|qty|location|YYYY-MM-DD|note" and "L|location|tag|checkedUtc".
class Inventory {
public:
    struct Item { std::string name, location, expiry, note; int qty = 0; };
    struct Location { std::string name, tag; int64_t checkedUtc = 0; };

    bool load(const std::string& path);   // false only when the file exists but can't be read
    bool save() const;
    const std::string& path() const { return path_; }

    std::vector<Item>& items() { return items_; }
    const std::vector<Item>& items() const { return items_; }
    std::vector<std::string> locations() const;                  // from items and linked tags, sorted
    Location* location(const std::string& name);                 // nullptr if never linked/checked
    Location& ensureLocation(const std::string& name);
    const Location* locationForTag(const std::string& tag) const;
    bool linkTag(const std::string& location, const std::string& tag);   // a tag belongs to one location

    // Days from today (UK local date of nowUtc) to the expiry date. False when the item has no
    // valid date or the clock isn't set.
    static bool daysLeft(const std::string& expiry, int64_t nowUtc, int& days);
    // "EXPIRED 3 DAYS AGO", "EXPIRES TODAY", "12 DAYS LEFT", "NO DATE", "DATE NOT CHECKED (CLOCK)".
    static std::string expiryText(const std::string& expiry, int64_t nowUtc);
    static bool validDate(const std::string& text);              // YYYY-MM-DD, a real date
    int expiringCount(int64_t nowUtc, int withinDays) const;     // includes expired

    // The UK government's suggested household emergency kit, added as items with quantity 0
    // (to be counted) in the given location. Skips names already there.
    int addStarterKit(const std::string& location);

private:
    std::string path_;
    std::vector<Item> items_;
    std::vector<Location> locations_;
};

} // namespace meshdeck
