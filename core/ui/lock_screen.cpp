// lock_screen.cpp - the lock screen: the time and date, what happened, and the PIN pad.
//
// Nothing behind it can be reached until the right PIN is entered. After 3 wrong PINs the waits
// in services/security.h apply, and the message counts them down.
#include <stdio.h>
#include "kernel/clock.h"
#include "services/security.h"
#include "ui/fonts.h"
#include "ui/overlay.h"
#include "ui/pin_pad.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {

class LockScreen : public Overlay {
public:
    explicit LockScreen(Shell& shell) : Overlay(shell) {
        pad_.onSubmit = [this](const std::string& pin) { submit(pin); };
    }

    void build(lv_obj_t* parent, int w, int h) override {
        const Theme& t = shell_.th();
        const int g = ui::kGap, padW = 420;
        // Left: the time, the date, LOCKED and the message line.
        time_ = ui::label(parent, "--:--", fonts().huge, t.ink);
        lv_obj_set_pos(time_, g * 2, g);
        date_ = ui::label(parent, "", fonts().body, t.muted);
        lv_obj_set_pos(date_, g * 2, 150);
        lv_obj_t* locked = ui::label(parent, "LOCKED", fonts().title, t.ink);
        lv_obj_set_pos(locked, g * 2, 260);
        msg_ = ui::label(parent, "", fonts().body, t.ink);
        lv_label_set_long_mode(msg_, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(msg_, w - padW - 6 * g);
        lv_obj_set_pos(msg_, g * 2, 330);
        // Right: the PIN pad.
        pad_.build(shell_, parent, w - padW - g, g, padW, h - 2 * g);
        refresh();
    }

    void tick() override {
        uint32_t now = shell_.platform().uptimeMs();
        if (now - last_ >= 1000) {
            last_ = now;
            refresh();
        }
    }

    bool onKey(uint32_t key, uint8_t) override {
        if (key == LV_KEY_ESC) { pad_.clear(); return true; }
        return pad_.key(key);
    }
    const char* hint() const override { return "TYPE YOUR PIN, THEN OK OR ENTER  |  ESC CLEARS"; }
    bool allowsShortcuts() const override { return false; }

private:
    void submit(const std::string& pin) {
        Security& sec = shell_.security();
        Security::Result r = sec.unlock(pin);
        pad_.clear();
        if (r == Security::Result::Ok) {
            shell_.closeOverlay();
            shell_.notify("UNLOCKED");
            return;
        }
        if (r == Security::Result::Wrong) {
            uint32_t free = sec.freeTriesLeft();
            message_ = sec.waitSeconds() ? "WRONG PIN." : free ? "WRONG PIN. " + std::to_string(free) + " MORE BEFORE WAITS BEGIN." : "WRONG PIN.";
        } else {
            message_ = "";
        }
        refresh();
    }

    void refresh() {
        if (!time_) return;
        int64_t now = shell_.nowUtc();
        if (now) {
            CivilTime c = civilFromUtc(localFromUtc(now));
            char buf[48];
            snprintf(buf, sizeof buf, "%02d:%02d", c.hour, c.minute);
            lv_label_set_text(time_, buf);
            snprintf(buf, sizeof buf, "%02d/%02d/%04d %s", c.day, c.month, c.year, zoneName(now));
            lv_label_set_text(date_, buf);
        } else {
            lv_label_set_text(time_, "--:--");
            lv_label_set_text(date_, "TIME NOT SET");
        }
        uint32_t wait = shell_.security().waitSeconds();
        std::string text = message_;
        if (wait) text += (text.empty() ? "" : " ") + std::string("WAIT ") + std::to_string(wait) + " S BEFORE TRYING AGAIN.";
        if (text.empty()) text = "ENTER YOUR PIN.";
        lv_label_set_text(msg_, text.c_str());
    }

    PinPad pad_;
    std::string message_;
    lv_obj_t* time_ = nullptr;
    lv_obj_t* date_ = nullptr;
    lv_obj_t* msg_ = nullptr;
    uint32_t last_ = 0;
};

std::unique_ptr<Overlay> makeLockScreen(Shell& shell) { return std::make_unique<LockScreen>(shell); }

} // namespace meshdeck
