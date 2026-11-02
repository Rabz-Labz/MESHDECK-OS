// pong.cpp - see pong.h. The ball speeds up a little with every return, and it leaves the paddle at
// an angle that depends on where it was hit. The CPU follows the ball at a limited speed and aims
// slightly off, so a fast player can win.
#include "games/pong.h"
#include <math.h>
#include <algorithm>

namespace meshdeck {
namespace {

const double kPlayerSpeed = 620;     // units per second
const double kCpuSpeed = 380;        // the CPU is slower than a quick hand
const double kStartSpeed = 420;
const double kMaxSpeed = 900;
const double kSpeedUp = 1.05;        // each return is this much faster
const double kServeDelay = 1.0;      // seconds between points
const double kMaxStep = 0.05;        // a long frame is cut to this, so nothing jumps through a paddle

double clampTo(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

} // namespace

Pong::Pong(uint32_t seed) : rng_(seed ? seed : 1) {}

uint32_t Pong::nextRandom() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

double Pong::unit() { return nextRandom() / 4294967296.0; }

void Pong::start() {
    if (phase_ == Phase::Over) {
        playerScore_ = cpuScore_ = 0;
        playerY_ = cpuY_ = kHeight / 2;
    }
    if (phase_ != Phase::Ready && phase_ != Phase::Over) return;
    serve(true);   // the first serve goes to the player
}

void Pong::serve(bool towardPlayer) {
    speed_ = kStartSpeed;
    ballX_ = kWidth / 2;
    ballY_ = kHeight / 2;
    // A serve leaves at a random angle, up to half the speed up or down.
    ballVy_ = (unit() - 0.5) * speed_;
    double vx = sqrt(std::max(0.0, speed_ * speed_ - ballVy_ * ballVy_));
    ballVx_ = towardPlayer ? -vx : vx;
    cpuError_ = (unit() - 0.5) * 110;  // the CPU's aim is off by up to 55 units for this point
    pointTimer_ = kServeDelay;
    phase_ = Phase::Point;
}

void Pong::step(double dt, int move) {
    if (phase_ == Phase::Ready || phase_ == Phase::Over) return;
    dt = clampTo(dt, 0, kMaxStep);
    const double half = kPaddleH / 2;

    // The player's paddle follows the keys; it can move during a pause too.
    playerY_ = clampTo(playerY_ + clampTo(move, -1, 1) * kPlayerSpeed * dt, half, kHeight - half);

    // The CPU heads for the ball while the ball comes its way, and back to the middle otherwise.
    double target = ballVx_ > 0 ? ballY_ + cpuError_ : kHeight / 2;
    double step = clampTo(target - cpuY_, -kCpuSpeed * dt, kCpuSpeed * dt);
    cpuY_ = clampTo(cpuY_ + step, half, kHeight - half);

    if (phase_ == Phase::Point) {
        pointTimer_ -= dt;
        if (pointTimer_ <= 0) phase_ = Phase::Playing;
        return;
    }

    // The ball moves, bounces off the top and bottom, and is hit by a paddle if it is in its path.
    ballX_ += ballVx_ * dt;
    ballY_ += ballVy_ * dt;
    if (ballY_ < kBall) { ballY_ = kBall; ballVy_ = -ballVy_; }
    if (ballY_ > kHeight - kBall) { ballY_ = kHeight - kBall; ballVy_ = -ballVy_; }

    if (ballVx_ < 0 && ballX_ - kBall <= kPaddleInset + kPaddleW && ballX_ >= kPaddleInset &&
        fabs(ballY_ - playerY_) <= half + kBall) {
        bounceOffPaddle(true);
    }
    if (ballVx_ > 0 && ballX_ + kBall >= kWidth - kPaddleInset - kPaddleW && ballX_ <= kWidth - kPaddleInset &&
        fabs(ballY_ - cpuY_) <= half + kBall) {
        bounceOffPaddle(false);
    }

    // A ball past either edge is a point for the other side.
    if (ballX_ < 0) scorePoint(false);
    if (ballX_ > kWidth) scorePoint(true);
}

void Pong::bounceOffPaddle(bool leftSide) {
    const double paddleY = leftSide ? playerY_ : cpuY_;
    double offset = clampTo((ballY_ - paddleY) / (kPaddleH / 2), -1, 1);   // -1 top edge, +1 bottom edge
    speed_ = std::min(speed_ * kSpeedUp, kMaxSpeed);
    // The angle depends on where the ball hit, plus a little chance, so a ball that always meets the
    // middle of a paddle still drifts up or down and eventually gets past.
    ballVy_ = offset * speed_ * 0.8 + (unit() - 0.5) * 0.4 * speed_;
    ballVy_ = clampTo(ballVy_, -0.9 * speed_, 0.9 * speed_);
    double vx = sqrt(std::max(speed_ * speed_ - ballVy_ * ballVy_, (0.3 * speed_) * (0.3 * speed_)));
    ballVx_ = leftSide ? vx : -vx;
    ballX_ = leftSide ? kPaddleInset + kPaddleW + kBall : kWidth - kPaddleInset - kPaddleW - kBall;
}

void Pong::scorePoint(bool forPlayer) {
    if (forPlayer) playerScore_++;
    else cpuScore_++;
    if (playerScore_ >= kWinScore || cpuScore_ >= kWinScore) {
        phase_ = Phase::Over;
        return;
    }
    serve(!forPlayer);   // the next serve goes toward the side that just conceded
}

} // namespace meshdeck
