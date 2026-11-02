// settings.h - user preferences as named values, saved to a small text file.
//
// Each setting is a key (letters, digits, '.', '_') with a text value. Typed getters return a
// default when the key is missing or unreadable, so a fresh or damaged file never stops the deck
// starting. Values are saved with writeFileAtomic, one "key=value" line each, in
// <dataRoot>/MESHDECK/settings.txt; the file is readable and editable on any PC.
//
// Changes are kept in memory and written by save(). The shell calls save() a second after the
// last change, so a run of +/- presses costs one write.
#pragma once
#include <map>
#include <string>

namespace meshdeck {

class Settings {
public:
    // Loads the file. Missing file = all defaults (returns false, which is not an error).
    bool load(const std::string& path);
    // Writes every value. False if storage can't be written (the values stay in memory).
    bool save();
    bool dirty() const { return dirty_; }

    std::string getString(const std::string& key, const std::string& fallback = "") const;
    int getInt(const std::string& key, int fallback = 0) const;
    bool getBool(const std::string& key, bool fallback = false) const;

    // Setters mark the settings dirty only if the value actually changed. Keys with characters
    // outside [A-Za-z0-9._] and values containing a line break are refused (false).
    bool set(const std::string& key, const std::string& value);
    bool setInt(const std::string& key, int value) { return set(key, std::to_string(value)); }
    bool setBool(const std::string& key, bool value) { return set(key, value ? "1" : "0"); }
    void clear();   // back to all defaults (still needs save())

    size_t size() const { return values_.size(); }

private:
    static bool validKey(const std::string& key);
    std::string path_;
    std::map<std::string, std::string> values_;
    bool dirty_ = false;
};

} // namespace meshdeck
