// test_mesh.cpp - the mesh service against the fake radio (which speaks the real serial API),
// plus contacts and read marks.
#include <string>
#include "doctest.h"
#include "fake_radio.h"
#include "kernel/storage.h"
#include "services/contacts.h"
#include "services/mesh.h"
#include "test_util.h"

using meshdeck::MeshService;

namespace {

struct RadioPlatform : meshdeck::Platform {
    FakeRadio radio;
    uint32_t ms = 1000;
    const char* name() const override { return "Test"; }
    uint32_t uptimeMs() const override { return ms; }
    bool radioPresent() const override { return radio.present; }
    int radioRead(uint8_t* buf, int max) override { return radio.read(buf, max); }
    bool radioWrite(const uint8_t* data, int len) override { return radio.write(data, len); }
    void randomBytes(uint8_t* out, size_t len) override {
        for (size_t i = 0; i < len; i++) out[i] = uint8_t(i * 41 + 3);
    }
};

// Runs the radio and the mesh for a while, 100 ms per step.
void run(RadioPlatform& p, MeshService& mesh, uint32_t forMs) {
    for (uint32_t t = 0; t < forMs; t += 100) {
        p.ms += 100;
        p.radio.tick(p.ms);
        mesh.tick();
    }
}

MeshService::Thread channel(int c) {
    MeshService::Thread t;
    t.channel = c;
    return t;
}

MeshService::Thread direct(uint32_t node) {
    MeshService::Thread t;
    t.direct = true;
    t.node = node;
    return t;
}

} // namespace

TEST_CASE("mesh: connects, learns the radio's nodes, region and channels") {
    RadioPlatform p;
    p.radio.chatter = false;
    MeshService mesh(p, [] { return (int64_t)1793542200; });
    mesh.begin("");
    CHECK(mesh.link() == MeshService::Link::None);
    run(p, mesh, 500);
    REQUIRE(mesh.link() == MeshService::Link::Ready);
    CHECK(mesh.myNum() == 0x1D01);
    CHECK(mesh.region() == "EU_868");
    CHECK(mesh.nodes().size() == 3);
    CHECK(mesh.nodeName(0x1A01) == "TS01");
    CHECK(mesh.nodes().at(0x1A02).hops == 1);
    CHECK(mesh.channels().size() == 2);
    CHECK(mesh.channelName(1) == "FIELD-A");
    CHECK(mesh.channelName(0) == "PRIMARY");
}

namespace {

int timesSent(const RadioPlatform& p, const std::string& text) {
    int n = 0;
    for (const std::string& s : p.radio.sentTexts)
        if (s == text) n++;
    return n;
}

const MeshService::Message* find(const MeshService& mesh, const std::string& text) {
    for (const MeshService::Message& m : mesh.messages())
        if (m.text == text) return &m;
    return nullptr;
}

} // namespace

TEST_CASE("mesh: RX ONLY holds messages until NORMAL, and RADIO OFF stops the radio") {
    RadioPlatform p;
    p.radio.chatter = false;
    MeshService mesh(p, [] { return (int64_t)1793542200; });
    mesh.begin("");
    run(p, mesh, 500);
    REQUIRE(mesh.link() == MeshService::Link::Ready);
    CHECK(mesh.emcon() == MeshService::Emcon::Normal);

    mesh.setEmcon(MeshService::Emcon::RxOnly);
    std::string error;
    REQUIRE(mesh.send("HOLD TEST", channel(1), error));
    run(p, mesh, 3000);
    CHECK(timesSent(p, "HOLD TEST") == 0);
    REQUIRE(find(mesh, "HOLD TEST") != nullptr);
    CHECK(find(mesh, "HOLD TEST")->state == MeshService::State::Queued);
    CHECK(find(mesh, "HOLD TEST")->note == "HELD: EMCON");

    mesh.setEmcon(MeshService::Emcon::Normal);
    run(p, mesh, 3000);
    CHECK(timesSent(p, "HOLD TEST") == 1);
    CHECK(find(mesh, "HOLD TEST")->state != MeshService::State::Queued);

    mesh.setEmcon(MeshService::Emcon::RadioOff);
    run(p, mesh, 500);
    CHECK(std::string(mesh.linkText()) == "RADIO OFF (EMCON)");
    CHECK(mesh.link() == MeshService::Link::None);
    REQUIRE(mesh.send("OFF TEST", channel(1), error));
    run(p, mesh, 3000);
    CHECK(timesSent(p, "OFF TEST") == 0);

    mesh.setEmcon(MeshService::Emcon::Normal);
    run(p, mesh, 3000);
    REQUIRE(mesh.link() == MeshService::Link::Ready);
    CHECK(timesSent(p, "OFF TEST") == 1);
}

TEST_CASE("mesh: a big start-up burst (many nodes, log text) loses nothing") {
    // Like the real C6L: its configuration with 40 nodes arrives in one go, 16 KB and more,
    // before MESHDECK reads any of it. The first frames (node number, region, channels) must survive.
    RadioPlatform p;
    p.radio.chatter = false;
    MeshService mesh(p, [] { return (int64_t)0; });
    mesh.begin("");
    mesh.tick();                       // sends want_config; the fake radio answers at once
    // Pad the radio's output with lots of log text and extra node frames before it is read.
    for (int i = 0; i < 40; i++) p.radio.receiveText(std::string(150, 'x') + std::to_string(i));
    run(p, mesh, 200);
    CHECK(mesh.link() == MeshService::Link::Ready);
    CHECK(mesh.myNum() == 0x1D01);
    CHECK(mesh.region() == "EU_868");
    CHECK(mesh.channels().size() == 2);
    CHECK(mesh.messages().size() == 40);
}

TEST_CASE("mesh: a message goes QUEUED, SENT, then DELIVERED with the ack") {
    RadioPlatform p;
    p.radio.chatter = false;
    MeshService mesh(p, [] { return (int64_t)1793542200; });
    mesh.begin("");
    run(p, mesh, 500);
    std::string err;
    REQUIRE(mesh.send("  meet at the hall  ", direct(0x1A01), err));
    CHECK(mesh.messages().back().state == MeshService::State::Queued);
    CHECK(mesh.messages().back().text == "meet at the hall");   // trimmed
    run(p, mesh, 300);
    CHECK(mesh.messages().back().state == MeshService::State::Sent);
    REQUIRE(p.radio.sentTexts.size() == 1);
    CHECK(p.radio.sentTexts[0] == "meet at the hall");
    run(p, mesh, 1500);
    CHECK(mesh.messages().back().state == MeshService::State::Delivered);
    CHECK(mesh.messages().back().note == "ACK FROM TS01");
}

TEST_CASE("mesh: refusals, empty and over-long messages are reported honestly") {
    RadioPlatform p;
    p.radio.chatter = false;
    MeshService mesh(p, [] { return (int64_t)0; });
    mesh.begin("");
    run(p, mesh, 500);
    std::string err;
    CHECK_FALSE(mesh.send("   ", channel(0), err));
    CHECK(err == "MESSAGE IS EMPTY");
    CHECK_FALSE(mesh.send(std::string(201, 'x'), channel(0), err));
    REQUIRE(mesh.send("FAILTEST", channel(1), err));
    run(p, mesh, 2000);
    CHECK(mesh.messages().back().state == MeshService::State::Failed);
    CHECK(mesh.messages().back().note == "NO ACK AFTER RETRIES");
}

TEST_CASE("mesh: nothing is sent until the radio is ready, and a missing radio says so") {
    RadioPlatform p;
    p.radio.present = false;
    p.radio.chatter = false;
    MeshService mesh(p, [] { return (int64_t)0; });
    mesh.begin("");
    std::string err;
    REQUIRE(mesh.send("hello", channel(0), err));
    run(p, mesh, 1000);
    CHECK(mesh.link() == MeshService::Link::None);
    CHECK(std::string(mesh.linkText()) == "NO RADIO");
    CHECK(mesh.messages().back().state == MeshService::State::Queued);
    p.radio.present = true;   // plugged in: it goes out
    run(p, mesh, 3000);
    CHECK(mesh.messages().back().state == MeshService::State::Delivered);
}

TEST_CASE("mesh: incoming messages, threads, history and deleting a thread") {
    std::string dir = testutil::scratch("mesh-history");
    std::string path = dir + "/messages.jnl";
    {
        RadioPlatform p;
        p.radio.chatter = false;
        MeshService mesh(p, [] { return (int64_t)1793542200; });
        mesh.begin(path);
        int received = 0;
        mesh.onReceive = [&](const MeshService::Message&) { received++; };
        run(p, mesh, 500);
        p.radio.receiveText("on channel");                 // FIELD-A
        p.radio.receiveText("just to you", 0x1D01);        // direct
        run(p, mesh, 200);
        CHECK(received == 2);
        REQUIRE(mesh.messages().size() == 2);
        CHECK(mesh.threadOf(mesh.messages()[0]).key() == "C1");
        CHECK(mesh.threadOf(mesh.messages()[1]).key() == "D1a01");
        std::string err;
        mesh.send("reply", direct(0x1A01), err);
        run(p, mesh, 2000);
        CHECK(mesh.deleteThread(channel(1)) == 1);
        CHECK(mesh.messages().size() == 2);
    }
    // After a restart: the direct conversation is back with its states; the deleted one isn't.
    RadioPlatform p;
    MeshService again(p, [] { return (int64_t)0; });
    again.begin(path);
    REQUIRE(again.messages().size() == 2);
    CHECK(again.messages()[0].text == "just to you");
    CHECK(again.messages()[1].text == "reply");
    CHECK(again.messages()[1].state == MeshService::State::Delivered);
    CHECK(again.messages()[0].seq < again.messages()[1].seq);
}

TEST_CASE("contacts: nicknames saved, cleaned and removed") {
    std::string path = testutil::scratch("contacts") + "/contacts.txt";
    meshdeck::Contacts c;
    CHECK_FALSE(c.load(path));   // no file yet
    REQUIRE(c.set(0x1A01, "  Dad|  ", "Cardputer\nat home"));
    REQUIRE(c.set(0x1A02, "Test relay", ""));
    meshdeck::Contacts again;
    REQUIRE(again.load(path));
    REQUIRE(again.find(0x1A01));
    CHECK(again.find(0x1A01)->nickname == "Dad");
    CHECK(again.find(0x1A01)->note == "Cardputerat home");
    CHECK(again.set(0x1A02, "", ""));   // empty nickname removes
    CHECK(again.find(0x1A02) == nullptr);
    CHECK(again.set(0x5A03, std::string(40, 'n'), ""));
    CHECK(again.find(0x5A03)->nickname.size() == meshdeck::Contacts::kMaxNickname);
}

TEST_CASE("read marks: only move forward and survive a restart") {
    std::string path = testutil::scratch("readmarks") + "/read.txt";
    meshdeck::ReadMarks r;
    r.load(path);
    CHECK(r.lastRead("C1") == 0);
    r.markRead("C1", 12);
    r.markRead("C1", 5);
    CHECK(r.lastRead("C1") == 12);
    meshdeck::ReadMarks again;
    again.load(path);
    CHECK(again.lastRead("C1") == 12);
}
