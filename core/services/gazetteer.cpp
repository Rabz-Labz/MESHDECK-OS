// gazetteer.cpp - see gazetteer.h.
#include "services/gazetteer.h"
#include <ctype.h>
#include <stdlib.h>
#include <algorithm>
#include "kernel/storage.h"

namespace meshdeck {

// Place kinds in ranking order (earlier = shown first when matches are equally good).
static const char* kTypes[] = {"CITY", "TOWN", "VILLAGE", "SUBURB", "HAMLET", "SETTLEMENT", "HILL", "WATER"};
static const int kTypeCount = sizeof kTypes / sizeof kTypes[0];

bool Gazetteer::load(const std::string& path) {
    text_.clear();
    entries_.clear();
    if (!readFile(path, text_, 8 * 1024 * 1024)) return false;
    // One pass over the text: name, tab, type, tab, easting, tab, northing, newline.
    size_t at = 0;
    while (at < text_.size()) {
        size_t end = text_.find('\n', at);
        if (end == std::string::npos) end = text_.size();
        size_t t1 = text_.find('\t', at);
        size_t t2 = t1 == std::string::npos ? t1 : text_.find('\t', t1 + 1);
        size_t t3 = t2 == std::string::npos ? t2 : text_.find('\t', t2 + 1);
        if (t3 != std::string::npos && t3 < end && t1 > at && t1 - at < 65535) {
            std::string type = text_.substr(t1 + 1, t2 - t1 - 1);
            int ti = kTypeCount - 1;
            for (int i = 0; i < kTypeCount; i++)
                if (type == kTypes[i]) ti = i;
            Entry e;
            e.offset = (uint32_t)at;
            e.length = (uint16_t)(t1 - at);
            e.type = (uint8_t)ti;
            e.e = (int32_t)strtol(text_.c_str() + t2 + 1, nullptr, 10);
            e.n = (int32_t)strtol(text_.c_str() + t3 + 1, nullptr, 10);
            entries_.push_back(e);
        }
        at = end + 1;
    }
    return !entries_.empty();
}

// How well name (length len) matches q (already upper case): 0 exact, 1 starts with it, 2 a later
// word starts with it, 3 contains it, -1 no match.
static int matchKind(const char* name, size_t len, const std::string& q) {
    if (q.size() > len) return -1;
    int best = -1;
    for (size_t i = 0; i + q.size() <= len; i++) {
        size_t k = 0;
        while (k < q.size() && toupper((unsigned char)name[i + k]) == q[k]) k++;
        if (k != q.size()) continue;
        if (i == 0) return q.size() == len ? 0 : 1;
        bool wordStart = !isalnum((unsigned char)name[i - 1]);
        int kind = wordStart ? 2 : 3;
        if (best < 0 || kind < best) best = kind;
    }
    return best;
}

std::vector<Gazetteer::Place> Gazetteer::search(const std::string& query, size_t max) const {
    // Step 1: tidy what was typed: trim, upper case.
    std::string q;
    for (char c : query) q += (char)toupper((unsigned char)c);
    size_t a = q.find_first_not_of(' '), b = q.find_last_not_of(' ');
    q = a == std::string::npos ? "" : q.substr(a, b - a + 1);
    std::vector<Place> out;
    if (q.size() < 2) return out;

    // Step 2: score every place; keep the matches.
    struct Hit {
        int kind, type, length;
        const Entry* entry;
    };
    std::vector<Hit> hits;
    for (const Entry& e : entries_) {
        int kind = matchKind(text_.data() + e.offset, e.length, q);
        if (kind >= 0) hits.push_back({kind, e.type, e.length, &e});
    }
    // Step 3: best first, and only as many as asked for.
    size_t n = std::min(max, hits.size());
    std::partial_sort(hits.begin(), hits.begin() + n, hits.end(), [](const Hit& x, const Hit& y) {
        if (x.kind != y.kind) return x.kind < y.kind;
        if (x.type != y.type) return x.type < y.type;
        return x.length < y.length;
    });
    for (size_t i = 0; i < n; i++) {
        const Entry& e = *hits[i].entry;
        out.push_back({text_.substr(e.offset, e.length), kTypes[e.type], e.e, e.n});
    }
    return out;
}

} // namespace meshdeck
