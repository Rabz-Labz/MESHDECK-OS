// high_scores.h - the best score of each game, kept on the card in MESHDECK/games/scores.txt. One line
// per game: "GAME|score". A damaged line is skipped, so one bad line never loses the others.
#pragma once
#include <map>
#include <string>

namespace meshdeck {

class HighScores {
public:
    // Reads the file. A missing file is simply empty; false only if it exists and can't be read.
    bool load(const std::string& path);
    int best(const std::string& game) const;          // 0 when the game has no score yet
    // Keeps score if it beats the best so far. Returns true for a new best. Nothing is written until save().
    bool record(const std::string& game, int score);
    bool save() const;                                // writes the whole file, safely

private:
    std::string path_;
    std::map<std::string, int> best_;
};

} // namespace meshdeck
