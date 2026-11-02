// setup_wizard.cpp - first-start setup: welcome, clock, PIN, done.
//
// Shown once, on the first start (until the setting setup.done is 1). Every step can be skipped
// and everything can be changed later (clock in SETTINGS, PIN in SECURITY).
#include <stdio.h>
#include "kernel/clock.h"
#include "services/security.h"
#include "ui/clock_setter.h"
#include "ui/fonts.h"
#include "ui/overlay.h"
#include "ui/pin_pad.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {

class SetupWizard : public Overlay {
public:
    enum Step { Welcome, Clock, Pin, Done };

    explicit SetupWizard(Shell& shell) : Overlay(shell) {
        clock_.start(shell.nowUtc());
        pad_.onSubmit = [this](const std::string& pin) { pinEntered(pin); };
    }

    void build(lv_obj_t* parent, int w, int h) override {
        const Theme& t = shell_.th();
        const int g = ui::kGap;
        static const char* titles[] = {"WELCOME TO MESHDECK", "SET THE CLOCK", "PROTECT WITH A PIN?", "READY"};
        char head[64];
        snprintf(head, sizeof head, "SETUP %d OF 4: %s", step_ + 1, titles[step_]);
        lv_obj_t* title = ui::label(parent, head, fonts().title, t.ink);
        lv_obj_set_pos(title, g, g);

        int bodyY = 90, bodyH = h - bodyY - ui::kTouchMin - 3 * g;
        lv_obj_t* body = ui::box(parent, g, bodyY, w - 2 * g, bodyH);
        switch (step_) {
        case Welcome: buildWelcome(body, w - 2 * g); break;
        case Clock: buildClock(body, w - 2 * g); break;
        case Pin: buildPin(body, w - 2 * g, bodyH); break;
        case Done: buildDone(body, w - 2 * g); break;
        }

        // Bottom row: BACK and SKIP SETUP on the left, the step's main action on the right.
        int rowY = h - ui::kTouchMin - g;
        if (step_ > Welcome) {
            lv_obj_t* back = ui::button(shell_, parent, "< BACK", [this] { go(Step(step_ - 1)); }, 200, ui::kTouchMin);
            lv_obj_set_pos(back, g, rowY);
        }
        if (step_ < Done) {
            lv_obj_t* skip = ui::button(shell_, parent, "SKIP SETUP", [this] { finish(); }, 240, ui::kTouchMin);
            lv_obj_set_pos(skip, g + 200 + g, rowY);
        }
        const char* main = step_ == Welcome ? "START >" : step_ == Clock ? "SAVE CLOCK >" : step_ == Pin ? "NO PIN >" : "START USING MESHDECK";
        lv_obj_t* next = ui::button(shell_, parent, main, [this] { primary(); }, step_ == Done ? 420 : 300, ui::kTouchMin);
        lv_obj_set_pos(next, w - (step_ == Done ? 420 : 300) - g, rowY);
        if (step_ != Pin) lv_group_focus_obj(next);   // ENTER does the obvious thing
    }

    bool onKey(uint32_t key, uint8_t) override {
        if (step_ == Pin && pad_.key(key)) return true;
        if (key == LV_KEY_ESC) {
            if (step_ > Welcome) go(Step(step_ - 1));
            return true;
        }
        return false;
    }

    const char* hint() const override {
        return step_ == Pin ? "TYPE A PIN OF 4-12 DIGITS, THEN OK  |  ESC BACK"
                            : "ARROWS + ENTER  |  ESC BACK  |  CTRL+N THEME";
    }

private:
    void buildWelcome(lv_obj_t* body, int w) {
        text(body, w,
             "MESHDECK is an offline field computer: mesh messages, OS maps, a reference library and a logbook, "
             "all working with no internet or phone network.\n\n"
             "This setup takes a minute: set the clock, and choose whether to protect the deck with a PIN. "
             "Every step can be skipped and changed later.");
    }

    void buildClock(lv_obj_t* body, int w) {
        text(body, w, "UK local time (GMT or BST is worked out for you).");
        clock_.build(shell_, body, 56, w);
    }

    void buildPin(lv_obj_t* body, int w, int h) {
        const Theme& t = shell_.th();
        int textW = w - 420 - 2 * ui::kGap;
        std::string prompt = confirming_ ? "TYPE THE SAME PIN AGAIN TO CONFIRM IT."
                                         : "A PIN locks the deck when it starts, after a few idle minutes, and on Ctrl+L. "
                                           "Without it, anyone holding the deck can read everything on it.\n\n"
                                           "Type 4 to 12 digits and press OK, or choose NO PIN.";
        lv_obj_t* p = text(body, textW, prompt);
        (void)p;
        if (!message_.empty()) {
            lv_obj_t* m = ui::label(body, message_, fonts().body, t.warn);
            lv_obj_set_pos(m, 0, h - 40);
        }
        pad_.build(shell_, body, w - 420, 0, 420, h);
    }

    void buildDone(lv_obj_t* body, int w) {
        int64_t now = shell_.nowUtc();
        char clock[40] = "NOT SET";
        if (now) formatLocal(now, clock, sizeof clock);
        std::string s = std::string("CLOCK: ") + clock + (now ? std::string(" ") + zoneName(now) : "") +
                        "\nPIN: " + (shell_.security().pinSet() ? "SET" : "NONE") +
                        "\n\nKEYS: 1-9, 0 AND A OPEN THE HOME TILES. ESC GOES BACK. CTRL+L LOCKS. "
                        "CTRL+P SAVES A SCREENSHOT. CTRL+N CHANGES THE COLOURS.";
        text(body, w, s);
    }

    lv_obj_t* text(lv_obj_t* body, int w, const std::string& s) {
        lv_obj_t* l = ui::label(body, s, fonts().body, shell_.th().ink);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, w);
        return l;
    }

    void primary() {
        switch (step_) {
        case Welcome: go(Clock); break;
        case Clock: {
            if (!shell_.platform().rtcWrite(clock_.utc())) shell_.notify("THE CLOCK COULDN'T BE SET ON THIS DEVICE");
            go(Pin);
            break;
        }
        case Pin: go(Done); break;   // "NO PIN"
        case Done: finish(); break;
        }
    }

    // First entry: check it and ask again. Second entry: must match, then the PIN is set.
    void pinEntered(const std::string& pin) {
        std::string error;
        pad_.clear();
        if (!confirming_) {
            if (!Security::validPin(pin, error)) { message_ = error; shell_.rebuild(); return; }
            firstPin_ = pin;
            confirming_ = true;
            message_.clear();
            shell_.rebuild();
            return;
        }
        if (pin != firstPin_) {
            confirming_ = false;
            firstPin_.clear();
            message_ = "THE TWO PINS DIDN'T MATCH. START AGAIN.";
            shell_.rebuild();
            return;
        }
        Security& sec = shell_.security();
        if (sec.pinSet() || !sec.setPin("", pin, error)) {
            message_ = sec.pinSet() ? "A PIN IS ALREADY SET (CHANGE IT IN SECURITY)" : error;
            shell_.rebuild();
            return;
        }
        firstPin_.clear();
        go(Done);
    }

    void go(Step s) {
        step_ = s;
        confirming_ = false;
        firstPin_.clear();
        message_.clear();
        shell_.rebuild();
    }

    void finish() {
        shell_.settings().setBool("setup.done", true);
        shell_.settings().save();
        shell_.closeOverlay();
        shell_.notify("SETUP DONE. PRESS A NUMBER OR TAP A TILE.");
    }

    Step step_ = Welcome;
    ClockSetter clock_;
    PinPad pad_;
    bool confirming_ = false;
    std::string firstPin_, message_;
};

std::unique_ptr<Overlay> makeSetupWizard(Shell& shell) { return std::make_unique<SetupWizard>(shell); }

} // namespace meshdeck
