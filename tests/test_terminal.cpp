// test_terminal.cpp - the TERMINAL commands: what each reports, what it says when a value isn't known,
// and the two commands that the screen carries out (CLEAR and SCREENSHOT).
#include <string>
#include "doctest.h"
#include "platform/platform.h"
#include "services/mesh.h"
#include "services/power_monitor.h"
#include "services/terminal.h"

namespace {

class FakePlatform : public meshdeck::Platform {
public:
    std::string root;
    const char* name() const override { return "Test"; }
    uint32_t uptimeMs() const override { return 5000; }
    std::string dataRoot() const override { return root; }
    void randomBytes(uint8_t* out, size_t len) override {
        for (size_t i = 0; i < len; i++) out[i] = 0;
    }
};

// A line of the output that contains the text, or "" if none does.
std::string lineWith(const meshdeck::Terminal::Result& r, const std::string& text) {
    for (const std::string& line : r.lines)
        if (line.find(text) != std::string::npos) return line;
    return "";
}

} // namespace

TEST_CASE("terminal: HELP lists every command, with its help text") {
    FakePlatform platform;
    meshdeck::MeshService mesh(platform, [] { return (int64_t)0; });
    meshdeck::PowerMonitor power;
    meshdeck::Terminal::Context ctx{platform, mesh, power, 6600, [] { return (int64_t)0; }};
    meshdeck::Terminal::Result r = meshdeck::Terminal::run("help", ctx);   // not case-sensitive
    REQUIRE(!r.lines.empty());
    for (const meshdeck::Terminal::Command& c : meshdeck::Terminal::commands()) {
        CHECK(lineWith(r, c.name) != "");
        CHECK(lineWith(r, c.help) != "");
    }
}

TEST_CASE("terminal: an unknown command says so and points at HELP") {
    FakePlatform platform;
    meshdeck::MeshService mesh(platform, [] { return (int64_t)0; });
    meshdeck::PowerMonitor power;
    meshdeck::Terminal::Context ctx{platform, mesh, power, 6600, [] { return (int64_t)0; }};
    meshdeck::Terminal::Result r = meshdeck::Terminal::run("  frobnicate now ", ctx);
    REQUIRE(r.lines.size() == 1);
    CHECK(r.lines[0] == "UNKNOWN COMMAND: FROBNICATE. TYPE HELP");
    CHECK(meshdeck::Terminal::run("   ", ctx).lines.empty());   // a blank line does nothing
}

TEST_CASE("terminal: STATUS says what it knows and admits what it doesn't") {
    FakePlatform platform;
    meshdeck::MeshService mesh(platform, [] { return (int64_t)0; });
    meshdeck::PowerMonitor power;
    meshdeck::Terminal::Context ctx{platform, mesh, power, 6600, [] { return (int64_t)0; }};
    meshdeck::Terminal::Result r = meshdeck::Terminal::run("STATUS", ctx);
    CHECK(lineWith(r, "MESHDECK ") != "");
    CHECK(lineWith(r, "ON Test") != "");
    CHECK(lineWith(r, "UP 5 S") != "");
    CHECK(lineWith(r, "FREE MEMORY NOT REPORTED") != "");   // the fake platform reports no memory
    CHECK(lineWith(r, "CARD NOT FOUND") != "");
    platform.root = "/sd";
    CHECK(lineWith(meshdeck::Terminal::run("STATUS", ctx), "CARD OK") != "");
}

TEST_CASE("terminal: POWER reports what is measured and says when it isn't") {
    FakePlatform platform;
    meshdeck::MeshService mesh(platform, [] { return (int64_t)0; });
    meshdeck::PowerMonitor power;
    meshdeck::Terminal::Context ctx{platform, mesh, power, 6600, [] { return (int64_t)0; }};
    CHECK(lineWith(meshdeck::Terminal::run("POWER", ctx), "BATTERY NOT MEASURED") != "");
    CHECK(lineWith(meshdeck::Terminal::run("POWER", ctx), "CURRENT NOT MEASURED") != "");
    power.sample(true, 7900, true, 210, false);
    meshdeck::Terminal::Result r = meshdeck::Terminal::run("POWER", ctx);
    CHECK(lineWith(r, "VOLTAGE 7.90 V") != "");
    CHECK(lineWith(r, "EXTERNAL POWER: NO") != "");
}

TEST_CASE("terminal: TIME shows UK time, or says the clock isn't set") {
    FakePlatform platform;
    meshdeck::MeshService mesh(platform, [] { return (int64_t)0; });
    meshdeck::PowerMonitor power;
    meshdeck::Terminal::Context unset{platform, mesh, power, 6600, [] { return (int64_t)0; }};
    CHECK(meshdeck::Terminal::run("TIME", unset).lines[0] == "CLOCK NOT SET");
    meshdeck::Terminal::Context set{platform, mesh, power, 6600, [] { return (int64_t)1793542200; }};   // 2026-11-01 14:10 UTC
    std::string line = meshdeck::Terminal::run("TIME", set).lines[0];
    CHECK(line.find("2026") != std::string::npos);
    CHECK(line.find("GMT") != std::string::npos);   // November is UK winter time
}

TEST_CASE("terminal: CLEAR and SCREENSHOT are handed back to the screen to carry out") {
    FakePlatform platform;
    meshdeck::MeshService mesh(platform, [] { return (int64_t)0; });
    meshdeck::PowerMonitor power;
    meshdeck::Terminal::Context ctx{platform, mesh, power, 6600, [] { return (int64_t)0; }};
    CHECK(meshdeck::Terminal::run("clear", ctx).action == meshdeck::Terminal::Action::Clear);
    CHECK(meshdeck::Terminal::run("screenshot", ctx).action == meshdeck::Terminal::Action::Screenshot);
    CHECK(meshdeck::Terminal::run("status", ctx).action == meshdeck::Terminal::Action::None);
}
