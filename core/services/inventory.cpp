// inventory.cpp - see inventory.h.
#include "services/inventory.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include "kernel/storage.h"
#include "kernel/clock.h"

namespace meshdeck {

static std::vector<std::string> splitBar(const std::string& line) {
    std::vector<std::string> out(1);
    for (char c : line) {
        if (c == '|') out.emplace_back();
        else out.back() += c;
    }
    return out;
}

static std::string noBar(std::string s) {   // keep the line format intact
    for (char& c : s) if (c == '|' || c == '\n' || c == '\r') c = ' ';
    return s;
}

bool Inventory::load(const std::string& path) {
    path_ = path;
    items_.clear();
    locations_.clear();
    if (!fileExists(path)) return true;
    std::string data;
    if (!readFile(path, data, 512 * 1024)) return false;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        std::vector<std::string> f = splitBar(data.substr(pos, end - pos));
        pos = end + 1;
        if (f[0] == "I" && f.size() >= 6) {
            Item it;
            it.name = f[1];
            it.qty = atoi(f[2].c_str());
            it.location = f[3];
            it.expiry = f[4];
            it.note = f[5];
            if (!it.name.empty()) items_.push_back(it);
        } else if (f[0] == "L" && f.size() >= 4 && !f[1].empty()) {
            locations_.push_back({f[1], f[2], atoll(f[3].c_str())});
        }
    }
    return true;
}

bool Inventory::save() const {
    if (path_.empty()) return false;
    std::string out;
    char num[32];
    for (const Item& it : items_) {
        snprintf(num, sizeof num, "%d", it.qty);
        out += "I|" + noBar(it.name) + "|" + num + "|" + noBar(it.location) + "|" + noBar(it.expiry) + "|" + noBar(it.note) + "\n";
    }
    for (const Location& l : locations_) {
        snprintf(num, sizeof num, "%lld", (long long)l.checkedUtc);
        out += "L|" + noBar(l.name) + "|" + noBar(l.tag) + "|" + num + "\n";
    }
    return writeFileAtomic(path_, out);
}

std::vector<std::string> Inventory::locations() const {
    std::vector<std::string> out;
    for (const Item& it : items_) if (!it.location.empty()) out.push_back(it.location);
    for (const Location& l : locations_) out.push_back(l.name);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

Inventory::Location* Inventory::location(const std::string& name) {
    for (Location& l : locations_) if (l.name == name) return &l;
    return nullptr;
}

Inventory::Location& Inventory::ensureLocation(const std::string& name) {
    if (Location* l = location(name)) return *l;
    locations_.push_back({name, "", 0});
    return locations_.back();
}

const Inventory::Location* Inventory::locationForTag(const std::string& tag) const {
    if (tag.empty()) return nullptr;
    for (const Location& l : locations_) if (l.tag == tag) return &l;
    return nullptr;
}

bool Inventory::linkTag(const std::string& location, const std::string& tag) {
    if (location.empty() || tag.empty()) return false;
    for (Location& l : locations_) if (l.tag == tag) l.tag.clear();   // move it, never share it
    ensureLocation(location).tag = tag;
    return true;
}

bool Inventory::validDate(const std::string& t) {
    int y, m, d;
    char extra;
    if (t.size() != 10 || sscanf(t.c_str(), "%4d-%2d-%2d%c", &y, &m, &d, &extra) != 3) return false;
    if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1) return false;
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return d <= mdays[m - 1] + (m == 2 && leap ? 1 : 0);
}

bool Inventory::daysLeft(const std::string& expiry, int64_t nowUtc, int& days) {
    if (!nowUtc || !validDate(expiry)) return false;
    int y, m, d;
    sscanf(expiry.c_str(), "%4d-%2d-%2d", &y, &m, &d);
    CivilTime today = civilFromUtc(localFromUtc(nowUtc));
    int64_t a = utcFromCivil({today.year, today.month, today.day, 0, 0, 0});
    int64_t b = utcFromCivil({y, m, d, 0, 0, 0});
    days = (int)((b - a) / 86400);
    return true;
}

std::string Inventory::expiryText(const std::string& expiry, int64_t nowUtc) {
    if (expiry.empty()) return "NO DATE";
    if (!validDate(expiry)) return "BAD DATE";
    int days;
    if (!daysLeft(expiry, nowUtc, days)) return "DATE NOT CHECKED (CLOCK NOT SET)";
    char buf[48];
    if (days < -1) snprintf(buf, sizeof buf, "EXPIRED %d DAYS AGO", -days);
    else if (days == -1) snprintf(buf, sizeof buf, "EXPIRED YESTERDAY");
    else if (days == 0) snprintf(buf, sizeof buf, "EXPIRES TODAY");
    else if (days == 1) snprintf(buf, sizeof buf, "1 DAY LEFT");
    else snprintf(buf, sizeof buf, "%d DAYS LEFT", days);
    return buf;
}

int Inventory::expiringCount(int64_t nowUtc, int withinDays) const {
    int n = 0, days;
    for (const Item& it : items_)
        if (daysLeft(it.expiry, nowUtc, days) && days <= withinDays) n++;
    return n;
}

int Inventory::addStarterKit(const std::string& location) {
    // GOV.UK "Prepare" household emergency kit suggestions, in our words.
    static const char* kKit[][2] = {
        {"DRINKING WATER", "ABOUT 3 LITRES PER PERSON PER DAY"},
        {"FOOD THAT KEEPS", "NO COOKING NEEDED"},
        {"TORCH", "WIND-UP OR BATTERY"},
        {"SPARE BATTERIES", ""},
        {"RADIO", "WIND-UP OR BATTERY"},
        {"FIRST AID KIT", ""},
        {"PRESCRIPTION MEDICINES", "AND COPIES OF PRESCRIPTIONS"},
        {"PHONE POWER BANK", "CHARGED"},
        {"CASH", "SMALL NOTES AND COINS"},
        {"COPIES OF DOCUMENTS", "ID, INSURANCE, CONTACTS - IN A WATERPROOF BAG"},
        {"WARM CLOTHES AND BLANKETS", ""},
        {"HYGIENE ITEMS", "WIPES, BAGS, SANITISER"},
        {"WHISTLE", "TO SIGNAL FOR HELP"},
        {"SPARE KEYS", "HOUSE AND CAR"},
    };
    int added = 0;
    for (const auto& k : kKit) {
        bool have = false;
        for (const Item& it : items_) if (it.location == location && it.name == k[0]) have = true;
        if (have) continue;
        Item it;
        it.name = k[0];
        it.note = k[1];
        it.location = location;
        items_.push_back(it);
        added++;
    }
    return added;
}

} // namespace meshdeck
