// drip_rain.h - the boot splash's rain: a column of drips falls down the screen, and each drip that
// passes a cell of the words "MESHDECK - OS" lights that cell, so the words form as the drips fall. No
// screen code here; the splash draws the state this class keeps.
#pragma once
#include <stdint.h>
#include <vector>

namespace meshdeck {

class DripRain {
public:
    static constexpr int kWordChars = 13;                  // "MESHDECK - OS"
    static constexpr int kWordCols = kWordChars * 6 - 1, kWordRows = 7;   // 5 x 7 characters, one column between them

    // cols and rows are the grid the screen is split into; the word is centred on it.
    DripRain(int cols, int rows, uint32_t seed);
    void step(double dt);                 // moves every drip on by dt seconds

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    double headRow(int col) const { return head_[col]; }   // the drip's front, in rows; below 0 while waiting
    bool lit(int col, int row) const;     // whether the word's cell at this grid position is lit
    bool isWordCell(int col, int row) const;   // whether the word has a cell here (lit or not)
    int wordLeft() const { return left_; }
    int wordTop() const { return top_; }
    bool done() const;                    // every cell of the word is lit
    void stop() { stopped_ = true; }      // no new drips: those falling carry on down and off the screen
    bool drained() const;                 // stopped, and every drip has left the screen
    int wordCells() const { return wordCells_; }   // how many cells the word has in all

private:
    uint32_t nextRandom();
    double unit();                        // a random number in [0, 1)
    void lightBetween(int col, double from, double to);

    int cols_, rows_;
    int left_, top_;                      // where the word's top-left cell sits on the grid
    int wordCells_ = 0;
    bool stopped_ = false;
    uint32_t rng_;
    std::vector<double> head_, speed_, wait_;
    std::vector<uint8_t> word_;           // word cells, row-major over the grid: 1 = a letter cell
    std::vector<uint8_t> lit_;            // the same grid: 1 = that letter cell has been lit
};

} // namespace meshdeck
