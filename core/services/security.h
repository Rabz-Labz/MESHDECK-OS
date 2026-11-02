// security.h - the PIN lock.
//
// Rules (unchanged from MESHDECK 1.x):
//   - The PIN is 4 to 12 digits. It is never stored: only PBKDF2-HMAC-SHA256 of it (20,000 rounds,
//     random 16-byte salt), so reading the record doesn't reveal the PIN.
//   - With a PIN set, the deck starts locked, locks after the chosen idle time, and on Ctrl+L.
//   - The first 3 wrong PINs in a row cost nothing. Each further one makes you wait: 30 s, 1 min,
//     5 min, 15 min, then 1 h each time. The count is saved, so restarting doesn't skip the wait
//     (the wait starts again from the restart). Nothing is ever wiped automatically.
//   - User data on the SD card is not encrypted (decided in 1.x; noted in docs/security.md).
//
// The record is kept by the platform away from the SD card (Tab5: internal flash), so removing
// the card can't remove the PIN. Its layout, 60 bytes, little-endian:
//   0 "BS"   2 version (1)   3 flags (bit 0: PIN set)   4 iterations (u32)   8 failures (u32)
//   12 salt (16 bytes)   28 hash (32 bytes)
#pragma once
#include <stdint.h>
#include <string>
#include "platform/platform.h"

namespace meshdeck {

class Security {
public:
    enum class Result { Ok, Wrong, Waiting };
    static constexpr uint32_t kIterations = 20000;
    static constexpr const char* kRecordName = "security";

    explicit Security(Platform& platform) : platform_(platform) {}
    void begin();                      // loads the record; locked from the start when a PIN is set

    bool pinSet() const { return pinSet_; }
    bool locked() const { return locked_; }
    uint32_t failures() const { return failures_; }
    uint32_t waitSeconds() const;      // until another PIN may be tried (0 = now)
    // How many more wrong PINs are free before waits begin (0 once they have).
    uint32_t freeTriesLeft() const { return failures_ >= 3 ? 0 : 3 - failures_; }

    void lock();                       // only does anything when a PIN is set
    Result unlock(const std::string& pin);
    // Checks the PIN for a protected action (change or remove it). Counts as an attempt.
    Result verify(const std::string& pin) { return attempt(pin); }

    static bool validPin(const std::string& pin, std::string& error);
    // Sets a new PIN. With a PIN already set, `current` must be right. Error text says why not.
    bool setPin(const std::string& current, const std::string& next, std::string& error);
    bool removePin(const std::string& current, std::string& error);

    static uint32_t delayFor(uint32_t failures);   // wait (s) after the nth wrong PIN in a row

private:
    bool check(const std::string& pin) const;
    Result attempt(const std::string& pin);
    bool save();
    std::string encode() const;
    bool decode(const std::string& data);

    Platform& platform_;
    bool pinSet_ = false, locked_ = false;
    uint32_t iterations_ = kIterations, failures_ = 0;
    uint8_t salt_[16] = {0}, hash_[32] = {0};
    uint32_t lastFailMs_ = 0;
};

} // namespace meshdeck
