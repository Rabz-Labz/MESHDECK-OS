// settings.cpp - see settings.h.
#include "settings.h"
#include <stdlib.h>
#include "storage.h"

namespace meshdeck {

bool Settings::validKey(const std::string& key) {
    if (key.empty() || key.size() > 64) return false;
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_')) return false;
    return true;
}

bool Settings::load(const std::string& path) {
    path_ = path;
    values_.clear();
    dirty_ = false;
    std::string data;
    if (path.empty() || !readFile(path, data, 64 * 1024)) return false;
    // One "key=value" per line. Lines that don't fit (comments, damage) are skipped, so a
    // half-written or hand-edited file still loads whatever is readable.
    size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        std::string line = data.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        std::string key = line.substr(0, eq);
        if (validKey(key)) values_[key] = line.substr(eq + 1);
    }
    return true;
}

bool Settings::save() {
    if (path_.empty()) return false;
    std::string out = "# MESHDECK settings (key=value)\n";
    for (const auto& kv : values_) out += kv.first + "=" + kv.second + "\n";
    if (!writeFileAtomic(path_, out)) return false;
    dirty_ = false;
    return true;
}

std::string Settings::getString(const std::string& key, const std::string& fallback) const {
    auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

int Settings::getInt(const std::string& key, int fallback) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return fallback;
    char* end = nullptr;
    long v = strtol(it->second.c_str(), &end, 10);
    return (end && *end == '\0') ? (int)v : fallback;   // "12abc" is unreadable -> default
}

bool Settings::getBool(const std::string& key, bool fallback) const {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    if (it->second == "1" || it->second == "true" || it->second == "on") return true;
    if (it->second == "0" || it->second == "false" || it->second == "off") return false;
    return fallback;
}

bool Settings::set(const std::string& key, const std::string& value) {
    if (!validKey(key) || value.find_first_of("\r\n") != std::string::npos) return false;
    auto it = values_.find(key);
    if (it != values_.end() && it->second == value) return true;
    values_[key] = value;
    dirty_ = true;
    return true;
}

void Settings::clear() {
    if (!values_.empty()) dirty_ = true;
    values_.clear();
}

} // namespace meshdeck
