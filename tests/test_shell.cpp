// test_shell.cpp - drives the real shell and apps headless: LVGL renders into a display with no
// screen, and the test presses keys exactly as a keyboard driver would.
#include <map>
#include <memory>
#include <string>
#include "apps/apps.h"
#include "doctest.h"
#include "fake_radio.h"
#include "services/contacts.h"
#include "services/mesh.h"
#include "services/position.h"
#include "kernel/event_bus.h"
#include "kernel/settings.h"
#include "kernel/storage.h"
#include "lvgl.h"
#include "test_util.h"
#include "ui/keys.h"
#include "ui/shell.h"

namespace {

uint32_t fakeMs = 1000;   // the test controls time

class FakePlatform : public meshdeck::Platform {
public:
    std::string root;
    const char* name() const override { return "Test"; }
    uint32_t uptimeMs() const override { return fakeMs; }
    bool rtcRead(int64_t& utc) override { utc = 1793542200; return true; }   // 2026-11-01 14:10 UTC
    std::string dataRoot() const override { return root; }
    bool batteryMillivolts(int& mv) override { mv = 7400; return true; }
    bool batteryMilliamps(int& ma) override { ma = 200; return true; }
    void setBacklight(int pct) override { backlight = pct; }
    bool rtcWrite(int64_t utc) override { written = utc; return true; }
    bool loadSecret(const char* n, std::string& d) override {
        auto it = secrets.find(n);
        if (it == secrets.end()) return false;
        d = it->second;
        return true;
    }
    bool saveSecret(const char* n, const std::string& d) override { secrets[n] = d; return true; }
    void randomBytes(uint8_t* out, size_t len) override {
        for (size_t i = 0; i < len; i++) out[i] = uint8_t(i + 1);
    }
    // The radio starts unplugged; the messaging tests plug it in.
    bool radioPresent() const override { return radio.present; }
    int radioRead(uint8_t* buf, int max) override { return radio.read(buf, max); }
    bool radioWrite(const uint8_t* data, int len) override { return radio.write(data, len); }
    FakeRadio radio;
    int backlight = -1;
    int64_t written = 0;
    std::map<std::string, std::string> secrets;
};

// A pretend finger for the touch tests.
bool touchDown = false;
int touchX = 0, touchY = 0;

// LVGL is set up once for the whole test run, with a 1280x720 display that draws nowhere and a
// touch device that reports the pretend finger.
lv_display_t* testDisplay() {
    static lv_display_t* disp = nullptr;
    if (!disp) {
        lv_init();
        lv_tick_set_cb([]() -> uint32_t { return fakeMs; });
        disp = lv_display_create(1280, 720);
        static uint8_t buf[1280 * 40 * 2];
        lv_display_set_buffers(disp, buf, nullptr, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(disp, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
        lv_indev_t* touch = lv_indev_create();
        lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch, [](lv_indev_t*, lv_indev_data_t* d) {
            d->point.x = touchX;
            d->point.y = touchY;
            d->state = touchDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        });
    }
    return disp;
}

// One main-loop pass: the shell's work, then LVGL (input and drawing) 50 ms later.
void pass(meshdeck::Shell& shell) {
    shell.tick();
    fakeMs += 50;
    lv_timer_handler();
    shell.tick();
}

struct Fixture {
    FakePlatform platform;
    meshdeck::EventBus events;
    meshdeck::Settings settings;
    std::unique_ptr<meshdeck::MeshService> mesh;
    meshdeck::Contacts contacts;
    std::unique_ptr<meshdeck::PositionService> position;
    std::unique_ptr<meshdeck::Services> services;
    std::unique_ptr<meshdeck::Shell> shell;
    // firstStart = true leaves setup.done unset, so the setup wizard appears. A non-empty pin is
    // set before start-up, so the deck starts locked. radio = true plugs in the fake radio.
    explicit Fixture(const std::string& name, bool firstStart = false, const std::string& pin = "", bool radio = false) {
        platform.radio.present = radio;
        platform.radio.chatter = false;
        platform.root = testutil::scratch(name);
        meshdeck::Paths(platform.root).createAll();
        settings.load(meshdeck::Paths(platform.root).file(meshdeck::Folder::Root, "settings.txt"));
        if (!firstStart) settings.setBool("setup.done", true);
        if (!pin.empty()) {
            meshdeck::Security sec(platform);
            sec.begin();
            std::string err;
            sec.setPin("", pin, err);
        }
        lv_display_t* disp = testDisplay();
        shell = std::make_unique<meshdeck::Shell>(platform, events, settings);
        meshdeck::Shell* sh = shell.get();
        mesh = std::make_unique<meshdeck::MeshService>(platform, [sh] { return sh->nowUtc(); });
        mesh->begin(meshdeck::Paths(platform.root).file(meshdeck::Folder::Messages, "messages.jnl"));
        meshdeck::Paths paths(platform.root);
        contacts.load(paths.file(meshdeck::Folder::Messages, "contacts.txt"));
        position = std::make_unique<meshdeck::PositionService>(platform, *mesh, [sh] { return sh->nowUtc(); });
        position->begin(paths.file(meshdeck::Folder::Root, "position.txt"));
        services = std::make_unique<meshdeck::Services>(meshdeck::Services{*mesh, contacts, *position});
        shell->addBackgroundTask([this] { platform.radio.tick(fakeMs); });
        meshdeck::addBuiltInApps(*shell, *services);
        shell->begin(disp);
        pass(*shell);
    }
};

} // namespace

TEST_CASE("shell: eleven apps, tile keys open them, ESC returns home") {
    Fixture fx("shell-keys");
    meshdeck::Shell& s = *fx.shell;
    CHECK(s.appCount() == 11);
    CHECK(s.currentApp() == -1);
    CHECK(std::string(s.app(0).name()) == "MESSAGES");
    CHECK(std::string(s.app(9).name()) == "GAMES");
    CHECK(std::string(s.app(10).name()) == "TOOLS");

    s.key('2');
    pass(s);
    CHECK(s.currentApp() == 1);   // MAP
    s.key('9');
    pass(s);
    CHECK(s.currentApp() == 8);   // SECURITY, straight from another app
    s.key(LV_KEY_ESC);
    pass(s);
    CHECK(s.currentApp() == -1);
    s.key('0');
    pass(s);
    CHECK(s.currentApp() == 9);   // GAMES
}

TEST_CASE("shell: arrows move focus and ENTER presses the focused tile") {
    Fixture fx("shell-focus");
    meshdeck::Shell& s = *fx.shell;
    s.key(LV_KEY_DOWN);    // from tile 1 straight down to tile 5
    pass(s);
    s.key(LV_KEY_ENTER);
    pass(s);
    pass(s);
    CHECK(s.currentApp() == 4);   // FILES
}

TEST_CASE("every app: ENTER on its opening screen does not leave the app") {
    // Each app opened by its tile key (1-9, then 0), ENTER pressed once: the app must stay open.
    // The title row's BACK button must not be the first thing ENTER reaches.
    // DIAGNOSTICS only shows information, and its opening
    // screens have no control but BACK, so ENTER there correctly leaves them. They're not checked.
    for (int k = 0; k < 10; k++) {
        Fixture fx("focus-app-" + std::to_string(k));
        meshdeck::Shell& s = *fx.shell;
        s.key(k < 9 ? '1' + k : '0');
        pass(s);
        REQUIRE(s.currentApp() == k);
        const std::string name = s.app(k).name();
        if (name == "DIAGNOSTICS") continue;
        s.key(LV_KEY_ENTER);
        pass(s);
        pass(s);
        CHECK_MESSAGE(s.currentApp() == k, "app " << k << " (" << s.app(k).name() << ") left on ENTER");
    }
}

TEST_CASE("logbook: ENTER on the hub opens its first part") {
    Fixture fx("shell-logbook-hub-enter");
    meshdeck::Shell& s = *fx.shell;
    s.key('4');
    pass(s);
    REQUIRE(s.currentApp() == 3);
    s.key(LV_KEY_ENTER);            // the hub's first tile: LOG ENTRIES
    pass(s);
    pass(s);
    CHECK(s.currentApp() == 3);
    s.key(LV_KEY_ESC);
    pass(s);
    CHECK(s.currentApp() == 3);     // back to the hub, not home
}

TEST_CASE("logbook: ENTER on the REPORTS list opens a form; ESC steps back from it") {
    Fixture fx("shell-logbook-form");
    meshdeck::Shell& s = *fx.shell;
    s.key('4');
    pass(s);
    REQUIRE(s.currentApp() == 3);
    s.key('2');                     // REPORTS: the list of forms
    pass(s);
    s.key(LV_KEY_ENTER);            // opens the first form (not BACK, which the title row offers first)
    pass(s);
    pass(s);
    CHECK(s.currentApp() == 3);
    s.key(LV_KEY_ESC);              // back to the list of forms
    pass(s);
    pass(s);
    CHECK(s.currentApp() == 3);
    s.key(LV_KEY_ESC);              // back to the hub
    pass(s);
    CHECK(s.currentApp() == 3);
    s.key(LV_KEY_ESC);              // and home
    pass(s);
    CHECK(s.currentApp() == -1);
}

TEST_CASE("shell: an app's own keys come first, and its back step is respected") {
    Fixture fx("shell-logbook");
    meshdeck::Shell& s = *fx.shell;
    s.key('4');
    pass(s);
    REQUIRE(s.currentApp() == 3);   // LOGBOOK hub
    s.key('2');                     // on the hub, 2 opens REPORTS rather than the MAP tile
    pass(s);
    CHECK(s.currentApp() == 3);
    s.key(LV_KEY_ESC);              // back to the hub, still in LOGBOOK
    pass(s);
    CHECK(s.currentApp() == 3);
    s.key(LV_KEY_ESC);              // and home
    pass(s);
    CHECK(s.currentApp() == -1);
}


TEST_CASE("shell: Ctrl+N changes theme and the setting is saved a second later") {
    Fixture fx("shell-theme");
    meshdeck::Shell& s = *fx.shell;
    CHECK(s.themeId() == meshdeck::ThemeId::Green);
    s.key('n', meshdeck::kModCtrl);
    pass(s);
    CHECK(s.themeId() == meshdeck::ThemeId::Amber);
    CHECK(fx.settings.dirty());
    fakeMs += 1500;
    pass(s);
    CHECK_FALSE(fx.settings.dirty());
    std::string saved;
    CHECK(meshdeck::readFile(meshdeck::Paths(fx.platform.root).file(meshdeck::Folder::Root, "settings.txt"), saved));
    CHECK(saved.find("ui.theme=1") != std::string::npos);
}

TEST_CASE("shell: Ctrl+P saves a full-screen PNG in Files/Screenshots") {
    Fixture fx("shell-screenshot");
    meshdeck::Shell& s = *fx.shell;
    s.key('p', meshdeck::kModCtrl);
    pass(s);
    std::string folder = meshdeck::Paths(fx.platform.root).folder(meshdeck::Folder::Screenshots);
    auto files = meshdeck::listDir(folder);
    REQUIRE(files.size() == 1);
    CHECK(files[0].name == "2026-11-01_1410.png");   // UK time: GMT in November
    // Signature + IHDR + IDAT (zlib header, 720 stored rows, Adler-32) + IEND.
    CHECK(files[0].size == 8 + 25 + 12 + (2 + 720 * (5 + 1 + 1280 * 3) + 4) + 12);
    // A second one in the same minute gets a suffix.
    s.key('p', meshdeck::kModCtrl);
    pass(s);
    CHECK(meshdeck::fileExists(folder + "/2026-11-01_1410_2.png"));
}

TEST_CASE("shell: status shows battery, time left and UK time") {
    Fixture fx("shell-status");
    meshdeck::Shell& s = *fx.shell;
    // 7.4 V = 40 %; 2000 mAh x 40 % at 200 mA = 4 h 00 min.
    CHECK(s.statusText() == "NO RADIO   BAT 40% 4H00 LEFT   14:10 GMT");
}

// Taps the screen at (x, y): finger down for one pass, up for the next.
static void tap(meshdeck::Shell& s, int x, int y) {
    touchX = x;
    touchY = y;
    touchDown = true;
    pass(s);
    touchDown = false;
    pass(s);
    pass(s);
}

TEST_CASE("terminal: typing HELP and tapping RUN answers without leaving or freezing") {
    Fixture fx("terminal-run");
    meshdeck::Shell& s = *fx.shell;
    s.key('6');                                     // TERMINAL
    pass(s);
    REQUIRE(s.currentApp() == 5);
    for (char c : std::string("help")) s.key((uint32_t)c);
    pass(s);
    tap(s, 1184, 616);                              // the RUN button, bottom right
    for (int i = 0; i < 10; i++) pass(s);
    CHECK(s.currentApp() == 5);
}

TEST_CASE("shell: touch opens a tile and the BACK button returns home") {
    Fixture fx("shell-touch");
    meshdeck::Shell& s = *fx.shell;
    tap(s, 482, 362);                      // middle of tile 6 (TERMINAL): second row, second column
    REQUIRE(s.currentApp() == 5);
    tap(s, 16 + 90, meshdeck::Shell::kStatusH + 12 + 40);   // middle of < BACK
    CHECK(s.currentApp() == -1);
    // And from an app with action-area content on the right (SETTINGS).
    s.key('7');
    pass(s);
    REQUIRE(s.currentApp() == 6);
    tap(s, 16 + 90, meshdeck::Shell::kStatusH + 12 + 40);
    CHECK(s.currentApp() == -1);
}

// Types keys one at a time, each followed by a main-loop pass.
static void type(meshdeck::Shell& s, const char* keys) {
    for (const char* k = keys; *k; k++) {
        s.key(*k == '\n' ? (uint32_t)LV_KEY_ENTER : (uint32_t)*k);
        pass(s);
    }
    pass(s);
}

TEST_CASE("shell: with a PIN the deck starts locked and only the right PIN opens it") {
    Fixture fx("shell-locked", false, "2468");
    meshdeck::Shell& s = *fx.shell;
    CHECK(s.locked());
    CHECK(s.overlayShowing());
    s.key('2');                    // tile keys don't work while locked (the 2 goes into the PIN)...
    pass(s);
    CHECK(s.currentApp() == -1);
    s.key('p', meshdeck::kModCtrl); // ...nor screenshots
    pass(s);
    CHECK(meshdeck::listDir(meshdeck::Paths(fx.platform.root).folder(meshdeck::Folder::Screenshots)).empty());
    type(s, "1111\n");             // submits 21111: wrong
    CHECK(s.locked());
    CHECK(s.security().failures() >= 1);
    s.key(LV_KEY_ESC);
    type(s, "2468\n");
    CHECK_FALSE(s.locked());
    CHECK_FALSE(s.overlayShowing());
}

TEST_CASE("shell: Ctrl+L locks only when a PIN is set; auto-lock after idle minutes") {
    Fixture none("shell-nopin");
    none.shell->key('l', meshdeck::kModCtrl);
    pass(*none.shell);
    CHECK_FALSE(none.shell->locked());

    Fixture fx("shell-autolock", false, "1357");
    meshdeck::Shell& s = *fx.shell;
    type(s, "1357\n");
    REQUIRE_FALSE(s.locked());
    s.key('l', meshdeck::kModCtrl);
    pass(s);
    CHECK(s.locked());
    type(s, "1357\n");
    REQUIRE_FALSE(s.locked());
    s.setAutolockMinutes(2);
    fakeMs += 60000;
    pass(s);
    CHECK_FALSE(s.locked());       // one idle minute: still open
    fakeMs += 61000;
    pass(s);
    CHECK(s.locked());             // two: locked
}

TEST_CASE("shell: first start runs setup; setting the clock and a PIN by keyboard") {
    Fixture fx("shell-setup", true);
    meshdeck::Shell& s = *fx.shell;
    REQUIRE(s.overlayShowing());
    type(s, "\n");                 // START
    type(s, "\n");                 // SAVE CLOCK (unchanged)
    CHECK(fx.platform.written == 1793542200);
    type(s, "4321\n");             // PIN
    type(s, "4321\n");             // confirm
    CHECK(s.security().pinSet());
    type(s, "\n");                 // START USING MESHDECK
    CHECK_FALSE(s.overlayShowing());
    CHECK(fx.settings.getBool("setup.done"));
    CHECK_FALSE(s.locked());       // setting a PIN doesn't lock straight away
}

TEST_CASE("shell: setup can be skipped") {
    Fixture fx("shell-setup-skip", true);
    meshdeck::Shell& s = *fx.shell;
    s.key(LV_KEY_LEFT);            // step 1 has two buttons: SKIP SETUP, then START (focused)
    pass(s);
    type(s, "\n");
    CHECK_FALSE(s.overlayShowing());
    CHECK(fx.settings.getBool("setup.done"));
    CHECK_FALSE(s.security().pinSet());
}

TEST_CASE("shell: the screen stays on at the chosen brightness however long it sits idle") {
    Fixture fx("shell-always-on");
    meshdeck::Shell& s = *fx.shell;
    s.setBrightness(70);
    CHECK(fx.platform.backlight == 70);
    fakeMs += 60 * 60 * 1000;              // an hour with no input
    pass(s);
    CHECK(fx.platform.backlight == 70);
    s.key('2');                            // and the first key works
    pass(s);
    CHECK(s.currentApp() == 1);
}

TEST_CASE("shell: SETTINGS has DISPLAY, CLOCK and ABOUT, opened with 1-3") {
    Fixture fx("shell-settings-hub");
    meshdeck::Shell& s = *fx.shell;
    s.key('7');
    pass(s);
    REQUIRE(s.currentApp() == 6);
    s.key('1');                            // DISPLAY
    s.key('+');                            // brightness +10
    for (int i = 0; i < 3; i++) pass(s);
    CHECK(s.brightness() == 70);
    CHECK(fx.platform.backlight == 70);
    s.key(LV_KEY_ESC);                     // back to the hub
    s.key(LV_KEY_ESC);                     // and home
    for (int i = 0; i < 3; i++) pass(s);
    CHECK(s.currentApp() == -1);
}

TEST_CASE("shell: keys typed quickly act on the screen the previous key led to") {
    Fixture fx("shell-fast-keys");
    meshdeck::Shell& s = *fx.shell;
    s.key('7');                    // SETTINGS hub
    s.key('2');                    // its CLOCK part, not the MAP tile
    s.key(LV_KEY_ESC);             // back to the hub
    s.key('3');                    // its ABOUT part, not the LIBRARY tile
    for (int i = 0; i < 6; i++) pass(s);
    CHECK(s.currentApp() == 6);
}

TEST_CASE("messages: tap a chat, type, ENTER sends; the radio gets it and it shows delivered") {
    Fixture fx("shell-messages", false, "", true);
    meshdeck::Shell& s = *fx.shell;
    for (int i = 0; i < 6; i++) pass(s);
    REQUIRE(fx.mesh->link() == meshdeck::MeshService::Link::Ready);
    CHECK(s.statusText().find("MESH OK") == 0);
    s.key('1');                            // MESSAGES
    pass(s);
    REQUIRE(s.currentApp() == 0);
    tap(s, 640, 156 + 48 + 40);            // the first chat in the list: PRIMARY
    for (const char* c = "hello mesh"; *c; c++) {   // the message box has focus: just type
        s.key((uint32_t)*c);
        pass(s);
    }
    s.key(LV_KEY_ENTER);
    for (int i = 0; i < 4; i++) pass(s);
    REQUIRE_FALSE(fx.mesh->messages().empty());
    CHECK(fx.mesh->messages().back().text == "hello mesh");
    CHECK(fx.mesh->messages().back().to == meshdeck::MeshService::kBroadcast);
    fakeMs += 1000;
    for (int i = 0; i < 40; i++) pass(s);  // ~2 s: handed over, then acknowledged
    REQUIRE(fx.platform.radio.sentTexts.size() == 1);
    CHECK(fx.platform.radio.sentTexts[0] == "hello mesh");
    CHECK(fx.mesh->messages().back().state == meshdeck::MeshService::State::Delivered);
}

TEST_CASE("messages: an incoming message shows as unread on the home tile until read") {
    Fixture fx("shell-unread", false, "", true);
    meshdeck::Shell& s = *fx.shell;
    for (int i = 0; i < 6; i++) pass(s);
    fx.platform.radio.receiveText("are you ok?", 0x1D01);   // direct to us from TS01
    for (int i = 0; i < 4; i++) pass(s);
    CHECK(s.app(0).tileNote() == "1 UNREAD");
    s.key('1');
    pass(s);
    tap(s, 640, 156 + 48 + 40);            // newest conversation first: TS01
    pass(s);
    CHECK(s.app(0).tileNote() == "");
}

TEST_CASE("shell: brightness is clamped and sent to the platform") {
    Fixture fx("shell-bright");
    meshdeck::Shell& s = *fx.shell;
    s.setBrightness(150);
    CHECK(s.brightness() == 100);
    CHECK(fx.platform.backlight == 100);
    s.setBrightness(0);
    CHECK(s.brightness() == 10);
}
