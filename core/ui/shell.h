// shell.h - the MESHDECK shell: home screen, status bar, app frame, keys, focus and screenshots.
//
// Screen layout (1280 x 720):
//   status bar  (52 px)   MESHDECK ... keyboard warning, battery and time left, clock
//   home        11 app tiles in a 4 x 3 grid, plus a NOW panel (clock, date, battery)
//     or app    title row: BACK, title, the app's action buttons; then the app's body
//   hint line   (44 px)   what the keys do here, or a short notice
//
// Keys (the same on every screen; see docs/user-guide.md):
//   1-9, 0, A   open that home tile           ESC        back one step
//   arrows      move focus                    ENTER      press the focused control
//   Ctrl+P      screenshot to Files/Screenshots
//   Ctrl+N      next colour theme             Ctrl+L     lock now (needs a PIN)
//   + / -       zoom, page or volume, whatever the open app uses them for
// The open app sees every key the shell doesn't use (App::onKey) before focus moves.
//
// Start-up: with a PIN set the deck starts on the lock screen; on the very first start (setting
// setup.done not yet 1) it starts on the setup wizard. Both are Overlays (ui/overlay.h).
//
// Threading rule: LVGL callbacks never rebuild the screen directly. Everything that replaces the
// screen (open, home, back, rebuild, theme) is queued and carried out by tick() from the main loop.
#pragma once
#include <stdint.h>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "kernel/event_bus.h"
#include "kernel/settings.h"
#include "kernel/storage.h"
#include "lvgl.h"
#include "platform/platform.h"
#include "services/power_monitor.h"
#include "services/security.h"
#include "ui/app.h"
#include "ui/overlay.h"
#include "ui/theme.h"

namespace meshdeck {

class Shell {
public:
    static constexpr int kStatusH = 52;
    static constexpr int kHintH = 44;
    static constexpr int kTitleH = 80;

    Shell(Platform& platform, EventBus& events, Settings& settings);
    ~Shell();

    // Adds an app. Home tiles follow the order added: keys 1-9, then 0, then A.
    void addApp(std::unique_ptr<App> app);
    // Builds the home screen. Call after lv_init(), the display, and every addApp().
    void begin(lv_display_t* display);
    // Queues a key press (handled by the next tick). Safe to call from input drivers.
    void key(uint32_t key, uint8_t mods = 0);
    // Main-loop work: keys, queued actions, status refresh (1 s), the open app's tick, saving
    // settings a second after the last change.
    void tick();

    // ---- Navigation (all queued) ----
    void later(std::function<void()> fn);
    void open(int index);
    void home();
    void back();
    void rebuild();
    void notify(const std::string& text);   // shown on the hint line for 4 s

    // Saves a screenshot to MESHDECK/Files/Screenshots and reports where (queued, so the screen
    // is captured after the current changes are drawn).
    void screenshot();
    // Immediate version for tools and tests: renders the active screen into path.
    bool screenshotTo(const std::string& path);

    // ---- Lock and overlays ----
    Security& security() { return security_; }
    bool locked() const { return security_.locked(); }
    // Locks and shows the lock screen. False (with a notice) when no PIN is set.
    bool lockNow();
    void showOverlay(std::unique_ptr<Overlay> overlay);   // queued
    void closeOverlay();                                  // queued
    // The boot splash: on when the platform starts the shell (before begin). When it ends, the lock
    // screen or the setup wizard takes over, as it does without the splash.
    void setBootSplash(bool on);
    void finishBootSplash();                              // queued; called by the splash itself
    bool overlayShowing() const { return overlay_ != nullptr; }
    int autolockMinutes() const;                          // 0 = never
    void setAutolockMinutes(int minutes);
    int emconLevel() const;                               // 0 NORMAL, 1 RX ONLY, 2 RADIO OFF
    void setEmconLevel(int level);

    // ---- Services ----
    // Work run on every main-loop pass whatever is on screen (e.g. reading the mesh radio).
    void addBackgroundTask(std::function<void()> task) { background_.push_back(std::move(task)); }
    // A short piece of text for the status bar, left of the battery (e.g. "MESH OK").
    void addStatusSource(std::function<std::string()> source) { statusSources_.push_back(std::move(source)); }
    // The free cell on the home screen (the one after the apps): up to kHomeInfoLines lines of live
    // information from the services (radio, signal, Tab5). Refreshed every second while home is shown.
    static constexpr int kHomeInfoLines = 5;
    void setHomeInfo(std::function<std::vector<std::string>()> source) { homeInfo_ = std::move(source); }
    // Called by an app while building: give this control keyboard focus when the screen appears.
    void preferFocus(lv_obj_t* obj) { preferredFocus_ = obj; }

    // ---- What apps use ----
    Platform& platform() { return platform_; }
    EventBus& events() { return events_; }
    Settings& settings() { return settings_; }
    Paths paths() const { return Paths(platform_.dataRoot()); }
    PowerMonitor& power() { return power_; }
    const Theme& th() const { return theme(themeId()); }
    ThemeId themeId() const;
    void setTheme(ThemeId id);
    int brightness() const;                // 10-100 %
    void setBrightness(int percent);
    int capacityMah() const;               // battery pack size for the time-left estimate
    int64_t nowUtc() const;                // 0 when the clock isn't set
    // Adds a control to keyboard navigation (the widgets in ui/widgets.h do this themselves).
    void focusable(lv_obj_t* obj);

    int appCount() const { return (int)apps_.size(); }
    App& app(int index) { return *apps_[index]; }
    int currentApp() const { return current_; }   // -1 on the home screen
    static std::string keyLabel(int index);      // "1".."9", "0", "A"
    std::string statusText() const;              // the status bar's right-hand text (also for tests)

private:
    struct KeyPress { uint32_t key; uint8_t mods; };
    enum class Dir { Up, Down, Left, Right };

    void handleKey(uint32_t key, uint8_t mods);
    int indexForKey(uint32_t key) const;
    void moveFocus(Dir dir);
    void build();
    void buildStatusBar(lv_obj_t* scr);
    void buildHome(lv_obj_t* scr);
    void buildApp(lv_obj_t* scr);
    void buildOverlay(lv_obj_t* scr);
    void checkAutolock();
    uint32_t idleMs() const;
    void buildHintLine(lv_obj_t* scr);
    void refreshStatus();
    void showHint();
    void settingChanged();
    lv_obj_t* focused() const;
    static void readKeypad(lv_indev_t* indev, lv_indev_data_t* data);

    Platform& platform_;
    EventBus& events_;
    Settings& settings_;
    PowerMonitor power_;
    Security security_;
    std::unique_ptr<Overlay> overlay_;
    bool bootSplash_ = false;
    void startOverlay();                  // the lock screen or the setup wizard, when either is due
    uint32_t lastKeyMs_ = 0;        // for auto-lock (touch activity comes from LVGL)
    std::vector<std::unique_ptr<App>> apps_;
    int current_ = -1;
    int homeFocus_ = 0;             // tile to focus when returning home

    lv_display_t* display_ = nullptr;
    lv_group_t* group_ = nullptr;
    lv_indev_t* keypad_ = nullptr;
    std::vector<lv_obj_t*> focusables_;
    bool textInput_ = false;

    std::deque<KeyPress> keys_;                  // from drivers, handled in tick()
    std::deque<uint32_t> lvglKeys_;              // passed on to LVGL's keypad
    bool lvglKeyDown_ = false;
    uint32_t lvglLastKey_ = 0;
    std::vector<std::function<void()>> later_;
    std::vector<std::function<void()>> background_;
    std::vector<std::function<std::string()>> statusSources_;
    std::function<std::vector<std::string>()> homeInfo_;   // see setHomeInfo
    std::vector<lv_obj_t*> homeInfoLabels_;                 // the lines of that panel, while home is shown
    lv_obj_t* backButton_ = nullptr;                        // the title row's BACK on an app's screen
    lv_obj_t* preferredFocus_ = nullptr;
    std::string homeNotes_;          // tile notes shown on the home screen as built

    // Widgets refreshed every second (nullptr when not on screen)
    lv_obj_t* statusRight_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    lv_obj_t* nowTime_ = nullptr;
    lv_obj_t* nowDate_ = nullptr;
    lv_obj_t* nowPower_ = nullptr;

    std::string notice_;
    uint32_t noticeUntil_ = 0;
    uint32_t lastSecond_ = 0;
    uint32_t settingsChangedAt_ = 0;
    bool settingsPending_ = false;
    int64_t clockUtc_ = 0;          // last good RTC reading
    uint32_t clockAtMs_ = 0;        // uptime when it was read
};

} // namespace meshdeck
