// security.cpp - see security.h.
#include "services/security.h"
#include <string.h>
#include "kernel/sha256.h"

namespace meshdeck {

void Security::begin() {
    std::string data;
    if (!platform_.loadSecret(kRecordName, data) || !decode(data)) {
        pinSet_ = false;
        failures_ = 0;
    }
    locked_ = pinSet_;
    lastFailMs_ = platform_.uptimeMs();   // a restart starts any pending wait again, never skips it
}

uint32_t Security::delayFor(uint32_t f) {
    if (f <= 3) return 0;
    static const uint32_t steps[] = {30, 60, 300, 900};
    return f - 4 < 4 ? steps[f - 4] : 3600;
}

uint32_t Security::waitSeconds() const {
    uint32_t need = delayFor(failures_) * 1000u, gone = platform_.uptimeMs() - lastFailMs_;
    return gone >= need ? 0 : (need - gone + 999) / 1000;
}

bool Security::validPin(const std::string& pin, std::string& error) {
    if (pin.size() < 4 || pin.size() > 12) { error = "PIN MUST BE 4 TO 12 DIGITS"; return false; }
    for (char c : pin)
        if (c < '0' || c > '9') { error = "PIN MUST BE DIGITS ONLY"; return false; }
    return true;
}

bool Security::check(const std::string& pin) const {
    if (!pinSet_) return true;
    uint8_t h[32];
    pbkdf2Sha256((const uint8_t*)pin.data(), pin.size(), salt_, sizeof salt_, iterations_, h, sizeof h);
    return equalConstantTime(h, hash_, sizeof h);
}

Security::Result Security::attempt(const std::string& pin) {
    if (waitSeconds()) return Result::Waiting;
    if (!check(pin)) {
        failures_++;
        lastFailMs_ = platform_.uptimeMs();
        save();
        return Result::Wrong;
    }
    if (failures_) {
        failures_ = 0;
        save();
    }
    return Result::Ok;
}

void Security::lock() {
    if (pinSet_) locked_ = true;
}

Security::Result Security::unlock(const std::string& pin) {
    Result r = attempt(pin);
    if (r == Result::Ok) locked_ = false;
    return r;
}

bool Security::setPin(const std::string& current, const std::string& next, std::string& error) {
    if (pinSet_) {
        Result r = attempt(current);
        if (r == Result::Waiting) { error = "WAIT " + std::to_string(waitSeconds()) + " S BEFORE TRYING AGAIN"; return false; }
        if (r != Result::Ok) { error = "CURRENT PIN IS WRONG"; return false; }
    }
    if (!validPin(next, error)) return false;
    platform_.randomBytes(salt_, sizeof salt_);
    iterations_ = kIterations;
    pbkdf2Sha256((const uint8_t*)next.data(), next.size(), salt_, sizeof salt_, iterations_, hash_, sizeof hash_);
    pinSet_ = true;
    failures_ = 0;
    if (!save()) { error = "PIN COULDN'T BE SAVED"; return false; }
    return true;
}

bool Security::removePin(const std::string& current, std::string& error) {
    if (!pinSet_) { error = "NO PIN IS SET"; return false; }
    Result r = attempt(current);
    if (r == Result::Waiting) { error = "WAIT " + std::to_string(waitSeconds()) + " S BEFORE TRYING AGAIN"; return false; }
    if (r != Result::Ok) { error = "PIN IS WRONG"; return false; }
    pinSet_ = locked_ = false;
    failures_ = 0;
    memset(salt_, 0, sizeof salt_);
    memset(hash_, 0, sizeof hash_);
    if (!save()) { error = "COULDN'T BE SAVED"; return false; }
    return true;
}

bool Security::save() { return platform_.saveSecret(kRecordName, encode()); }

static void putU32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; i++) s += char((v >> (8 * i)) & 0xFF);
}
static uint32_t getU32(const std::string& s, size_t at) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= uint32_t((uint8_t)s[at + i]) << (8 * i);
    return v;
}

std::string Security::encode() const {
    std::string s = "BS";
    s += char(1);
    s += char(pinSet_ ? 1 : 0);
    putU32(s, iterations_);
    putU32(s, failures_);
    s.append((const char*)salt_, sizeof salt_);
    s.append((const char*)hash_, sizeof hash_);
    return s;
}

bool Security::decode(const std::string& d) {
    if (d.size() != 60 || d[0] != 'B' || d[1] != 'S' || d[2] != 1) return false;
    pinSet_ = d[3] & 1;
    iterations_ = getU32(d, 4);
    failures_ = getU32(d, 8);
    memcpy(salt_, d.data() + 12, sizeof salt_);
    memcpy(hash_, d.data() + 28, sizeof hash_);
    if (iterations_ < 1000 || iterations_ > 1000000) iterations_ = kIterations;
    return true;
}

} // namespace meshdeck
