// security_app.cpp - SECURITY: the PIN, auto-lock and lock now.
//
// V1 covers what the lock screen needs. The EMCON radio levels are in V7.
#include <memory>
#include "apps/apps.h"
#include "services/mesh.h"
#include "services/security.h"
#include "ui/fonts.h"
#include "ui/pin_pad.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {

class SecurityApp : public App {
public:
    SecurityApp(Shell& shell, Services& services) : App(shell), mesh_(services.mesh) {
        pad_.onSubmit = [this](const std::string& pin) { pinEntered(pin); };
    }
    const char* name() const override { return "SECURITY"; }
    const char* subtitle() const override { return "PIN, lock, EMCON"; }
    void onOpen() override { reset(); }

    void build(Frame& frame) override {
        if (flow_ != Flow::None) buildFlow(frame);
        else buildMain(frame);
    }

    bool onKey(uint32_t key, uint8_t) override { return flow_ != Flow::None && pad_.key(key); }

    bool onBack() override {
        if (flow_ == Flow::None) return false;
        reset();
        shell_.rebuild();
        return true;
    }

    const char* hint() const override {
        return flow_ != Flow::None ? "TYPE THE PIN, THEN OK  |  ESC CANCELS" : "ARROWS + ENTER  |  CTRL+L LOCK";
    }

private:
    // Change: current -> new -> confirm. Set (no PIN yet): new -> confirm. Remove: current.
    enum class Flow { None, Change, Set, Remove };
    enum class Stage { Current, New, Confirm };

    void buildMain(Frame& frame) {
        const Theme& t = shell_.th();
        Security& sec = shell_.security();
        const int g = ui::kGap, half = (frame.w - g) / 2;
        lv_obj_t* left = ui::panel(shell_, frame.body, 0, 0, half, frame.h);
        ui::label(left, "PIN LOCK", fonts().title, t.ink);
        lv_obj_t* state = ui::label(left, sec.pinSet() ? "A PIN IS SET." : "NO PIN: ANYONE CAN USE THIS DECK.", fonts().body,
                                    sec.pinSet() ? t.ink : t.warn);
        lv_obj_set_pos(state, 0, 64);
        int bw = half - 2 * g - 4;
        int y = 120;
        auto add = [&](const char* label, std::function<void()> fn) {
            lv_obj_t* b = ui::button(shell_, left, label, std::move(fn), bw, ui::kTouchMin + 8);
            lv_obj_set_pos(b, 0, y);
            y += ui::kTouchMin + 8 + g;
        };
        if (sec.pinSet()) {
            add("LOCK NOW", [this] { shell_.lockNow(); });
            add("CHANGE PIN", [this] { start(Flow::Change); });
            add("REMOVE PIN", [this] { start(Flow::Remove); });
        } else {
            add("SET A PIN", [this] { start(Flow::Set); });
        }
        if (!message_.empty()) {
            lv_obj_t* m = ui::label(left, message_, fonts().body, t.warn);
            lv_obj_align(m, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        }

        lv_obj_t* right = ui::panel(shell_, frame.body, half + g, 0, half, frame.h);
        ui::label(right, "AUTO-LOCK", fonts().title, t.ink);
        lv_obj_t* sub = ui::label(right, "LOCK AFTER THIS LONG WITH NO KEY OR TOUCH", fonts().small, t.muted);
        lv_obj_set_pos(sub, 0, 64);
        lv_obj_t* row = ui::box(right, 0, 100, half - 2 * g, ui::kTouchBig);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, g / 2, 0);
        static const int choices[] = {0, 2, 5, 15};
        for (int m : choices) {
            lv_obj_t* b = ui::button(shell_, row, m ? std::to_string(m) + " MIN" : "OFF",
                                     [this, m] { shell_.setAutolockMinutes(m); shell_.rebuild(); },
                                     (half - 2 * g - 3 * g / 2) / 4 - 2, ui::kTouchBig);
            if (m == shell_.autolockMinutes()) lv_obj_set_style_bg_color(b, t.line, 0);
        }
        // Radio emissions, as in 1.x. The level is kept across restarts (see Shell::emconLevel).
        lv_obj_t* emTitle = ui::label(right, "RADIO EMISSIONS (EMCON)", fonts().small, t.muted);
        lv_obj_set_pos(emTitle, 0, 214);
        lv_obj_t* emRow = ui::box(right, 0, 244, half - 2 * g, ui::kTouchBig);
        lv_obj_set_flex_flow(emRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(emRow, g / 2, 0);
        static const char* levels[] = {"NORMAL", "RX ONLY", "RADIO OFF"};
        const int current = shell_.emconLevel();
        for (int lvl = 0; lvl < 3; lvl++) {
            lv_obj_t* b = ui::button(shell_, emRow, levels[lvl], [this, lvl] { setEmcon(lvl); },
                                     (half - 2 * g - g) / 3 - 2, ui::kTouchBig);
            if (lvl == current) lv_obj_set_style_bg_color(b, t.line, 0);
        }
        lv_obj_t* emHelp = ui::label(right, emconHelp(current), fonts().small, t.muted);
        lv_label_set_long_mode(emHelp, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(emHelp, half - 2 * g);
        lv_obj_set_pos(emHelp, 0, 350);

    }

    // Sets the level in the settings, applies it to the mesh at once, and redraws the buttons.
    void setEmcon(int level) {
        shell_.setEmconLevel(level);
        mesh_.setEmcon((MeshService::Emcon)shell_.emconLevel());
        shell_.rebuild();
    }

    static const char* emconHelp(int level) {
        if (level == 1) return "RX ONLY: NOTHING IS TRANSMITTED. MESSAGES WAIT AS HELD. THE RADIO'S TRANSMITTER IS OFF.";
        if (level == 2) return "RADIO OFF: MESHDECK STOPS USING THE RADIO. ITS TRANSMITTER IS OFF. THE RADIO STAYS POWERED.";
        return "NORMAL: MESSAGES GO OUT AND COME IN.";
    }

    void buildFlow(Frame& frame) {
        const Theme& t = shell_.th();
        static const char* prompts[] = {"TYPE YOUR CURRENT PIN.", "TYPE THE NEW PIN (4 TO 12 DIGITS).",
                                        "TYPE THE NEW PIN AGAIN TO CONFIRM IT."};
        lv_obj_t* p = ui::label(frame.body, prompts[(int)stage_], fonts().title, t.ink);
        lv_label_set_long_mode(p, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(p, frame.w - 460);
        if (!message_.empty()) {
            lv_obj_t* m = ui::label(frame.body, message_, fonts().body, t.warn);
            lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(m, frame.w - 460);
            lv_obj_set_pos(m, 0, 160);
        }
        pad_.build(shell_, frame.body, frame.w - 420, 0, 420, frame.h);
    }

    void start(Flow f) {
        flow_ = f;
        stage_ = f == Flow::Set ? Stage::New : Stage::Current;
        message_.clear();
        shell_.rebuild();
    }

    void reset() {
        flow_ = Flow::None;
        current_.clear();
        next_.clear();
        pad_.clear();
    }

    void pinEntered(const std::string& pin) {
        Security& sec = shell_.security();
        std::string error;
        pad_.clear();
        message_.clear();
        if (stage_ == Stage::Current) {
            // Check now, so a wrong PIN is reported before typing a new one. The real change
            // checks it again (counted once more only if wrong).
            Security::Result r = sec.verify(pin);
            if (r == Security::Result::Waiting) message_ = "WAIT " + std::to_string(sec.waitSeconds()) + " S BEFORE TRYING AGAIN";
            else if (r == Security::Result::Wrong) message_ = "THAT PIN IS WRONG";
            else if (flow_ == Flow::Remove) {
                if (sec.removePin(pin, error)) { reset(); message_ = "PIN REMOVED. THE DECK NO LONGER LOCKS."; }
                else message_ = error;
            } else {
                current_ = pin;
                stage_ = Stage::New;
            }
        } else if (stage_ == Stage::New) {
            if (Security::validPin(pin, error)) {
                next_ = pin;
                stage_ = Stage::Confirm;
            } else {
                message_ = error;
            }
        } else {
            if (pin != next_) {
                message_ = "THE TWO PINS DIDN'T MATCH. TYPE THE NEW PIN AGAIN.";
                stage_ = Stage::New;
            } else if (sec.setPin(current_, next_, error)) {
                reset();
                message_ = "NEW PIN SET.";
            } else {
                message_ = error;
            }
        }
        shell_.rebuild();
    }

    MeshService& mesh_;
    PinPad pad_;
    Flow flow_ = Flow::None;
    Stage stage_ = Stage::Current;
    std::string current_, next_, message_;
};

std::unique_ptr<App> makeSecurityApp(Shell& shell, Services& services) {
    return std::make_unique<SecurityApp>(shell, services);
}

} // namespace meshdeck
