// tools_app.cpp - TOOLS: a hub of a countdown timer, a stopwatch and a simple calculator.
//
// Keys: on the hub, 1, 2 and 3 open TIMER, STOPWATCH and CALCULATOR (as GAMES' 1-3 do).
//   TIMER: SPACE starts or pauses, R resets to the preset.
//   STOPWATCH: SPACE starts or stops, L records a lap, R resets.
//   CALCULATOR: digits, . + - * / and ENTER or = work it out; BACKSPACE deletes; C clears.
// ESC goes back. The calculator works left to right, as a pocket calculator does.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <string>
#include <vector>
#include "apps/apps.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {
namespace {

const char* kTimer = "TIMER";
const char* kStopwatch = "STOPWATCH";
const char* kCalculator = "CALCULATOR";

const uint32_t kPresetsMin[] = {1, 5, 10, 15};
const size_t kLapsKept = 8;
const size_t kMaxDigits = 12;

// hh:mm:ss, or mm:ss under an hour. With tenths, mm:ss.t (the stopwatch shows tenths).
std::string formatClock(uint32_t ms, bool tenths) {
    uint32_t total = ms / 1000, h = total / 3600, m = total / 60 % 60, s = total % 60;
    char buf[32];
    if (h) snprintf(buf, sizeof buf, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    else snprintf(buf, sizeof buf, "%02u:%02u", (unsigned)m, (unsigned)s);
    std::string text = buf;
    if (tenths) text += "." + std::to_string(ms / 100 % 10);
    return text;
}

// %.10g: a whole number shows without a decimal point; anything else to ten significant digits.
std::string formatNumber(double v) {
    char buf[40];
    snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

class ToolsApp : public App {
public:
    using App::App;

    const char* name() const override { return "TOOLS"; }
    const char* subtitle() const override { return "Timer, stopwatch, calculator"; }
    void onOpen() override { view_ = View::Hub; }

    void build(Frame& frame) override {
        clock_ = status_ = lapsLabel_ = display_ = nullptr;
        if (view_ == View::Timer) buildTimer(frame);
        else if (view_ == View::Stopwatch) buildStopwatch(frame);
        else if (view_ == View::Calculator) buildCalculator(frame);
        else buildHub(frame);
    }

    void tick() override {
        const uint32_t now = shell_.platform().uptimeMs();
        if (view_ == View::Timer && timerRunning_) {
            uint32_t dt = now - lastTick_;
            remainingMs_ = dt >= remainingMs_ ? 0 : remainingMs_ - dt;
            if (remainingMs_ == 0) {
                timerRunning_ = false;
                timerDone_ = true;
                shell_.rebuild();   // the buttons and the status line change
            }
        }
        lastTick_ = now;
        if (clock_) {
            std::string text = view_ == View::Timer ? formatClock(remainingMs_, false) : formatClock(watchMs(now), true);
            if (text != shownClock_) {
                shownClock_ = text;
                lv_label_set_text(clock_, text.c_str());
            }
        }
    }

    bool onKey(uint32_t key, uint8_t) override {
        if (view_ == View::Hub) {
            if (key == '1') { open(View::Timer); return true; }
            if (key == '2') { open(View::Stopwatch); return true; }
            if (key == '3') { open(View::Calculator); return true; }
            return false;
        }
        if (view_ == View::Timer) {
            if (key == ' ') { toggleTimer(); return true; }
            if (key == 'r' || key == 'R') { setPreset(preset_); return true; }
            return false;
        }
        if (view_ == View::Stopwatch) {
            if (key == ' ') { toggleStopwatch(); return true; }
            if (key == 'l' || key == 'L') { lap(); return true; }
            if (key == 'r' || key == 'R') { resetStopwatch(); return true; }
            return false;
        }
        return calcKey(key);
    }

    bool onBack() override {
        if (view_ == View::Hub) return false;
        view_ = View::Hub;
        shell_.rebuild();
        return true;
    }

    const char* hint() const override {
        if (view_ == View::Timer) return "SPACE STARTS OR PAUSES  |  R RESETS  |  ESC BACK";
        if (view_ == View::Stopwatch) return "SPACE STARTS OR STOPS  |  L LAP  |  R RESETS  |  ESC BACK";
        if (view_ == View::Calculator) return "DIGITS AND + - * /  |  ENTER OR = WORKS IT OUT  |  C CLEARS  |  ESC BACK";
        return "CHOOSE A TOOL  |  ARROWS + ENTER";
    }

private:
    enum class View { Hub, Timer, Stopwatch, Calculator };

    void open(View v) {
        view_ = v;
        if (v == View::Calculator) calcClear();
        shell_.rebuild();
    }

    // ---- hub -----------------------------------------------------------------------------------

    void buildHub(Frame& frame) {
        std::vector<ui::HubItem> items;
        items.push_back({kTimer, "COUNT DOWN, THEN ALARM", [this] { open(View::Timer); }});
        items.push_back({kStopwatch, "START, STOP, LAPS", [this] { open(View::Stopwatch); }});
        items.push_back({kCalculator, "ADD, TAKE AWAY, TIMES, DIVIDE", [this] { open(View::Calculator); }});
        ui::hub(shell_, frame.body, frame.w, frame.h, items, 3);
        if (lv_obj_get_child_count(frame.body) > 0) shell_.preferFocus(lv_obj_get_child(frame.body, 0));
    }

    // ---- timer ---------------------------------------------------------------------------------

    void toggleTimer() {
        if (timerDone_ || remainingMs_ == 0) return;
        timerRunning_ = !timerRunning_;
        lastTick_ = shell_.platform().uptimeMs();
        shell_.rebuild();
    }

    void setPreset(uint32_t minutes) {
        preset_ = minutes;
        remainingMs_ = minutes * 60 * 1000;
        timerRunning_ = timerDone_ = false;
        shell_.rebuild();
    }

    void buildTimer(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap;
        shownClock_ = formatClock(remainingMs_, false);
        clock_ = ui::label(frame.body, shownClock_, fonts().huge, t.ink);
        lv_obj_set_pos(clock_, 0, 40);

        const char* state = timerDone_ ? "TIME'S UP" : timerRunning_ ? "RUNNING" : remainingMs_ < preset_ * 60000 ? "PAUSED" : "READY";
        status_ = ui::label(frame.body, state, fonts().body, timerDone_ ? t.warn : t.ink);
        lv_obj_set_pos(status_, 0, 190);

        lv_obj_t* presets = ui::box(frame.body, 0, 260, frame.w, ui::kTouchBig);
        lv_obj_set_flex_flow(presets, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(presets, g, 0);
        const int pw = (frame.w - 3 * g) / 4;
        for (uint32_t m : kPresetsMin) {
            lv_obj_t* b = ui::button(shell_, presets, std::to_string(m) + " MIN", [this, m] { setPreset(m); }, pw, ui::kTouchBig);
            if (m == preset_) lv_obj_set_style_bg_color(b, t.line, 0);
        }

        lv_obj_t* controls = ui::box(frame.body, 0, 260 + ui::kTouchBig + g, frame.w, ui::kTouchBig);
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(controls, g, 0);
        ui::button(shell_, controls, timerRunning_ ? "PAUSE" : "START", [this] { toggleTimer(); }, (frame.w - g) / 2, ui::kTouchBig);
        ui::button(shell_, controls, "RESET", [this] { setPreset(preset_); }, (frame.w - g) / 2, ui::kTouchBig);
    }

    // ---- stopwatch -----------------------------------------------------------------------------

    uint32_t watchMs(uint32_t now) const {
        return watchRunning_ ? watchAccumMs_ + (now - watchStartedAt_) : watchAccumMs_;
    }

    void toggleStopwatch() {
        const uint32_t now = shell_.platform().uptimeMs();
        if (watchRunning_) watchAccumMs_ = watchMs(now);
        else watchStartedAt_ = now;
        watchRunning_ = !watchRunning_;
        shell_.rebuild();
    }

    void lap() {
        if (watchAccumMs_ == 0 && !watchRunning_) return;
        laps_.push_back(watchMs(shell_.platform().uptimeMs()));
        if (laps_.size() > 50) laps_.erase(laps_.begin());
        shell_.rebuild();
    }

    void resetStopwatch() {
        watchRunning_ = false;
        watchAccumMs_ = 0;
        laps_.clear();
        shell_.rebuild();
    }

    void buildStopwatch(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap;
        shownClock_ = formatClock(watchMs(shell_.platform().uptimeMs()), true);
        clock_ = ui::label(frame.body, shownClock_, fonts().huge, t.ink);
        lv_obj_set_pos(clock_, 0, 40);

        const char* state = watchRunning_ ? "RUNNING" : watchAccumMs_ > 0 ? "STOPPED" : "READY";
        status_ = ui::label(frame.body, state, fonts().body, t.ink);
        lv_obj_set_pos(status_, 0, 190);

        lv_obj_t* controls = ui::box(frame.body, 0, 260, frame.w, ui::kTouchBig);
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(controls, g, 0);
        const int bw = (frame.w - 2 * g) / 3;
        ui::button(shell_, controls, watchRunning_ ? "STOP" : "START", [this] { toggleStopwatch(); }, bw, ui::kTouchBig);
        ui::button(shell_, controls, "LAP", [this] { lap(); }, bw, ui::kTouchBig);
        ui::button(shell_, controls, "RESET", [this] { resetStopwatch(); }, bw, ui::kTouchBig);

        std::string text;
        size_t first = laps_.size() > kLapsKept ? laps_.size() - kLapsKept : 0;
        for (size_t i = first; i < laps_.size(); i++)
            text += "LAP " + std::to_string(i + 1) + "   " + formatClock(laps_[i], true) + "\n";
        lapsLabel_ = ui::label(frame.body, text.empty() ? "LAPS APPEAR HERE" : text, fonts().small, t.muted);
        lv_obj_set_pos(lapsLabel_, 0, 260 + ui::kTouchBig + g);
    }

    // ---- calculator ----------------------------------------------------------------------------

    void calcClear() {
        acc_ = 0;
        op_ = 0;
        entry_ = "0";
        fresh_ = true;
        error_ = false;
    }

    double entryValue() const { return strtod(entry_.c_str(), nullptr); }

    // Works out "acc op rhs". A division by zero shows ERROR until C.
    void calcApply(double rhs) {
        if (op_ == '+') acc_ += rhs;
        else if (op_ == '-') acc_ -= rhs;
        else if (op_ == '*') acc_ *= rhs;
        else if (op_ == '/') {
            if (rhs == 0) { error_ = true; entry_ = "ERROR"; return; }
            acc_ /= rhs;
        }
        entry_ = formatNumber(acc_);
    }

    // Every calculator key goes through here, from the touch buttons and from the keyboard.
    bool calcPress(char c) {
        if (error_ && c != 'C') return true;
        if ((c >= '0' && c <= '9') || c == '.') {
            if (fresh_) { entry_ = "0"; fresh_ = false; }
            if (c == '.') {
                if (entry_.find('.') == std::string::npos) entry_ += '.';
            } else if (entry_ == "0") {
                entry_ = std::string(1, c);
            } else if (entry_.size() < kMaxDigits) {
                entry_ += c;
            }
        } else if (c == '+' || c == '-' || c == '*' || c == '/') {
            if (op_ && !fresh_) calcApply(entryValue());
            else if (!op_) acc_ = entryValue();
            if (!error_) {
                op_ = c;
                entry_ = formatNumber(acc_);
                fresh_ = true;
            }
        } else if (c == '=') {
            if (op_) {
                calcApply(fresh_ ? acc_ : entryValue());
                op_ = 0;
                fresh_ = true;
            }
        } else if (c == 'C') {
            calcClear();
        } else if (c == 'B') {
            if (!fresh_) {
                if (entry_.size() > 1) entry_.pop_back();
                else entry_ = "0";
            }
        }
        if (display_) lv_label_set_text(display_, entry_.c_str());
        return true;
    }

    bool calcKey(uint32_t key) {
        if (key == LV_KEY_ENTER) return calcPress('=');
        if (key == LV_KEY_BACKSPACE) return calcPress('B');
        if (key >= 128) return false;
        char c = (char)key;
        if (c == 'c') c = 'C';
        if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == 'C')
            return calcPress(c);
        return false;
    }

    void buildCalculator(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap;
        lv_obj_t* panel = ui::panel(shell_, frame.body, 0, 0, frame.w, 120);
        display_ = ui::label(panel, entry_, fonts().huge, t.ink);
        lv_obj_align(display_, LV_ALIGN_RIGHT_MID, -g, 0);

        static const char* rows[5][4] = {
            {"7", "8", "9", "/"},
            {"4", "5", "6", "*"},
            {"1", "2", "3", "-"},
            {"0", ".", "=", "+"},
            {"C", "BKSP", nullptr, nullptr},
        };
        const int bw = (frame.w - 3 * g) / 4, bh = ui::kTouchMin;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 4; c++) {
                const char* label = rows[r][c];
                if (!label) continue;
                const char key = !strcmp(label, "BKSP") ? 'B' : label[0];
                lv_obj_t* b = ui::button(shell_, frame.body, label, [this, key] { calcPress(key); }, bw, bh);
                lv_obj_set_pos(b, c * (bw + g), 140 + r * (bh + g));
            }
    }

    // ---- state ---------------------------------------------------------------------------------

    View view_ = View::Hub;
    lv_obj_t* clock_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* lapsLabel_ = nullptr;
    lv_obj_t* display_ = nullptr;
    std::string shownClock_;
    uint32_t lastTick_ = 0;

    // Timer
    uint32_t preset_ = 5;                // minutes
    uint32_t remainingMs_ = 5 * 60 * 1000;
    bool timerRunning_ = false;
    bool timerDone_ = false;

    // Stopwatch
    uint32_t watchAccumMs_ = 0;
    uint32_t watchStartedAt_ = 0;
    bool watchRunning_ = false;
    std::vector<uint32_t> laps_;

    // Calculator
    double acc_ = 0;
    char op_ = 0;
    std::string entry_ = "0";
    bool fresh_ = true;
    bool error_ = false;
};

} // namespace

std::unique_ptr<App> makeToolsApp(Shell& shell) { return std::make_unique<ToolsApp>(shell); }

} // namespace meshdeck
