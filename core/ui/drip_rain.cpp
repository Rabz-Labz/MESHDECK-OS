// drip_rain.cpp - see drip_rain.h. Each column has one drip: it waits a random moment, falls at its own
// speed, and starts again from the top when it leaves the bottom (until the rain is stopped). Where a
// drip passes a cell of the word, that cell is lit for good.
#include "ui/drip_rain.h"
#include <math.h>
#include <string.h>

namespace meshdeck {
namespace {

// The word, and the characters it uses, 5 columns by 7 rows each, one row per string. 1 is a lit cell.
const char kWord[] = "MESHDECK - OS";
struct Glyph { char ch; const char* rows[7]; };
const Glyph kGlyphs[] = {
    {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
    {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
    {'C', {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}},
    {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
    {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'-', {"00000", "00000", "00000", "01110", "00000", "00000", "00000"}},
};

const Glyph* glyphFor(char ch) {
    for (const Glyph& g : kGlyphs)
        if (g.ch == ch) return &g;
    return nullptr;   // a space
}

} // namespace

DripRain::DripRain(int cols, int rows, uint32_t seed) : cols_(cols), rows_(rows), rng_(seed ? seed : 1) {
    left_ = (cols_ - kWordCols) / 2;
    top_ = rows_ / 3;
    word_.assign((size_t)cols_ * rows_, 0);
    lit_.assign((size_t)cols_ * rows_, 0);
    // Step 1: the word, character by character, one column apart. Cells off the grid are left out.
    static_assert(sizeof kWord - 1 == kWordChars, "kWordChars must match the word");
    for (int i = 0; i < kWordChars; i++) {
        const Glyph* g = glyphFor(kWord[i]);
        if (!g) continue;
        for (int r = 0; r < kWordRows; r++)
            for (int c = 0; c < 5; c++) {
                if (g->rows[r][c] != '1') continue;
                int gx = left_ + i * 6 + c, gy = top_ + r;
                if (gx < 0 || gy < 0 || gx >= cols_ || gy >= rows_) continue;
                word_[(size_t)gy * cols_ + gx] = 1;
                wordCells_++;
            }
    }
    // Step 2: each column starts with its own random wait and speed, so the drips never move in step.
    head_.assign(cols_, -1);
    speed_.assign(cols_, 0);
    wait_.assign(cols_, 0);
    for (int c = 0; c < cols_; c++) wait_[c] = unit() * 1.5;
}

uint32_t DripRain::nextRandom() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

double DripRain::unit() { return nextRandom() / 4294967296.0; }

bool DripRain::lit(int col, int row) const {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) return false;
    return lit_[(size_t)row * cols_ + col] != 0;
}

bool DripRain::isWordCell(int col, int row) const {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) return false;
    return word_[(size_t)row * cols_ + col] != 0;
}

bool DripRain::drained() const {
    if (!stopped_) return false;
    for (int c = 0; c < cols_; c++)
        if (head_[c] >= 0) return false;
    return true;
}

bool DripRain::done() const {
    for (size_t i = 0; i < word_.size(); i++)
        if (word_[i] && !lit_[i]) return false;
    return true;
}

// Lights the word cells a drip has passed between two positions of its front (in rows).
void DripRain::lightBetween(int col, double from, double to) {
    int first = (int)floor(from < 0 ? 0 : from), last = (int)floor(to);
    for (int r = first; r <= last && r < rows_; r++)
        if (r >= 0 && isWordCell(col, r)) lit_[(size_t)r * cols_ + col] = 1;
}

void DripRain::step(double dt) {
    if (dt < 0) dt = 0;
    if (dt > 0.1) dt = 0.1;   // a long pause (a busy frame) must not make the drips jump
    for (int c = 0; c < cols_; c++) {
        if (head_[c] < 0) {
            if (stopped_) continue;   // no new drips once the rain is stopped
            // Waiting above the screen: when the wait is over, the drip starts at the top at its own speed.
            wait_[c] -= dt;
            if (wait_[c] <= 0) {
                head_[c] = 0;
                speed_[c] = 12 + unit() * 16;   // rows per second
            }
            continue;
        }
        double before = head_[c];
        head_[c] += speed_[c] * dt;
        lightBetween(c, before, head_[c]);
        if (head_[c] >= rows_) {
            head_[c] = -1;
            wait_[c] = 0.2 + unit() * 1.2;
        }
    }
}

} // namespace meshdeck
