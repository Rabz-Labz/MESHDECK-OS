// sha256.h - SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 2104) and PBKDF2-HMAC-SHA256 (RFC 8018).
//
// Portable C++, so the PIN check is the same code on the Tab5, in the simulator and in the tests
// (checked against the published test vectors in tests/test_security.cpp). Also used to check
// that uploaded files arrived intact.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace meshdeck {

// Incremental hashing: update() any number of times, then finish() once.
struct Sha256 {
    uint32_t h[8];
    uint64_t bits;
    uint8_t block[64];
    size_t used;

    Sha256();
    void update(const void* data, size_t len);
    void finish(uint8_t out[32]);
};

void sha256(const void* data, size_t len, uint8_t out[32]);
void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t out[32]);
void pbkdf2Sha256(const uint8_t* pass, size_t passLen, const uint8_t* salt, size_t saltLen, uint32_t iterations,
                  uint8_t* out, size_t outLen);
// Compares without stopping early, so timing doesn't hint at how much matched.
bool equalConstantTime(const uint8_t* a, const uint8_t* b, size_t len);

} // namespace meshdeck
