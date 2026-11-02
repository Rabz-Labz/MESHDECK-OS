// klondike.h - KLONDIKE: the card game of seven columns, four foundations, a stock and a waste pile,
// drawing one card at a time. The rules have no screen code, so they are tested directly.
//
// Locations, as the screen names them: the stock (face down, draw from it), the waste (the cards drawn,
// face up; only the top one can be played), the four foundations (each builds up from an ace to a
// king, by suit), and seven tableau columns (build down by alternating colour; a face-down card is
// turned up when the card above it moves away). Empty columns take kings only.
#pragma once
#include <stdint.h>
#include <vector>

namespace meshdeck {

class Klondike {
public:
    struct Card {
        uint8_t rank = 0;   // 1 (ace) to 13 (king)
        uint8_t suit = 0;   // 0 spades, 1 hearts, 2 diamonds, 3 clubs; hearts and diamonds are red
        bool up = false;    // face up (visible)
    };
    static constexpr int kColumns = 7;
    static constexpr int kFoundations = 4;

    explicit Klondike(uint32_t seed = 1);
    void newGame();                       // a fresh shuffled deal
    // Turns the next stock card face up onto the waste. With the stock empty, the waste goes back to
    // the stock (turned over, in order). Returns false only if there is nothing to draw or recycle.
    bool draw();
    // Moves the cards of column `col` from index `from` to the end (a face-up run) onto column `dst`.
    bool moveRun(int col, int from, int dst);
    bool wasteToColumn(int dst);
    bool wasteToFoundation(int f);
    bool columnToFoundation(int col, int f);     // the top card only
    bool foundationToColumn(int f, int dst);
    bool undo();                          // takes back the last move or draw

    bool won() const { return cardsOnFoundations() == 52; }
    int moves() const { return moves_; }  // moves made, not counting draws
    int cardsOnFoundations() const;

    const std::vector<Card>& stock() const { return s_.stock; }
    const std::vector<Card>& waste() const { return s_.waste; }
    const std::vector<Card>& foundation(int f) const { return s_.foundation[f]; }
    const std::vector<Card>& column(int c) const { return s_.column[c]; }

    // Whether a run or a card could be placed; used by the screen to show what is allowed.
    bool canPlaceOnColumn(const Card& c, int dst) const;
    bool canPlaceOnFoundation(const Card& c, int f) const;

private:
    struct State {
        std::vector<Card> stock, waste;
        std::vector<Card> foundation[kFoundations];
        std::vector<Card> column[kColumns];
        int moves = 0;                   // the move count at this point, so undo can restore it
    };
    uint32_t nextRandom();
    void saveForUndo();
    void turnTopUp(int col);             // a face-down card left on top of a column turns up
    bool runIsFaceUpSequence(int col, int from) const;

    State s_;
    std::vector<State> history_;         // snapshots before each move, for undo
    int moves_ = 0;
    uint32_t rng_;
};

} // namespace meshdeck
