// sea_battle.cpp - see sea_battle.h. Fleets are placed by trying random positions until every ship
// fits. The CPU fires at random until it hits something; then it fires next to its hits until the
// ship sinks.
#include "games/sea_battle.h"
#include <algorithm>

namespace meshdeck {

namespace {

void clearSea(SeaBattle::Sea& sea) {
    for (int i = 0; i < SeaBattle::kCells; i++) {
        sea.ship[i] = -1;
        sea.fired[i] = false;
    }
    for (int s = 0; s < SeaBattle::kShips; s++) sea.hits[s] = 0;
    sea.cellsLeft = SeaBattle::kFleetCells;
}

// The squares next to i, inside the board: up, down, left, right.
std::vector<int> neighbours(int i) {
    int r = i / SeaBattle::kSize, c = i % SeaBattle::kSize;
    std::vector<int> out;
    if (r > 0) out.push_back(i - SeaBattle::kSize);
    if (r < SeaBattle::kSize - 1) out.push_back(i + SeaBattle::kSize);
    if (c > 0) out.push_back(i - 1);
    if (c < SeaBattle::kSize - 1) out.push_back(i + 1);
    return out;
}

} // namespace

SeaBattle::SeaBattle(uint32_t seed) : rng_(seed ? seed : 1) {}

uint32_t SeaBattle::nextRandom() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void SeaBattle::start() {
    placeFleet(player_);
    placeFleet(cpu_);
    shots_ = 0;
    lastCpuShot_ = -1;
    cpuHits_.clear();
    phase_ = Phase::Playing;
}

void SeaBattle::placeFleet(Sea& sea) {
    for (bool placed = false; !placed;) {
        clearSea(sea);
        placed = true;
        for (int s = 0; s < kShips && placed; s++) {
            bool fitted = false;
            for (int tries = 0; tries < 400 && !fitted; tries++) {
                bool across = nextRandom() % 2;
                int r = (int)(nextRandom() % kSize), c = (int)(nextRandom() % kSize);
                int len = kFleet[s];
                if (across ? c + len > kSize : r + len > kSize) continue;
                bool clear = true;
                for (int k = 0; k < len && clear; k++) {
                    int i = across ? r * kSize + c + k : (r + k) * kSize + c;
                    clear = sea.ship[i] < 0;
                }
                if (!clear) continue;
                for (int k = 0; k < len; k++) {
                    int i = across ? r * kSize + c + k : (r + k) * kSize + c;
                    sea.ship[i] = s;
                }
                fitted = true;
            }
            placed = fitted;   // a crowded sea: start the whole fleet again
        }
    }
}

SeaBattle::Shot SeaBattle::shootAt(Sea& sea, int i) {
    sea.fired[i] = true;
    int id = sea.ship[i];
    if (id < 0) return Shot::Miss;
    sea.hits[id]++;
    sea.cellsLeft--;
    return sea.hits[id] == kFleet[id] ? Shot::Sunk : Shot::Hit;
}

SeaBattle::Shot SeaBattle::fire(int i) {
    if (phase_ != Phase::Playing || i < 0 || i >= kCells || cpu_.fired[i]) return Shot::Repeat;
    shots_++;
    Shot result = shootAt(cpu_, i);
    if (cpu_.cellsLeft == 0) {
        phase_ = Phase::Over;
        return result;
    }

    // The CPU fires back.
    int target = cpuChoose();
    lastCpuShot_ = target;
    int id = player_.ship[target];
    Shot reply = shootAt(player_, target);
    if (reply == Shot::Hit) {
        cpuHits_.push_back(target);
    } else if (reply == Shot::Sunk) {
        // The ship is gone: forget its hits, so the CPU hunts elsewhere.
        cpuHits_.erase(std::remove_if(cpuHits_.begin(), cpuHits_.end(),
                                      [&](int h) { return player_.ship[h] == id; }),
                       cpuHits_.end());
    }
    if (player_.cellsLeft == 0) phase_ = Phase::Over;
    return result;
}

int SeaBattle::cpuChoose() {
    // Hunt: a square next to an unsunk hit, if one is still unfired.
    std::vector<int> around;
    for (int h : cpuHits_)
        for (int n : neighbours(h))
            if (!player_.fired[n]) around.push_back(n);
    if (!around.empty()) return around[nextRandom() % around.size()];

    // Otherwise a random unfired square.
    for (int tries = 0; tries < 400; tries++) {
        int i = (int)(nextRandom() % kCells);
        if (!player_.fired[i]) return i;
    }
    for (int i = 0; i < kCells; i++)
        if (!player_.fired[i]) return i;
    return 0;   // unreachable: the game ends before every square is fired at
}

} // namespace meshdeck
