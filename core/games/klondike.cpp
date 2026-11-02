// klondike.cpp - see klondike.h. Every action that changes the layout first saves a copy of the
// layout, so undo can go back exactly. A column's face-down card turns up as soon as the card above
// it has gone.
#include "games/klondike.h"
#include <algorithm>

namespace meshdeck {
namespace {

const size_t kMaxHistory = 400;   // undo goes back this far at most

bool red(uint8_t suit) { return suit == 1 || suit == 2; }

} // namespace

Klondike::Klondike(uint32_t seed) : rng_(seed ? seed : 1) { newGame(); }

uint32_t Klondike::nextRandom() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void Klondike::newGame() {
    // Step 1: all 52 cards, shuffled (Fisher-Yates).
    std::vector<Card> deck;
    for (uint8_t suit = 0; suit < 4; suit++)
        for (uint8_t rank = 1; rank <= 13; rank++) deck.push_back({rank, suit, false});
    for (size_t i = deck.size() - 1; i > 0; i--) std::swap(deck[i], deck[nextRandom() % (i + 1)]);

    // Step 2: deal column c gets c + 1 cards, the last one face up; the rest form the stock.
    s_ = State();
    size_t next = 0;
    for (int c = 0; c < kColumns; c++) {
        for (int k = 0; k <= c; k++) s_.column[c].push_back(deck[next++]);
        s_.column[c].back().up = true;
    }
    while (next < deck.size()) s_.stock.push_back(deck[next++]);
    history_.clear();
    moves_ = 0;
}

void Klondike::saveForUndo() {
    State snap = s_;
    snap.moves = moves_;
    history_.push_back(snap);
    if (history_.size() > kMaxHistory) history_.erase(history_.begin());
}

bool Klondike::draw() {
    if (s_.stock.empty() && s_.waste.empty()) return false;
    saveForUndo();
    if (s_.stock.empty()) {
        // Recycle: the waste goes back under the stock, turned over, so the order is kept.
        while (!s_.waste.empty()) {
            Card c = s_.waste.back();
            c.up = false;
            s_.stock.push_back(c);
            s_.waste.pop_back();
        }
    } else {
        Card c = s_.stock.back();
        c.up = true;
        s_.waste.push_back(c);
        s_.stock.pop_back();
    }
    return true;
}

void Klondike::turnTopUp(int col) {
    std::vector<Card>& c = s_.column[col];
    if (!c.empty() && !c.back().up) c.back().up = true;
}

bool Klondike::canPlaceOnColumn(const Card& c, int dst) const {
    if (dst < 0 || dst >= kColumns) return false;
    const std::vector<Card>& column = s_.column[dst];
    if (column.empty()) return c.rank == 13;   // only a king starts an empty column
    const Card& top = column.back();
    return top.up && top.rank == c.rank + 1 && red(top.suit) != red(c.suit);
}

bool Klondike::canPlaceOnFoundation(const Card& c, int f) const {
    if (f < 0 || f >= kFoundations) return false;
    const std::vector<Card>& pile = s_.foundation[f];
    if (pile.empty()) return c.rank == 1;      // aces start a foundation
    const Card& top = pile.back();
    return top.suit == c.suit && top.rank + 1 == c.rank;
}

bool Klondike::runIsFaceUpSequence(int col, int from) const {
    const std::vector<Card>& c = s_.column[col];
    if (from < 0 || from >= (int)c.size() || !c[from].up) return false;
    for (size_t i = from; i + 1 < c.size(); i++) {
        if (!c[i + 1].up) return false;
        if (c[i].rank != c[i + 1].rank + 1 || red(c[i].suit) == red(c[i + 1].suit)) return false;
    }
    return true;
}

bool Klondike::moveRun(int col, int from, int dst) {
    if (col < 0 || col >= kColumns || col == dst || !runIsFaceUpSequence(col, from)) return false;
    if (!canPlaceOnColumn(s_.column[col][from], dst)) return false;
    saveForUndo();
    std::vector<Card>& src = s_.column[col];
    s_.column[dst].insert(s_.column[dst].end(), src.begin() + from, src.end());
    src.erase(src.begin() + from, src.end());
    turnTopUp(col);
    moves_++;
    return true;
}

bool Klondike::wasteToColumn(int dst) {
    if (s_.waste.empty() || !canPlaceOnColumn(s_.waste.back(), dst)) return false;
    saveForUndo();
    s_.column[dst].push_back(s_.waste.back());
    s_.waste.pop_back();
    moves_++;
    return true;
}

bool Klondike::wasteToFoundation(int f) {
    if (s_.waste.empty() || !canPlaceOnFoundation(s_.waste.back(), f)) return false;
    saveForUndo();
    s_.foundation[f].push_back(s_.waste.back());
    s_.waste.pop_back();
    moves_++;
    return true;
}

bool Klondike::columnToFoundation(int col, int f) {
    if (col < 0 || col >= kColumns || s_.column[col].empty() || !s_.column[col].back().up) return false;
    if (!canPlaceOnFoundation(s_.column[col].back(), f)) return false;
    saveForUndo();
    s_.foundation[f].push_back(s_.column[col].back());
    s_.column[col].pop_back();
    turnTopUp(col);
    moves_++;
    return true;
}

bool Klondike::foundationToColumn(int f, int dst) {
    if (f < 0 || f >= kFoundations || s_.foundation[f].empty()) return false;
    if (!canPlaceOnColumn(s_.foundation[f].back(), dst)) return false;
    saveForUndo();
    s_.column[dst].push_back(s_.foundation[f].back());
    s_.foundation[f].pop_back();
    moves_++;
    return true;
}

bool Klondike::undo() {
    if (history_.empty()) return false;
    s_ = history_.back();
    moves_ = s_.moves;
    history_.pop_back();
    return true;
}

int Klondike::cardsOnFoundations() const {
    int n = 0;
    for (int f = 0; f < kFoundations; f++) n += (int)s_.foundation[f].size();
    return n;
}

} // namespace meshdeck
