// high_scores.cpp - see high_scores.h.
#include "services/high_scores.h"
#include <stdlib.h>
#include "kernel/storage.h"

namespace meshdeck {

bool HighScores::load(const std::string& path) {
    path_ = path;
    best_.clear();
    if (!fileExists(path)) return true;
    std::string data;
    if (!readFile(path, data, 64 * 1024)) return false;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        std::string line = data.substr(pos, end - pos);
        pos = end + 1;
        // Only a line of the form GAME|number is kept; anything else is skipped.
        size_t bar = line.find('|');
        if (bar == std::string::npos || bar == 0 || bar + 1 >= line.size()) continue;
        char* stop = nullptr;
        long value = strtol(line.c_str() + bar + 1, &stop, 10);
        if (*stop == '\r' || *stop == '\0') {
            if (value < 0) continue;
            best_[line.substr(0, bar)] = (int)value;
        }
    }
    return true;
}

int HighScores::best(const std::string& game) const {
    auto it = best_.find(game);
    return it == best_.end() ? 0 : it->second;
}

bool HighScores::record(const std::string& game, int score) {
    if (score <= best(game) || game.find('|') != std::string::npos || game.empty()) return false;
    best_[game] = score;
    return true;
}

bool HighScores::save() const {
    if (path_.empty()) return false;
    std::string out;
    for (const auto& entry : best_) out += entry.first + "|" + std::to_string(entry.second) + "\n";
    return writeFileAtomic(path_, out);
}

} // namespace meshdeck
