// shell.cpp - see shell.h.
#include "ui/shell.h"
#include <ctype.h>
#include <stdio.h>
#include <algorithm>
#include <climits>
#include "kernel/clock.h"
#include "ui/fonts.h"
#include "ui/keys.h"
#include "ui/screenshot.h"
#include "ui/widgets.h"

namespace meshdeck {

// ---------------------------------------------------------------------------------------------
// Frame

lv_obj_t* Frame::action(const std::string& label, std::function<void()> fn) {
    return ui::button(shell_, actions_, label, std::move(fn), 200, ui::kTouchMin + 8);
}

// ---------------------------------------------------------------------------------------------
// Setup

Shell::Shell(Platform& platform, EventBus& events, Settings& settings)
    : platform_(platform), events_(events), settings_(settings), security_(platform) {}

Shell::~Shell() {
    if (keypad_) lv_indev_delete(keypad_);
    if (group_) lv_group_delete(group_);
}

void Shell::addApp(std::unique_ptr<App> app) { apps_.push_back(std::move(app)); }

void Shell::begin(lv_display_t* display) {
    display_ = display;
    // Keyboard input reaches LVGL through a keypad device fed by this shell (readKeypad), so the
    // shell sees every key first. Focus moves are done by the shell, not LVGL's group order.
    group_ = lv_group_create();
    lv_group_set_wrap(group_, false);
    keypad_ = lv_indev_create();
    lv_indev_set_type(keypad_, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keypad_, readKeypad);
    lv_indev_set_user_data(keypad_, this);
    lv_indev_set_group(keypad_, group_);
    lv_indev_set_display(keypad_, display);

    platform_.setBacklight(brightness());
    refreshStatus();   // reads the clock first: the setup wizard starts from the current time
    // The boot splash comes first when it is on; the start overlay follows it when the splash ends.
    security_.begin();
    if (bootSplash_) overlay_ = makeBootSplash(*this);
    else startOverlay();
    lastKeyMs_ = platform_.uptimeMs();
    build();
}

// ---------------------------------------------------------------------------------------------
// Main loop

void Shell::key(uint32_t key, uint8_t mods) {
    lastKeyMs_ = platform_.uptimeMs();
    if (keys_.size() < 32) keys_.push_back({key, mods});   // a full queue drops keys, never blocks
}

// Idle = time since the last key or touch (LVGL tracks touches per display).
uint32_t Shell::idleMs() const {
    uint32_t idle = platform_.uptimeMs() - lastKeyMs_;
    if (display_) {
        uint32_t touchIdle = lv_display_get_inactive_time(display_);
        if (touchIdle < idle) idle = touchIdle;
    }
    return idle;
}


void Shell::later(std::function<void()> fn) { later_.push_back(std::move(fn)); }

void Shell::tick() {
    // Step 1: keys from the drivers. Once a key has queued navigation (ESC, a tile key...), the
    // rest wait for the next pass, so they act on the screen it leads to, not the old one.
    while (!keys_.empty() && later_.empty()) {
        KeyPress k = keys_.front();
        keys_.pop_front();
        handleKey(k.key, k.mods);
    }
    // Step 2: queued work (navigation, rebuilds). Work queued while running waits for next time.
    std::vector<std::function<void()>> work;
    work.swap(later_);
    for (auto& fn : work) fn();

    // Step 3: tell the keyboard whether a text box has focus (Tab5: text mode vs navigation).
    lv_obj_t* f = focused();
    bool text = f && lv_obj_check_type(f, &lv_textarea_class);
    if (text != textInput_) {
        textInput_ = text;
        platform_.setTextInput(text);
    }

    // Step 4: once a second, read the clock and battery and refresh the status widgets.
    uint32_t now = platform_.uptimeMs();
    if (now - lastSecond_ >= 1000) {
        lastSecond_ = now;
        refreshStatus();
    }
    if (!notice_.empty() && (int32_t)(now - noticeUntil_) >= 0) {
        notice_.clear();
        showHint();
    }

    // Step 5: save settings a second after the last change, so a run of presses is one write.
    if (settingsPending_ && now - settingsChangedAt_ >= 1000) {
        settingsPending_ = false;
        if (settings_.dirty() && !settings_.save()) notify("SETTINGS NOT SAVED: NO STORAGE");
    }

    for (auto& task : background_) task();
    checkAutolock();
    if (overlay_) overlay_->tick();
    else if (current_ >= 0) apps_[current_]->tick();
}

// Locks after the chosen idle time. Idle means no key (seen here) and no touch (LVGL tracks
// pointer activity per display).
void Shell::checkAutolock() {
    int minutes = autolockMinutes();
    if (!minutes || !security_.pinSet() || security_.locked()) return;
    if (idleMs() >= (uint32_t)minutes * 60000u) lockNow();
}

bool Shell::lockNow() {
    if (!security_.pinSet()) {
        notify("NO PIN SET: SET ONE IN SECURITY (0) TO USE THE LOCK");
        return false;
    }
    // Locked at once; the lock screen replaces whatever overlay was showing on the next pass, so
    // no widget of the old one can outlive it.
    security_.lock();
    later([this] {
        overlay_ = makeLockScreen(*this);
        build();
    });
    return true;
}

// Locked from the start when a PIN is set; otherwise the setup wizard on the very first start.
void Shell::startOverlay() {
    if (security_.locked()) overlay_ = makeLockScreen(*this);
    else if (!settings_.getBool("setup.done")) overlay_ = makeSetupWizard(*this);
}

void Shell::setBootSplash(bool on) { bootSplash_ = on; }

void Shell::finishBootSplash() {
    later([this] {
        overlay_.reset();
        startOverlay();
        build();
    });
}

void Shell::showOverlay(std::unique_ptr<Overlay> overlay) {
    auto* raw = overlay.release();
    later([this, raw] {
        overlay_.reset(raw);
        build();
    });
}

void Shell::closeOverlay() {
    later([this] {
        overlay_.reset();
        build();
    });
}

int Shell::autolockMinutes() const {
    int m = settings_.getInt("security.autolock_min", 5);
    return m < 0 ? 0 : m;
}

void Shell::setAutolockMinutes(int minutes) {
    settings_.setInt("security.autolock_min", minutes < 0 ? 0 : minutes);
    settingChanged();
}

int Shell::emconLevel() const {
    int level = settings_.getInt("security.emcon", 0);
    return level < 0 || level > 2 ? 0 : level;
}

void Shell::setEmconLevel(int level) {
    settings_.setInt("security.emcon", level < 0 || level > 2 ? 0 : level);
    settingChanged();
}

// ---------------------------------------------------------------------------------------------
// Keys

std::string Shell::keyLabel(int index) {
    if (index < 9) return std::string(1, char('1' + index));
    if (index == 9) return "0";
    if (index == 10) return "A";
    return "";
}

int Shell::indexForKey(uint32_t key) const {
    int i = -1;
    if (key >= '1' && key <= '9') i = int(key - '1');
    else if (key == '0') i = 9;
    else if (key == 'a' || key == 'A') i = 10;
    return i >= 0 && i < (int)apps_.size() ? i : -1;
}

lv_obj_t* Shell::focused() const { return group_ ? lv_group_get_focused(group_) : nullptr; }

void Shell::handleKey(uint32_t k, uint8_t mods) {
    // Step 1: Ctrl shortcuts work everywhere, even while typing, except on the lock screen.
    if (mods & kModCtrl) {
        if (overlay_ && !overlay_->allowsShortcuts()) return;
        switch (tolower((int)k)) {
        case 'p': screenshot(); return;
        case 'n': setTheme(nextTheme(themeId())); return;
        case 'l': lockNow(); return;
        default: break;
        }
        if (overlay_) overlay_->onKey(k, mods);
        else if (current_ >= 0) apps_[current_]->onKey(k, mods);
        return;
    }
    // An overlay (lock screen, setup) gets keys first, and the tile keys are off.
    if (overlay_) {
        if (overlay_->onKey(k, mods)) return;
        switch (k) {
        case LV_KEY_UP: moveFocus(Dir::Up); break;
        case LV_KEY_DOWN: moveFocus(Dir::Down); break;
        case LV_KEY_LEFT: moveFocus(Dir::Left); break;
        case LV_KEY_RIGHT: moveFocus(Dir::Right); break;
        case LV_KEY_ESC: break;
        default: lvglKeys_.push_back(k); break;
        }
        return;
    }
    // Step 2: while a text box has focus, typing goes to it. UP/DOWN/ESC still navigate.
    lv_obj_t* f = focused();
    bool typing = f && lv_obj_check_type(f, &lv_textarea_class);
    if (typing && k != LV_KEY_ESC && k != LV_KEY_UP && k != LV_KEY_DOWN) {
        lvglKeys_.push_back(k);
        return;
    }
    // Step 3: ESC goes back a step.
    if (k == LV_KEY_ESC) { back(); return; }
    // Step 4: the open app gets the key next (zoom, page turns, letters...).
    if (current_ >= 0 && apps_[current_]->onKey(k, mods)) return;
    // Step 5: tile keys open apps from anywhere.
    int index = indexForKey(k);
    if (index >= 0) { open(index); return; }
    // Step 6: navigation.
    switch (k) {
    case LV_KEY_UP: moveFocus(Dir::Up); break;
    case LV_KEY_DOWN: moveFocus(Dir::Down); break;
    case LV_KEY_LEFT: moveFocus(Dir::Left); break;
    case LV_KEY_RIGHT: moveFocus(Dir::Right); break;
    case LV_KEY_NEXT: lv_group_focus_next(group_); break;
    case LV_KEY_PREV: lv_group_focus_prev(group_); break;
    default: lvglKeys_.push_back(k); break;   // ENTER and anything else: to the focused widget
    }
}

// LVGL polls this for keypad input: each key is reported as a press, then a release.
void Shell::readKeypad(lv_indev_t* indev, lv_indev_data_t* data) {
    Shell* s = static_cast<Shell*>(lv_indev_get_user_data(indev));
    data->key = s->lvglLastKey_;
    if (s->lvglKeyDown_) {
        data->state = LV_INDEV_STATE_RELEASED;
        s->lvglKeyDown_ = false;
        data->continue_reading = !s->lvglKeys_.empty();
        return;
    }
    if (s->lvglKeys_.empty()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    s->lvglLastKey_ = s->lvglKeys_.front();
    s->lvglKeys_.pop_front();
    s->lvglKeyDown_ = true;
    data->key = s->lvglLastKey_;
    data->state = LV_INDEV_STATE_PRESSED;
    data->continue_reading = true;
}

// Moves focus to the nearest control in the direction pressed: the candidate whose centre is
// ahead of the current one with the smallest (distance ahead + 2 x sideways offset), so focus
// prefers to stay in the same row or column.
void Shell::moveFocus(Dir dir) {
    lv_obj_t* from = focused();
    if (!from) {
        if (!focusables_.empty()) lv_group_focus_obj(focusables_.front());
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(from, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    lv_obj_t* best = nullptr;
    long bestScore = LONG_MAX;
    for (lv_obj_t* o : focusables_) {
        if (o == from) continue;
        lv_area_t b;
        lv_obj_get_coords(o, &b);
        int dx = (b.x1 + b.x2) / 2 - cx, dy = (b.y1 + b.y2) / 2 - cy;
        long ahead = dir == Dir::Up ? -dy : dir == Dir::Down ? dy : dir == Dir::Left ? -dx : dx;
        long side = (dir == Dir::Up || dir == Dir::Down) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        if (ahead <= 0) continue;
        long score = ahead + 2 * side;
        if (score < bestScore) { bestScore = score; best = o; }
    }
    if (best) {
        lv_group_focus_obj(best);
        lv_obj_scroll_to_view_recursive(best, LV_ANIM_OFF);
    }
}

void Shell::focusable(lv_obj_t* obj) {
    focusables_.push_back(obj);
    lv_group_add_obj(group_, obj);
}

// ---------------------------------------------------------------------------------------------
// Navigation

void Shell::open(int index) {
    later([this, index] {
        if (index < 0 || index >= (int)apps_.size() || index == current_) return;
        if (current_ >= 0) apps_[current_]->onClose();
        else homeFocus_ = index;
        current_ = index;
        apps_[current_]->onOpen();
        build();
    });
}

void Shell::home() {
    later([this] {
        if (current_ < 0) return;
        apps_[current_]->onClose();
        current_ = -1;
        build();
    });
}

void Shell::back() {
    later([this] {
        if (current_ < 0) return;
        if (apps_[current_]->onBack()) return;   // the app went back a step itself
        apps_[current_]->onClose();
        current_ = -1;
        build();
    });
}

void Shell::rebuild() { later([this] { build(); }); }

void Shell::notify(const std::string& text) {
    notice_ = text;
    noticeUntil_ = platform_.uptimeMs() + 4000;
    showHint();
}

// ---------------------------------------------------------------------------------------------
// Settings

ThemeId Shell::themeId() const {
    int t = settings_.getInt("ui.theme", 0);
    return t >= 0 && t < (int)ThemeId::Count ? (ThemeId)t : ThemeId::Green;
}

void Shell::setTheme(ThemeId id) {
    settings_.setInt("ui.theme", (int)id);
    settingChanged();
    rebuild();
}

int Shell::brightness() const {
    int b = settings_.getInt("ui.brightness", 60);
    return b < 10 ? 10 : b > 100 ? 100 : b;
}

void Shell::setBrightness(int percent) {
    percent = percent < 10 ? 10 : percent > 100 ? 100 : percent;
    settings_.setInt("ui.brightness", percent);
    platform_.setBacklight(percent);
    settingChanged();
}

// The Tab5's battery is a 2S Li-ion pack; 2000 mAh is the default until the owner sets their own.
int Shell::capacityMah() const { return settings_.getInt("power.capacity_mah", 2000); }

void Shell::settingChanged() {
    settingsPending_ = true;
    settingsChangedAt_ = platform_.uptimeMs();
}

int64_t Shell::nowUtc() const {
    if (!clockUtc_) return 0;
    return clockUtc_ + (int64_t)((platform_.uptimeMs() - clockAtMs_) / 1000);
}

// ---------------------------------------------------------------------------------------------
// Screenshots

void Shell::screenshot() {
    later([this] {
        Paths p = paths();
        if (!p.available()) { notify("SCREENSHOT NOT SAVED: NO STORAGE"); return; }
        std::string folder = p.folder(Folder::Screenshots);
        makeDirs(folder);
        std::string path = screenshotName(folder, nowUtc());
        bool ok = screenshotTo(path);
        std::string name = path.substr(path.find_last_of('/') + 1);
        notify(ok ? "SCREENSHOT SAVED: FILES/SCREENSHOTS/" + name : "SCREENSHOT FAILED (STORAGE OR MEMORY)");
    });
}

bool Shell::screenshotTo(const std::string& path) { return saveScreenshot(lv_screen_active(), path); }

// ---------------------------------------------------------------------------------------------
// Building screens

void Shell::build() {
    // Step 1: forget the old screen's widgets and focus list.
    int oldFocus = -1;
    for (size_t i = 0; i < focusables_.size(); i++)
        if (focusables_[i] == focused()) oldFocus = (int)i;
    lv_group_remove_all_objs(group_);
    focusables_.clear();
    preferredFocus_ = nullptr;
    statusRight_ = hint_ = nowTime_ = nowDate_ = nowPower_ = nullptr;
    homeInfoLabels_.clear();
    backButton_ = nullptr;

    // Step 2: a new screen with the theme's background.
    const Theme& t = th();
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, t.bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, t.ink, 0);

    // Step 3: the parts.
    buildStatusBar(scr);
    if (overlay_) buildOverlay(scr);
    else if (current_ < 0) buildHome(scr);
    else buildApp(scr);
    buildHintLine(scr);

    // Step 4: show it, delete the old screen, and put focus back where it makes sense.
    lv_obj_t* old = lv_screen_active();
    lv_screen_load(scr);
    if (old && old != scr) lv_obj_delete(old);
    // (An overlay may have chosen its own focus while building, e.g. the setup wizard's NEXT.)
    bool chosen = overlay_ && std::find(focusables_.begin(), focusables_.end(), focused()) != focusables_.end();
    if (preferredFocus_ && std::find(focusables_.begin(), focusables_.end(), preferredFocus_) != focusables_.end()) {
        lv_group_focus_obj(preferredFocus_);   // the app asked for this one (e.g. the message box)
        chosen = true;
    }
    if (!focusables_.empty() && !chosen) {
        int want = overlay_ ? oldFocus : current_ < 0 ? homeFocus_ : oldFocus;
        if (want < 0 || want >= (int)focusables_.size()) want = 0;
        // On an app's screen, focus starts on its content, not on BACK (ENTER would leave the app).
        // BACK is still one UP or ESC away.
        if (current_ >= 0 && !overlay_ && focusables_[want] == backButton_ && focusables_.size() > 1) want = 1;
        lv_group_focus_obj(focusables_[want]);
    }
    refreshStatus();
}

void Shell::buildStatusBar(lv_obj_t* scr) {
    const Theme& t = th();
    int w = lv_display_get_horizontal_resolution(display_);
    lv_obj_t* bar = ui::box(scr, 0, 0, w, kStatusH);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, t.surface, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, t.line, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_t* name = ui::label(bar, "MESHDECK", fonts().body, t.ink);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, ui::kGap, 0);
    statusRight_ = ui::label(bar, "", fonts().body, t.ink);
    lv_obj_align(statusRight_, LV_ALIGN_RIGHT_MID, -ui::kGap, 0);
}

void Shell::buildHome(lv_obj_t* scr) {
    const Theme& t = th();
    int w = lv_display_get_horizontal_resolution(display_), h = lv_display_get_vertical_resolution(display_);
    const int g = ui::kGap, cols = 4, rows = 3;
    int top = kStatusH + g, areaH = h - kStatusH - kHintH - 2 * g;
    int tw = (w - (cols + 1) * g) / cols, tileH = (areaH - (rows - 1) * g) / rows;
    homeNotes_.clear();
    for (int i = 0; i < (int)apps_.size() && i < cols * rows - 1; i++) {
        int x = g + (i % cols) * (tw + g), y = top + (i / cols) * (tileH + g);
        std::string note = apps_[i]->tileNote();
        homeNotes_ += note + "|";
        lv_obj_t* tile = ui::tile(*this, scr, x, y, tw, tileH, keyLabel(i), apps_[i]->name(),
                                  note.empty() ? apps_[i]->subtitle() : note, [this, i] { open(i); });
        // A note (e.g. "3 UNREAD") is shown in the alert colour in place of the subtitle.
        if (!note.empty()) lv_obj_set_style_text_color(lv_obj_get_child(tile, -1), t.warn, 0);
    }
    // The last cell: NOW, a panel with the time, the date and the battery (not pressable).
    int x = g + (cols - 1) * (tw + g), y = top + (rows - 1) * (tileH + g);
    lv_obj_t* now = ui::panel(*this, scr, x, y, tw, tileH);
    lv_obj_set_style_border_width(now, 0, 0);
    nowTime_ = ui::label(now, "--:--", fonts().title, t.ink);
    lv_obj_align(nowTime_, LV_ALIGN_TOP_LEFT, 0, -6);
    nowDate_ = ui::label(now, "", fonts().small, t.muted);
    lv_obj_align(nowDate_, LV_ALIGN_LEFT_MID, 0, 8);
    nowPower_ = ui::label(now, "", fonts().small, t.muted);
    lv_obj_align(nowPower_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // The free cell after the apps: signal and Tab5 information, when the wiring provides it.
    int freeCell = (int)apps_.size();
    if (homeInfo_ && freeCell < cols * rows - 1) {
        int fx = g + (freeCell % cols) * (tw + g), fy = top + (freeCell / cols) * (tileH + g);
        lv_obj_t* info = ui::panel(*this, scr, fx, fy, tw, tileH);
        std::vector<std::string> lines = homeInfo_();
        for (int i = 0; i < kHomeInfoLines; i++) {
            lv_obj_t* line = ui::label(info, i < (int)lines.size() ? lines[i] : "", fonts().small, t.ink);
            lv_obj_set_pos(line, 14, 10 + i * 26);
            homeInfoLabels_.push_back(line);
        }
    }
}

void Shell::buildApp(lv_obj_t* scr) {
    const Theme& t = th();
    App& a = *apps_[current_];
    int w = lv_display_get_horizontal_resolution(display_), h = lv_display_get_vertical_resolution(display_);
    const int g = ui::kGap;
    int rowY = kStatusH + 12;

    // Title row: BACK, the title, then the app's actions on the right.
    ui::button(*this, scr, "< BACK", [this] { back(); }, 180, kTitleH - 8);
    lv_obj_set_pos(focusables_.back(), g, rowY + 4);
    backButton_ = focusables_.back();
    lv_obj_t* title = ui::label(scr, a.name(), fonts().title, t.ink);
    lv_obj_set_pos(title, g + 180 + 2 * g, rowY + 14);
    // The action box starts right of BACK, so even if it were clickable it couldn't cover it.
    lv_obj_t* actions = ui::box(scr, g + 180 + g, rowY, w - 180 - 3 * g, kTitleH);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, g, 0);

    // Body: everything between the title row and the hint line.
    int bodyY = rowY + kTitleH + 12, bodyH = h - bodyY - kHintH - 12;
    lv_obj_t* body = ui::box(scr, g, bodyY, w - 2 * g, bodyH);
    Frame frame(*this, body, w - 2 * g, bodyH, actions);
    a.build(frame);
}

void Shell::buildOverlay(lv_obj_t* scr) {
    int w = lv_display_get_horizontal_resolution(display_), h = lv_display_get_vertical_resolution(display_);
    lv_obj_t* area = ui::box(scr, 0, kStatusH, w, h - kStatusH - kHintH);
    overlay_->build(area, w, h - kStatusH - kHintH);
}

void Shell::buildHintLine(lv_obj_t* scr) {
    int w = lv_display_get_horizontal_resolution(display_), h = lv_display_get_vertical_resolution(display_);
    lv_obj_t* line = ui::box(scr, 0, h - kHintH, w, kHintH);
    hint_ = ui::label(line, "", fonts().small, th().muted);
    lv_obj_align(hint_, LV_ALIGN_LEFT_MID, ui::kGap, 0);
    showHint();
}

void Shell::showHint() {
    if (!hint_) return;
    if (!notice_.empty()) {
        lv_obj_set_style_text_color(hint_, th().warn, 0);
        lv_label_set_text(hint_, notice_.c_str());
        return;
    }
    lv_obj_set_style_text_color(hint_, th().muted, 0);
    std::string text;
    if (overlay_) {
        text = overlay_->hint() ? overlay_->hint() : "ARROWS + ENTER";
    } else if (current_ < 0) {
        text = "1-9 0 A OPEN  |  ARROWS + ENTER  |  CTRL+P SCREENSHOT  |  CTRL+L LOCK";
    } else {
        const char* appHint = apps_[current_]->hint();
        text = std::string("ESC BACK  |  ") + (appHint ? appHint : "ARROWS + ENTER  |  CTRL+P SCREENSHOT");
    }
    lv_label_set_text(hint_, text.c_str());
}

// ---------------------------------------------------------------------------------------------
// Status

std::string Shell::statusText() const {
    std::string s;
    if (!platform_.keyboardConnected()) s += "NO KEYBOARD   ";
    if (security_.locked()) s += "LOCKED   ";
    for (auto& source : statusSources_) {
        std::string part = source();
        if (!part.empty()) s += part + "   ";
    }
    int pct = power_.percent();
    if (pct < 0) s += "BAT --";
    else {
        s += "BAT " + std::to_string(pct) + "%";
        if (power_.external()) s += " EXT";
        else {
            int mins = power_.minutesLeft(capacityMah());
            // VT323's "~" looks like a superscript, so the estimate is spelled out.
            if (mins >= 0)
                s += " " + std::to_string(mins / 60) + "H" + (mins % 60 < 10 ? "0" : "") + std::to_string(mins % 60) + " LEFT";
        }
    }
    int64_t now = nowUtc();
    if (now) {
        CivilTime c = civilFromUtc(localFromUtc(now));
        char buf[24];
        snprintf(buf, sizeof buf, "   %02d:%02d %s", c.hour, c.minute, zoneName(now));
        s += buf;
    } else {
        s += "   TIME NOT SET";
    }
    return s;
}

void Shell::refreshStatus() {
    // Clock: re-read the RTC (cheap) so a clock set elsewhere shows at once.
    int64_t utc = 0;
    if (platform_.rtcRead(utc) && plausibleUtc(utc)) {
        clockUtc_ = utc;
        clockAtMs_ = platform_.uptimeMs();
    }
    // Battery: one sample per second feeds the averages.
    int mv = 0, ma = 0;
    bool haveMv = platform_.batteryMillivolts(mv), haveMa = platform_.batteryMilliamps(ma);
    power_.sample(haveMv, mv, haveMa, ma, platform_.externalPower());

    if (statusRight_) lv_label_set_text(statusRight_, statusText().c_str());
    // On the home screen, rebuild if any tile's note changed (e.g. a new message arrived).
    if (current_ < 0 && !overlay_ && display_) {
        std::string notes;
        for (auto& a : apps_) notes += a->tileNote() + "|";
        if (notes != homeNotes_) rebuild();
    }
    if (nowTime_) {
        int64_t now = nowUtc();
        if (now) {
            CivilTime c = civilFromUtc(localFromUtc(now));
            static const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
            static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
            char buf[40];
            snprintf(buf, sizeof buf, "%02d:%02d", c.hour, c.minute);
            lv_label_set_text(nowTime_, buf);
            snprintf(buf, sizeof buf, "%s %d %s %d %s", days[weekday(localFromUtc(now))], c.day, months[c.month - 1],
                     c.year, zoneName(now));
            lv_label_set_text(nowDate_, buf);
        } else {
            lv_label_set_text(nowTime_, "--:--");
            lv_label_set_text(nowDate_, "TIME NOT SET");
        }
        int mins = power_.minutesLeft(capacityMah());
        std::string p = power_.percent() < 0 ? "BATTERY NOT MEASURED"
                        : power_.external()  ? "EXTERNAL POWER"
                        : mins >= 0 ? "ABOUT " + std::to_string(mins / 60) + " H " + std::to_string(mins % 60) + " MIN LEFT"
                                    : "BATTERY " + std::to_string(power_.percent()) + "%";
        lv_label_set_text(nowPower_, p.c_str());
        if (homeInfo_ && !homeInfoLabels_.empty()) {
            std::vector<std::string> lines = homeInfo_();
            for (size_t i = 0; i < homeInfoLabels_.size(); i++)
                lv_label_set_text(homeInfoLabels_[i], i < lines.size() ? lines[i].c_str() : "");
        }
    }
}

} // namespace meshdeck
