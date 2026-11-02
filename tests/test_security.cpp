// test_security.cpp - hashing against published test vectors, and the PIN rules.
#include <map>
#include <string>
#include "doctest.h"
#include "kernel/sha256.h"
#include "services/security.h"

using meshdeck::Security;

static std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

TEST_CASE("sha256: FIPS 180-4 examples") {
    uint8_t h[32];
    meshdeck::sha256("", 0, h);
    CHECK(hex(h, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    meshdeck::sha256("abc", 3, h);
    CHECK(hex(h, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   // spans two blocks
    meshdeck::sha256(two, 56, h);
    CHECK(hex(h, 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("hmac-sha256: RFC 4231 test case 2") {
    uint8_t h[32];
    const char* msg = "what do ya want for nothing?";
    meshdeck::hmacSha256((const uint8_t*)"Jefe", 4, (const uint8_t*)msg, 28, h);
    CHECK(hex(h, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("pbkdf2-sha256: published vectors") {
    uint8_t h[32];
    auto run = [&](uint32_t c) {
        meshdeck::pbkdf2Sha256((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, c, h, 32);
        return hex(h, 32);
    };
    CHECK(run(1) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(run(2) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK(run(4096) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
}

namespace {
uint32_t clockMs = 5000;
class SecretPlatform : public meshdeck::Platform {
public:
    std::map<std::string, std::string> store;
    const char* name() const override { return "Test"; }
    uint32_t uptimeMs() const override { return clockMs; }
    bool loadSecret(const char* n, std::string& d) override {
        auto it = store.find(n);
        if (it == store.end()) return false;
        d = it->second;
        return true;
    }
    bool saveSecret(const char* n, const std::string& d) override { store[n] = d; return true; }
    void randomBytes(uint8_t* out, size_t len) override {
        for (size_t i = 0; i < len; i++) out[i] = uint8_t(i * 29 + 7);
    }
};
} // namespace

TEST_CASE("security: PIN rules") {
    std::string e;
    CHECK_FALSE(Security::validPin("123", e));
    CHECK_FALSE(Security::validPin("1234567890123", e));
    CHECK_FALSE(Security::validPin("12a4", e));
    CHECK(Security::validPin("0000", e));
    CHECK(Security::delayFor(3) == 0);
    CHECK(Security::delayFor(4) == 30);
    CHECK(Security::delayFor(5) == 60);
    CHECK(Security::delayFor(7) == 900);
    CHECK(Security::delayFor(8) == 3600);
}

TEST_CASE("security: set, lock, wrong tries, waits, unlock") {
    SecretPlatform p;
    Security sec(p);
    sec.begin();
    CHECK_FALSE(sec.pinSet());
    CHECK_FALSE(sec.locked());
    sec.lock();
    CHECK_FALSE(sec.locked());   // no PIN, no lock

    std::string err;
    REQUIRE(sec.setPin("", "2468", err));
    CHECK(p.store.at("security").size() == 60);
    CHECK(p.store.at("security").find("2468") == std::string::npos);   // never stored as typed
    sec.lock();
    CHECK(sec.locked());

    for (int i = 0; i < 3; i++) CHECK(sec.unlock("1111") == Security::Result::Wrong);
    CHECK(sec.waitSeconds() == 0);   // three free tries
    CHECK(sec.unlock("1111") == Security::Result::Wrong);
    CHECK(sec.waitSeconds() == 30);
    CHECK(sec.unlock("2468") == Security::Result::Waiting);   // even the right PIN must wait
    CHECK(sec.locked());
    clockMs += 30000;
    CHECK(sec.unlock("2468") == Security::Result::Ok);
    CHECK_FALSE(sec.locked());
    CHECK(sec.failures() == 0);
}

TEST_CASE("security: a restart keeps the PIN, the lock and the failure count") {
    SecretPlatform p;
    std::string err;
    {
        Security sec(p);
        sec.begin();
        REQUIRE(sec.setPin("", "13579", err));
        for (int i = 0; i < 4; i++) sec.unlock("0000");
    }
    Security again(p);
    again.begin();
    CHECK(again.pinSet());
    CHECK(again.locked());
    CHECK(again.failures() == 4);
    CHECK(again.waitSeconds() == 30);   // the wait starts again from the restart
}

TEST_CASE("security: change and remove need the current PIN") {
    SecretPlatform p;
    Security sec(p);
    sec.begin();
    std::string err;
    REQUIRE(sec.setPin("", "1234", err));
    CHECK_FALSE(sec.setPin("9999", "5678", err));
    CHECK(err == "CURRENT PIN IS WRONG");
    CHECK(sec.setPin("1234", "5678", err));
    CHECK_FALSE(sec.removePin("1234", err));
    CHECK(sec.removePin("5678", err));
    CHECK_FALSE(sec.pinSet());
    Security fresh(p);
    fresh.begin();
    CHECK_FALSE(fresh.locked());
}
