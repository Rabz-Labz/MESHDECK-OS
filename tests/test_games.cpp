// test_games.cpp - the game rules (RALLY, pong against the CPU) and the best-score file.
#include <math.h>
#include <algorithm>
#include <string>
#include "doctest.h"
#include <stdio.h>
#include "games/klondike.h"
#include "games/pong.h"
#include "games/sea_battle.h"
#include "services/high_scores.h"
#include "test_util.h"

namespace {

// Runs the game for up to maxSeconds with the player's paddle following the ball (or not at all).
// Returns the seconds it took, or -1 if the match did not end.
double playMatch(meshdeck::Pong& game, bool trackBall, double maxSeconds) {
    double t = 0;
    game.start();
    while (game.phase() != meshdeck::Pong::Phase::Over && t < maxSeconds) {
        int move = 0;
        if (trackBall) {
            double d = game.ballY() - game.playerY();
            move = d > 4 ? 1 : d < -4 ? -1 : 0;
        }
        game.step(1.0 / 60, move);
        t += 1.0 / 60;
    }
    return game.phase() == meshdeck::Pong::Phase::Over ? t : -1;
}

} // namespace

TEST_CASE("pong: a match starts with a serve and ends at seven points") {
    meshdeck::Pong game(7);
    CHECK(game.phase() == meshdeck::Pong::Phase::Ready);
    game.start();
    CHECK(game.phase() == meshdeck::Pong::Phase::Point);      // a short pause before the serve
    for (int i = 0; i < 70; i++) game.step(1.0 / 60, 0);
    CHECK(game.phase() == meshdeck::Pong::Phase::Playing);
    double seconds = playMatch(game, false, 600);
    REQUIRE(seconds > 0);
    CHECK(game.phase() == meshdeck::Pong::Phase::Over);
    CHECK(game.cpuScore() == meshdeck::Pong::kWinScore);      // a player who never moves loses
    CHECK(game.playerScore() < meshdeck::Pong::kWinScore);
    CHECK_FALSE(game.playerWon());
}

TEST_CASE("pong: the ball and both paddles stay inside the field") {
    meshdeck::Pong game(11);
    game.start();
    for (int i = 0; i < 60 * 120; i++) {   // two minutes of a player who tracks the ball
        double d = game.ballY() - game.playerY();
        game.step(1.0 / 60, d > 4 ? 1 : d < -4 ? -1 : 0);
        CHECK(game.ballY() >= 0);
        CHECK(game.ballY() <= meshdeck::Pong::kHeight);
        CHECK(game.playerY() >= meshdeck::Pong::kPaddleH / 2 - 1e-9);
        CHECK(game.playerY() <= meshdeck::Pong::kHeight - meshdeck::Pong::kPaddleH / 2 + 1e-9);
        CHECK(game.cpuY() >= meshdeck::Pong::kPaddleH / 2 - 1e-9);
        CHECK(game.cpuY() <= meshdeck::Pong::kHeight - meshdeck::Pong::kPaddleH / 2 + 1e-9);
        if (game.phase() == meshdeck::Pong::Phase::Over) break;
    }
}

TEST_CASE("pong: a player who tracks the ball scores some points and the match still finishes") {
    meshdeck::Pong game(23);
    double seconds = playMatch(game, true, 1200);
    REQUIRE(seconds > 0);
    CHECK(game.playerScore() + game.cpuScore() >= meshdeck::Pong::kWinScore);
    CHECK(game.playerScore() > 0);
}

TEST_CASE("pong: the same seed plays the same match") {
    meshdeck::Pong a(99), b(99);
    a.start();
    b.start();
    for (int i = 0; i < 60 * 30; i++) {
        a.step(1.0 / 60, 1);
        b.step(1.0 / 60, 1);
    }
    CHECK(a.ballX() == doctest::Approx(b.ballX()));
    CHECK(a.ballY() == doctest::Approx(b.ballY()));
    CHECK(a.cpuScore() == b.cpuScore());
}

TEST_CASE("sea battle: both fleets are placed with every ship's full length on the board") {
    meshdeck::SeaBattle game(42);
    for (int round = 0; round < 20; round++) {
        game.start();
        for (const meshdeck::SeaBattle::Sea* sea : {&game.player(), &game.cpu()}) {
            int cells = 0;
            int counts[meshdeck::SeaBattle::kShips] = {0};
            for (int i = 0; i < meshdeck::SeaBattle::kCells; i++) {
                if (sea->ship[i] < 0) continue;
                cells++;
                counts[sea->ship[i]]++;
            }
            CHECK(cells == meshdeck::SeaBattle::kFleetCells);
            for (int s = 0; s < meshdeck::SeaBattle::kShips; s++)
                CHECK(counts[s] == meshdeck::SeaBattle::kFleet[s]);
        }
    }
}

TEST_CASE("sea battle: a shot is a miss, a hit or a sinking, and a repeat is refused") {
    meshdeck::SeaBattle game(7);
    game.start();
    // Find one of your fleet's squares on the CPU's sea by searching the placement from the other side:
    // shoot every square until the first hit, which must come, and check the reply each time.
    int shotsFired = 0;
    bool sawHit = false, sawMiss = false;
    for (int i = 0; i < meshdeck::SeaBattle::kCells && game.phase() == meshdeck::SeaBattle::Phase::Playing; i++) {
        auto result = game.fire(i);
        CHECK(result != meshdeck::SeaBattle::Shot::Repeat);
        shotsFired++;
        if (game.cpu().ship[i] >= 0) {
            CHECK(game.cpu().hits[game.cpu().ship[i]] > 0);
            sawHit = true;
        } else {
            CHECK(result == meshdeck::SeaBattle::Shot::Miss);
            sawMiss = true;
        }
        CHECK(game.fire(i) == meshdeck::SeaBattle::Shot::Repeat);   // the same square again
    }
    CHECK(sawHit);
    CHECK(sawMiss);
    CHECK(game.shots() == shotsFired);
}

TEST_CASE("sea battle: a full game ends with one side's fleet sunk, and the CPU never fires at the same square twice") {
    meshdeck::SeaBattle game(123);
    game.start();
    int cpuShots[meshdeck::SeaBattle::kCells] = {0};
    int turns = 0;
    while (game.phase() == meshdeck::SeaBattle::Phase::Playing && turns < 200) {
        // A simple player: the next square not yet fired at.
        int next = 0;
        while (game.cpu().fired[next]) next++;
        game.fire(next);
        if (game.lastCpuShot() >= 0) cpuShots[game.lastCpuShot()]++;
        turns++;
    }
    REQUIRE(game.phase() == meshdeck::SeaBattle::Phase::Over);
    for (int i = 0; i < meshdeck::SeaBattle::kCells; i++) CHECK(cpuShots[i] <= 1);
    CHECK((game.cpu().cellsLeft == 0) != (game.player().cellsLeft == 0));   // exactly one side has lost
    CHECK(game.points() == 200 - game.shots());
}

TEST_CASE("sea battle: a new game starts again from zero") {
    meshdeck::SeaBattle game(9);
    game.start();
    game.fire(0);
    game.start();
    CHECK(game.phase() == meshdeck::SeaBattle::Phase::Playing);
    CHECK(game.shots() == 0);
    CHECK(game.lastCpuShot() == -1);
    CHECK_FALSE(game.cpu().fired[0]);
}

namespace {

// All 52 cards, each exactly once, across the stock, waste, foundations and columns.
bool allCardsOnce(const meshdeck::Klondike& k) {
    int seen[4][14] = {{0}};
    int total = 0;
    auto count = [&](const std::vector<meshdeck::Klondike::Card>& pile) {
        for (const auto& c : pile) {
            seen[c.suit][c.rank]++;
            total++;
        }
    };
    count(k.stock());
    count(k.waste());
    for (int f = 0; f < meshdeck::Klondike::kFoundations; f++) count(k.foundation(f));
    for (int c = 0; c < meshdeck::Klondike::kColumns; c++) count(k.column(c));
    if (total != 52) return false;
    for (int s = 0; s < 4; s++)
        for (int r = 1; r <= 13; r++)
            if (seen[s][r] != 1) return false;
    return true;
}

// One legal move, if there is one. Tries the moves a player could make, in a fixed order.
bool playAnyMove(meshdeck::Klondike& k) {
    using meshdeck::Klondike;
    for (int c = 0; c < Klondike::kColumns; c++) {
        const auto& col = k.column(c);
        for (int from = 0; from < (int)col.size(); from++) {
            if (!col[from].up) continue;
            for (int dst = 0; dst < Klondike::kColumns; dst++)
                if (k.moveRun(c, from, dst)) return true;
            if (from == (int)col.size() - 1)
                for (int f = 0; f < Klondike::kFoundations; f++)
                    if (k.columnToFoundation(c, f)) return true;
        }
    }
    for (int dst = 0; dst < Klondike::kColumns; dst++)
        if (k.wasteToColumn(dst)) return true;
    for (int f = 0; f < Klondike::kFoundations; f++)
        if (k.wasteToFoundation(f)) return true;
    for (int f = 0; f < Klondike::kFoundations; f++)
        for (int dst = 0; dst < Klondike::kColumns; dst++)
            if (k.foundationToColumn(f, dst)) return true;
    return false;
}

} // namespace

TEST_CASE("klondike: the deal has 52 cards, seven columns of one to seven, and 24 in the stock") {
    meshdeck::Klondike game(3);
    CHECK(allCardsOnce(game));
    CHECK(game.stock().size() == 24);
    CHECK(game.waste().empty());
    for (int c = 0; c < meshdeck::Klondike::kColumns; c++) {
        CHECK(game.column(c).size() == (size_t)(c + 1));
        CHECK(game.column(c).back().up);                                // the top card is face up
        for (size_t i = 0; i + 1 < game.column(c).size(); i++) CHECK_FALSE(game.column(c)[i].up);
    }
    CHECK(game.moves() == 0);
    CHECK_FALSE(game.won());
}

TEST_CASE("klondike: draw turns one card onto the waste, and an empty stock recycles the waste") {
    meshdeck::Klondike game(4);
    for (int i = 0; i < 24; i++) {
        REQUIRE(game.draw());
        CHECK(game.waste().back().up);
    }
    CHECK(game.stock().empty());
    CHECK(game.waste().size() == 24);
    REQUIRE(game.draw());                                              // recycle
    CHECK(game.waste().empty());
    CHECK(game.stock().size() == 24);
    CHECK_FALSE(game.stock().back().up);                               // turned over again
    CHECK(allCardsOnce(game));
    CHECK(game.moves() == 0);                                          // draws are not moves
}

TEST_CASE("klondike: undo takes back the last draw or move, and an illegal move changes nothing") {
    meshdeck::Klondike game(8);
    REQUIRE(game.draw());
    REQUIRE(game.draw());
    CHECK(game.waste().size() == 2);
    CHECK(game.undo());
    CHECK(game.waste().size() == 1);
    CHECK(game.undo());
    CHECK(game.waste().empty());
    CHECK_FALSE(game.undo());                                            // nothing left to take back
    // A move that the rules refuse changes nothing: a column can't be moved onto itself.
    meshdeck::Klondike fresh(8);
    CHECK_FALSE(fresh.moveRun(6, 6, 6));
    CHECK_FALSE(fresh.moveRun(0, 0, 0));
    CHECK(allCardsOnce(fresh));
    CHECK(fresh.moves() == 0);
    CHECK(fresh.column(6).size() == 7);
}

TEST_CASE("klondike: random legal play keeps all 52 cards in place, and some cards reach the foundations") {
    meshdeck::Klondike game(77);
    int bestFoundation = 0;
    for (int turn = 0; turn < 3000; turn++) {
        if (!playAnyMove(game) && !game.draw()) break;
        REQUIRE(allCardsOnce(game));
        bestFoundation = std::max(bestFoundation, game.cardsOnFoundations());
    }
    CHECK(bestFoundation > 0);
    CHECK(game.moves() >= 0);
}

TEST_CASE("best scores: only a higher score is a new best, and it survives a restart") {
    std::string dir = testutil::scratch("games-scores");
    std::string path = dir + "/scores.txt";
    {
        meshdeck::HighScores scores;
        REQUIRE(scores.load(path));                       // no file yet: empty, not an error
        CHECK(scores.best("RALLY") == 0);
        CHECK(scores.record("RALLY", 3));
        CHECK_FALSE(scores.record("RALLY", 2));           // lower: not a new best
        CHECK_FALSE(scores.record("RALLY", 3));           // equal: not a new best
        CHECK(scores.record("RALLY", 5));
        CHECK(scores.record("SEA BATTLE", 12));
        REQUIRE(scores.save());
    }
    meshdeck::HighScores again;
    REQUIRE(again.load(path));
    CHECK(again.best("RALLY") == 5);
    CHECK(again.best("SEA BATTLE") == 12);
    CHECK(again.best("KLONDIKE") == 0);
}

TEST_CASE("best scores: a damaged line is skipped, the others are kept") {
    std::string dir = testutil::scratch("games-scores-damaged");
    std::string path = dir + "/scores.txt";
    std::string text = "RALLY|4\nno bar here\nSEA BATTLE|x9\n|7\nKLONDIKE|-3\nSEA BATTLE|12\n";
    FILE* f = fopen(path.c_str(), "wb");
    REQUIRE(f);
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    meshdeck::HighScores scores;
    REQUIRE(scores.load(path));
    CHECK(scores.best("RALLY") == 4);
    CHECK(scores.best("SEA BATTLE") == 12);
    CHECK(scores.best("KLONDIKE") == 0);
}

TEST_CASE("pong: a new match starts from zero after one ends") {
    meshdeck::Pong game(5);
    REQUIRE(playMatch(game, false, 600) > 0);
    REQUIRE(game.phase() == meshdeck::Pong::Phase::Over);
    game.start();
    CHECK(game.phase() == meshdeck::Pong::Phase::Point);
    CHECK(game.playerScore() == 0);
    CHECK(game.cpuScore() == 0);
}
