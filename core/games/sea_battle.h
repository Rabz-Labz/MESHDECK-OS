// sea_battle.h - SEA BATTLE: a fleet on a 10 x 10 sea against the CPU. The rules have no screen code,
// so they are tested directly. Both fleets are placed at random when a game starts. You fire first
// at the CPU's sea; the CPU fires back after each shot, using the shots it has already hit to find
// the rest of a ship.
#pragma once
#include <stdint.h>
#include <vector>

namespace meshdeck {

class SeaBattle {
public:
    static constexpr int kSize = 10;
    static constexpr int kCells = kSize * kSize;
    static constexpr int kShips = 5;
    static constexpr int kFleet[kShips] = {5, 4, 3, 3, 2};   // ship lengths; 17 cells in all
    static constexpr int kFleetCells = 17;

    enum class Phase { Ready, Playing, Over };
    enum class Shot { Miss, Hit, Sunk, Repeat };   // Repeat: that square was already fired at

    // One side's sea: where its ships are and which squares have been fired at.
    struct Sea {
        int ship[kCells];          // the ship index on each square, -1 for open water
        bool fired[kCells];
        int hits[kShips];          // hits taken by each ship
        int cellsLeft = kFleetCells;
        bool hasShipAt(int i) const { return ship[i] >= 0; }
    };

    explicit SeaBattle(uint32_t seed = 1);
    void start();                                  // a new game with two new random fleets
    // The player fires at square i of the CPU's sea. The CPU then fires back (unless the game is over).
    Shot fire(int i);
    // The square the CPU fired at last, or -1 before its first shot.
    int lastCpuShot() const { return lastCpuShot_; }

    Phase phase() const { return phase_; }
    bool playerWon() const { return cpu_.cellsLeft == 0; }
    int shots() const { return shots_; }           // the player's shots so far
    int points() const { return 200 - shots_; }    // higher is better, for the best-score file; valid when Over
    const Sea& player() const { return player_; }  // your fleet, and the CPU's shots at it
    const Sea& cpu() const { return cpu_; }        // the CPU's fleet, and your shots at it

private:
    uint32_t nextRandom();
    void placeFleet(Sea& sea);
    Shot shootAt(Sea& sea, int i);
    int cpuChoose();                               // the square the CPU fires at next

    Phase phase_ = Phase::Ready;
    uint32_t rng_;
    Sea player_{}, cpu_{};
    int shots_ = 0;
    int lastCpuShot_ = -1;
    std::vector<int> cpuHits_;                     // the CPU's hits on your fleet that are not yet sunk
};

} // namespace meshdeck
