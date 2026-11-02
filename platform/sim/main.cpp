// main.cpp - MESHDECK simulator: the real shell and apps in a desktop window.
//
// Everything above the Platform interface is the same code that runs on the Tab5. The simulator
// supplies a fake battery (so the status bar and power page have something to show), the PC's
// clock, and a folder "sim-sd" in the current directory standing in for the SD card.
//
//   meshdeck-sim                  open the window; mouse = touch, PC keyboard = Tab5 keyboard
//   meshdeck-sim --shots <dir>    walk through setup, every app and the lock screen, saving each
//                                screen to <dir>/<name>.png, then exit (for docs and review)
#include <SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <random>
#include <string>
#include "apps/apps.h"
#include "fake_radio.h"
#include "services/contacts.h"
#include "services/mesh.h"
#include "services/position.h"
#include "kernel/event_bus.h"
#include "kernel/settings.h"
#include "kernel/storage.h"
#include "lvgl.h"
#include "ui/keys.h"
#include "ui/shell.h"

namespace {

// Deletes every file under dir, hidden ones included (the folders stay).
void removeTree(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string path = dir + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) removeTree(path);
        else remove(path.c_str());
    }
    closedir(d);
}

class SimPlatform : public meshdeck::Platform {
public:
    explicit SimPlatform(std::string root) : root_(std::move(root)) {}
    const char* name() const override { return "Simulator"; }
    uint32_t uptimeMs() const override { return SDL_GetTicks(); }
    // The PC's clock, plus whatever offset "setting the clock" asked for.
    bool rtcRead(int64_t& utc) override { utc = (int64_t)time(nullptr) + offset_; return true; }
    bool rtcWrite(int64_t utc) override { offset_ = utc - (int64_t)time(nullptr); return true; }
    std::string dataRoot() const override { return root_; }
    // Private records live in hidden files in the data folder (on the Tab5: internal flash).
    bool loadSecret(const char* name, std::string& data) override {
        return meshdeck::readFile(root_ + "/.secret-" + name, data, 4096);
    }
    bool saveSecret(const char* name, const std::string& data) override {
        return meshdeck::writeFileAtomic(root_ + "/.secret-" + name, data);
    }
    void randomBytes(uint8_t* out, size_t len) override {
        std::random_device rd;
        for (size_t i = 0; i < len; i++) out[i] = (uint8_t)rd();
    }
    // A pretend 2S pack at 3.95 V per cell, drawing about 220 mA with a little noise.
    bool batteryMillivolts(int& mv) override { mv = 7900; return true; }
    bool batteryMilliamps(int& ma) override { ma = 210 + (int)(SDL_GetTicks() / 1000 % 20); return true; }
    void setBacklight(int pct) override { printf("backlight %d%%\n", pct); }
    void log(const char* line) override { printf("%s\n", line); }
    // A pretend Meshtastic radio (platform/fake): three nodes, two channels, acks, check-ins.
    bool radioPresent() const override { return radio.present; }
    int radioRead(uint8_t* buf, int max) override { return radio.read(buf, max); }
    bool radioWrite(const uint8_t* data, int len) override { return radio.write(data, len); }
    FakeRadio radio;

private:
    std::string root_;
    int64_t offset_ = 0;
};

// PC key -> MESHDECK key. Printable characters arrive separately as SDL_TEXTINPUT.
int onSdlEvent(void* user, SDL_Event* e) {
    meshdeck::Shell& shell = *static_cast<meshdeck::Shell*>(user);
    if (e->type == SDL_TEXTINPUT) {
        for (const char* c = e->text.text; *c; c++)
            if (*c >= 32 && *c < 127) shell.key((uint32_t)*c);
        return 0;
    }
    if (e->type != SDL_KEYDOWN) return 0;
    SDL_Keymod m = (SDL_Keymod)e->key.keysym.mod;
    uint8_t mods = ((m & KMOD_CTRL) ? meshdeck::kModCtrl : 0) | ((m & KMOD_SHIFT) ? meshdeck::kModShift : 0) |
                   ((m & KMOD_ALT) ? meshdeck::kModAlt : 0);
    SDL_Keycode k = e->key.keysym.sym;
    uint32_t key = 0;
    switch (k) {
    case SDLK_UP: key = LV_KEY_UP; break;
    case SDLK_DOWN: key = LV_KEY_DOWN; break;
    case SDLK_LEFT: key = LV_KEY_LEFT; break;
    case SDLK_RIGHT: key = LV_KEY_RIGHT; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: key = LV_KEY_ENTER; break;
    case SDLK_ESCAPE: key = LV_KEY_ESC; break;
    case SDLK_BACKSPACE: key = LV_KEY_BACKSPACE; break;
    case SDLK_DELETE: key = LV_KEY_DEL; break;
    case SDLK_TAB: key = (mods & meshdeck::kModShift) ? LV_KEY_PREV : LV_KEY_NEXT; break;
    case SDLK_HOME: key = LV_KEY_HOME; break;
    case SDLK_END: key = LV_KEY_END; break;
    default:
        // Ctrl+letter produces no text event, so it is passed on here.
        if ((mods & meshdeck::kModCtrl) && k >= 'a' && k <= 'z') key = (uint32_t)k;
        break;
    }
    if (key) shell.key(key, mods);
    return 0;
}

// Walks through first-start setup (setting PIN 1234), every app, and the lock screen, saving
// each screen as a PNG. Runs on its own fresh data folder, so it never touches sim-sd.
int takeShots(meshdeck::Shell& shell, const std::string& dir) {
    meshdeck::makeDirs(dir);
    auto settle = [&] {
        for (int i = 0; i < 4; i++) {
            shell.tick();
            SDL_Delay(40);   // LVGL reads input every 30 ms
            lv_timer_handler();
        }
        shell.tick();
        lv_obj_update_layout(lv_screen_active());
    };
    int failures = 0;
    auto shot = [&](const std::string& name) {
        settle();
        std::string path = dir + "/" + name + ".png";
        bool ok = shell.screenshotTo(path);
        printf("%s %s\n", ok ? "saved" : "FAILED", path.c_str());
        failures += !ok;
    };
    auto type = [&](const char* keys) {
        for (const char* k = keys; *k; k++) shell.key(*k == '\n' ? (uint32_t)LV_KEY_ENTER : (uint32_t)*k);
        settle();
    };
    shot("setup-1-welcome");
    type("\n");
    shot("setup-2-clock");
    type("\n");
    shot("setup-3-pin");
    type("1234\n");
    type("1234\n");
    shot("setup-4-done");
    type("\n");
    shot("00-home");
    for (int i = 0; i < shell.appCount(); i++) {
        shell.open(i);
        char name[64];
        snprintf(name, sizeof name, "%02d-%s", i + 1, shell.app(i).name());
        for (char* c = name; *c; c++) *c = (char)tolower(*c);
        shot(name);
    }
    // MESSAGES: let the fake radio connect and chat, then the chat list, a thread, nodes.
    for (int i = 0; i < 40; i++) {
        shell.tick();
        lv_timer_handler();
        SDL_Delay(100);   // ~4 s: the pretend Cardputer sends a check-in
    }
    shell.open(0);
    settle();
    shot("01b-messages-chats");
    shell.key(LV_KEY_DOWN);
    type("\n");
    type("on my way\n");
    for (int i = 0; i < 25; i++) { shell.tick(); lv_timer_handler(); SDL_Delay(100); }   // acked
    shot("01c-messages-thread");
    shell.key(LV_KEY_ESC);
    settle();
    type("n");    // NEW CHAT
    shot("01d-messages-nodes");
    shell.key(LV_KEY_DOWN);   // first node: TS01
    type("\n");
    shell.key(LV_KEY_UP);     // from the message box up to the title row's CONTACT
    shell.key(LV_KEY_UP);
    settle();
    shell.key(LV_KEY_ESC);
    settle();
    shell.key(LV_KEY_ESC);
    settle();
    shell.home();
    settle();
    // MAP: the map, a search, and the place found.
    shell.open(2);   // LIBRARY: the built-in cards
    settle();
    shot("03-library-cards");
    shell.key(LV_KEY_ESC);
    settle();
    shell.open(1);
    settle();
    shot("02b-map");
    type("s");
    type("exeter");
    for (int i = 0; i < 8; i++) { shell.tick(); lv_timer_handler(); SDL_Delay(100); }   // the search waits 300 ms
    shot("02c-map-search");
    type("\n");
    shot("02d-map-place");
    type("++");   // closer in on the place found
    shot("02e-map-place-zoomed");
    // POSITION (two back from the map in focus order): a grid reference in central Exeter, the
    // maps guide's example area, then C centres the map on YOU.
    shell.key(LV_KEY_PREV);
    shell.key(LV_KEY_PREV);
    shell.key(LV_KEY_ENTER);
    settle();
    type("SX 9235 9290");
    shot("02f-map-position");
    type("\n");
    type("c");
    shot("02g-map-you");
    type("++");   // street level, from the local pack
    shot("02i-map-streets");
    shell.key(LV_KEY_PREV);   // NODES, one back from the map
    shell.key(LV_KEY_ENTER);
    settle();
    shot("02h-map-nodes");
    shell.key(LV_KEY_ESC);
    settle();
    shell.home();
    settle();
    shell.open(3);   // LOGBOOK: the field log
    settle();
    type("1");
    shot("04b-logbook-log");
    shell.key(LV_KEY_ESC);
    settle();
    shell.open(3);   // LOGBOOK: REPORTS, the list of forms, then the first form
    settle();
    type("2");
    shot("04c-logbook-reports");
    shell.key(LV_KEY_ENTER);
    settle();
    shot("04d-logbook-report-form");
    shell.key(LV_KEY_ESC);
    settle();
    shell.key(LV_KEY_ESC);   // back to the hub
    settle();
    shell.open(3);   // LOGBOOK: INVENTORY, the locations
    settle();
    type("3");
    shot("04e-logbook-inventory");
    type("Car");    // a new location, added with the ADD button below the box
    shell.key(LV_KEY_DOWN);
    settle();
    shell.key(LV_KEY_ENTER);
    settle();
    type("Water 2 litre");   // an item in that location: DOWN lands on SET DATE, LEFT goes to ADD ITEM
    shell.key(LV_KEY_DOWN);
    shell.key(LV_KEY_LEFT);
    settle();
    shell.key(LV_KEY_ENTER);
    settle();
    shot("04f-logbook-inventory-items");
    shell.key(LV_KEY_ESC);
    settle();
    shell.key(LV_KEY_ESC);
    settle();
    shell.open(3);   // LOGBOOK: NOTES, a new note saved with the button below the box
    settle();
    type("4");
    settle();
    type("Meet point: the church gate");
    settle();
    shot("04g-logbook-notes");   // the typing box; SAVE NOTE is tapped on the device
    shell.key(LV_KEY_ESC);
    settle();
    shell.open(5);   // TERMINAL: type HELP, then ENTER
    settle();
    type("help");
    settle();
    shell.key(LV_KEY_ENTER);
    settle();
    shot("07-terminal");
    shell.key(LV_KEY_ESC);
    settle();
    shell.open(9);   // GAMES: the hub, then RALLY with a few seconds of play
    settle();
    shot("09-games");
    shell.key(LV_KEY_ENTER);
    settle();
    shell.key(' ');
    for (int i = 0; i < 30; i++) { shell.tick(); lv_timer_handler(); SDL_Delay(100); }
    shot("09b-rally");
    shell.key(LV_KEY_ESC);
    settle();
    shell.key(LV_KEY_RIGHT);   // the hub: SEA BATTLE is the second tile
    shell.key(LV_KEY_ENTER);
    settle();
    shot("09c-sea-battle");
    shell.key(LV_KEY_ENTER);   // fire at the first square, then down and fire again
    settle();
    shell.key(LV_KEY_DOWN);
    shell.key(LV_KEY_ENTER);
    settle();
    shot("09d-sea-battle-fired");
    shell.key(LV_KEY_ESC);
    settle();
    shell.key('3');            // the hub: 3 opens KLONDIKE
    settle();
    shot("09e-klondike");
    shell.key('d');            // draw a card, then try to put it on the first column
    settle();
    shell.key(LV_KEY_ENTER);   // on the stock: draw
    settle();
    shell.key(LV_KEY_RIGHT);   // to the waste, pick it up, then to column 1 and put it down
    shell.key(LV_KEY_RIGHT);
    shell.key(LV_KEY_ENTER);
    shell.key(LV_KEY_RIGHT);
    shell.key(LV_KEY_RIGHT);
    shell.key(LV_KEY_RIGHT);
    shell.key(LV_KEY_RIGHT);
    shell.key(LV_KEY_ENTER);
    settle();
    shot("09f-klondike-moved");
    shell.key(LV_KEY_ESC);
    settle();
    shell.key(LV_KEY_ESC);
    settle();
    shell.open(6);   // SETTINGS: its parts
    settle();
    type("1");
    shot("08b-settings-display");
    shell.key(LV_KEY_ESC);
    type("2");
    shot("08c-settings-clock");
    shell.key(LV_KEY_ESC);
    type("3");
    shell.key(LV_KEY_DOWN);   // to the CREDITS button
    type("\n");
    shot("08d-settings-credits");
    shell.key(LV_KEY_ESC);
    shell.key(LV_KEY_ESC);
    settle();
    shell.home();
    settle();
    shell.open(7);   // DIAGNOSTICS: the touch test, the key log (with a few keys) and the radio page
    settle();
    shell.key(LV_KEY_ENTER);   // the first button: TOUCH TEST
    settle();
    shot("08b-diagnostics-touch-test");
    shell.key(LV_KEY_ESC);
    settle();
    shell.key(LV_KEY_RIGHT);   // KEY LOG
    shell.key(LV_KEY_ENTER);
    settle();
    type("MD");
    shell.key(LV_KEY_UP);
    shell.key('1', meshdeck::kModCtrl);
    shell.key(LV_KEY_ENTER);
    settle();
    shot("08c-diagnostics-key-log");
    shell.key(LV_KEY_ESC);
    settle();
    shell.key(LV_KEY_RIGHT);   // RADIO
    shell.key(LV_KEY_RIGHT);
    shell.key(LV_KEY_ENTER);
    settle();
    shot("08d-diagnostics-radio");
    shell.key(LV_KEY_ESC);
    settle();
    shell.home();
    settle();
    shell.setEmconLevel(1);   // SECURITY with EMCON at RX ONLY, then back to NORMAL
    shell.open(8);
    settle();
    shot("09b-security-emcon-rx-only");
    shell.setEmconLevel(0);
    shell.home();
    settle();
    shell.open(10);   // TOOLS: the timer running, the stopwatch with a lap, a calculation
    settle();
    type("1");
    type(" ");
    for (int i = 0; i < 25; i++) { shell.tick(); lv_timer_handler(); SDL_Delay(100); }
    shot("11b-tools-timer");
    shell.key(LV_KEY_ESC);
    settle();
    type("2");
    type(" ");
    for (int i = 0; i < 32; i++) { shell.tick(); lv_timer_handler(); SDL_Delay(100); }
    type("l");
    for (int i = 0; i < 21; i++) { shell.tick(); lv_timer_handler(); SDL_Delay(100); }
    type("l");
    shot("11c-tools-stopwatch");
    shell.key(LV_KEY_ESC);
    settle();
    type("3");
    type("12*3+4=");
    shot("11d-tools-calculator");
    shell.key(LV_KEY_ESC);
    settle();
    shell.home();
    shell.lockNow();
    shot("lock-screen");
    type("1111\n");
    shot("lock-wrong-pin");
    type("1234\n");
    if (shell.locked()) { printf("FAILED to unlock\n"); failures++; }
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    // Step 1: LVGL with an SDL window the size of the Tab5's screen, and the mouse as touch.
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return SDL_GetTicks(); });
    lv_display_t* disp = lv_sdl_window_create(1280, 720);
    lv_sdl_window_set_title(disp, "MESHDECK simulator (1280x720)");
    lv_sdl_mouse_create();

    // Step 2: the kernel and the shell, exactly as on the device. --shots starts from an empty
    // data folder so it always begins with first-start setup.
    bool shots = argc == 3 && std::string(argv[1]) == "--shots";
    if (shots) removeTree("sim-shots");
    SimPlatform platform(shots ? "sim-shots" : "sim-sd");
    meshdeck::Paths paths(platform.dataRoot());
    paths.createAll();
    // Map packs and the place index: any found in a "sim-maps" folder next to the program's
    // working folder are copied into the pretend SD card (they are too big to keep in git).
    for (const meshdeck::DirEntry& e : meshdeck::listDir("sim-maps")) {
        std::string dest = paths.file(meshdeck::Folder::Maps, e.name);
        if (e.isDir || meshdeck::fileExists(dest)) continue;
        FILE* in = fopen(("sim-maps/" + e.name).c_str(), "rb");
        FILE* out = fopen(dest.c_str(), "wb");
        char buf[65536];
        size_t n;
        while (in && out && (n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        if (in) fclose(in);
        if (out) fclose(out);
    }
    // Sample files for the simulator's FILES app (pictures and text), from a "sim-files" folder.
    for (const meshdeck::DirEntry& e : meshdeck::listDir("sim-files")) {
        std::string dest = paths.file(meshdeck::Folder::Files, e.name);
        if (e.isDir || meshdeck::fileExists(dest)) continue;
        FILE* in = fopen(("sim-files/" + e.name).c_str(), "rb");
        FILE* out = fopen(dest.c_str(), "wb");
        char buf[65536];
        size_t n;
        while (in && out && (n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        if (in) fclose(in);
        if (out) fclose(out);
    }    // Sample books for the simulator's shelf (public-domain packs made by tools/build_books.py),
    // from a "sim-books" folder next to the program, like the maps.
    for (const meshdeck::DirEntry& e : meshdeck::listDir("sim-books")) {
        std::string dest = paths.file(meshdeck::Folder::Books, e.name);
        if (e.isDir || meshdeck::fileExists(dest)) continue;
        FILE* in = fopen(("sim-books/" + e.name).c_str(), "rb");
        FILE* out = fopen(dest.c_str(), "wb");
        char buf[65536];
        size_t n;
        while (in && out && (n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        if (in) fclose(in);
        if (out) fclose(out);
    }    meshdeck::EventBus events;
    meshdeck::Settings settings;
    settings.load(paths.file(meshdeck::Folder::Root, "settings.txt"));
    meshdeck::Shell shell(platform, events, settings);
    meshdeck::MeshService mesh(platform, [&shell] { return shell.nowUtc(); });
    mesh.begin(paths.file(meshdeck::Folder::Messages, "messages.jnl"));
    meshdeck::Contacts contacts;
    contacts.load(paths.file(meshdeck::Folder::Messages, "contacts.txt"));
    meshdeck::PositionService position(platform, mesh, [&shell] { return shell.nowUtc(); });
    position.begin(paths.file(meshdeck::Folder::Root, "position.txt"));
    meshdeck::Services services{mesh, contacts, position};
    shell.addBackgroundTask([&platform] { platform.radio.tick(SDL_GetTicks()); });
    meshdeck::addBuiltInApps(shell, services);
    shell.setBootSplash(!shots);   // the splash runs in a normal start, but not in the screenshot walk
    shell.begin(disp);

    // Step 3: the PC keyboard feeds the shell like the Tab5 keyboard does.
    SDL_StartTextInput();
    SDL_AddEventWatch(onSdlEvent, &shell);

    if (shots) return takeShots(shell, argv[2]);

    // Step 4: the main loop (closing the window exits). MESHDECK_CR_SPLASH_SHOT=1 saves a frame of the
    // boot splash every 1.5 s for 9 s (forming, draining, fading, then what follows) and quits, so it
    // can be checked without watching the window.
    const bool splashShots = getenv("MESHDECK_CR_SPLASH_SHOT") != nullptr;
    const uint32_t startMs = SDL_GetTicks();
    int shotsTaken = 0;
    if (splashShots) meshdeck::makeDirs("shots");
    for (;;) {
        lv_timer_handler();
        shell.tick();
        events.dispatch();
        if (splashShots && SDL_GetTicks() - startMs > (uint32_t)(shotsTaken + 1) * 1500) {
            shell.screenshotTo("shots/splash-" + std::to_string(++shotsTaken) + ".png");
            if (shotsTaken == 6) break;
        }
        SDL_Delay(5);
    }
}
