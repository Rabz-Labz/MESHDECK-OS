// pong.h - RALLY: pong against the CPU. The rules live here with no screen code, so they can be
// tested directly. The field is 1000 x 600 units; the screen scales it. The player's paddle is on
// the left and is moved by the caller; the CPU's paddle is on the right.
#pragma once
#include <stdint.h>

namespace meshdeck {

class Pong {
public:
    static constexpr double kWidth = 1000, kHeight = 600;
    static constexpr double kPaddleH = 120, kPaddleW = 16, kPaddleInset = 24, kBall = 8;
    static constexpr int kWinScore = 7;   // first to this many points wins the match

    enum class Phase { Ready, Playing, Point, Over };

    explicit Pong(uint32_t seed = 1);
    void start();                          // Ready or Over -> Playing (Over starts a new match)
    // Moves the game on by dt seconds. move is the player's paddle: -1 up, 0 still, +1 down.
    void step(double dt, int move);

    Phase phase() const { return phase_; }
    double ballX() const { return ballX_; }
    double ballY() const { return ballY_; }
    double playerY() const { return playerY_; }   // the paddle's centre, in units
    double cpuY() const { return cpuY_; }
    int playerScore() const { return playerScore_; }
    int cpuScore() const { return cpuScore_; }
    bool playerWon() const { return playerScore_ > cpuScore_; }   // meaningful once the match is Over

private:
    uint32_t nextRandom();                 // xorshift: deterministic for a given seed
    double unit();                         // a random number in [0, 1)
    void serve(bool towardPlayer);         // puts the ball in the middle and sends it off
    void bounceOffPaddle(bool leftSide);
    void scorePoint(bool forPlayer);

    Phase phase_ = Phase::Ready;
    uint32_t rng_;
    double ballX_ = kWidth / 2, ballY_ = kHeight / 2;
    double ballVx_ = 0, ballVy_ = 0, speed_ = 0;
    double playerY_ = kHeight / 2, cpuY_ = kHeight / 2;
    double cpuError_ = 0;                  // the CPU aims a little off, so it can be beaten
    double pointTimer_ = 0;                // seconds before the next serve
    int playerScore_ = 0, cpuScore_ = 0;
};

} // namespace meshdeck
