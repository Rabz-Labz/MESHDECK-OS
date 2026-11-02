// diagnostics_app.cpp - DIAGNOSTICS: live system and power figures, and the 1.x diagnostic pages:
// a touch test, a key log and the radio.
#include <stdio.h>
#include <deque>
#include <memory>
#include <string>
#include "apps/apps.h"
#include "services/mesh.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"
#include "version.h"

namespace meshdeck {

class DiagnosticsApp : public App {
public:
    DiagnosticsApp(Shell& shell, Services& services) : App(shell), mesh_(services.mesh) {}
    const char* name() const override { return "DIAGNOSTICS"; }
    const char* subtitle() const override { return "Power, storage, system"; }
    void onOpen() override { last_ = 0; view_ = View::Main; }

    void build(Frame& frame) override {
        // Each rebuild deletes the old objects, so forget the pointers first.
        power_ = system_ = radio_ = keyLabel_ = nullptr;
        switch (view_) {
        case View::Main: buildMain(frame); break;
        case View::Touch: buildTouch(frame); break;
        case View::Keys: buildKeys(frame); break;
        case View::Radio: buildRadio(frame); break;
        }
    }

    void tick() override {
        uint32_t now = shell_.platform().uptimeMs();
        if (now - last_ >= 1000) {
            last_ = now;
            if (view_ == View::Main) refresh();
            if (view_ == View::Radio) refreshRadio();
        }
    }

    // The key log takes every key the shell doesn't use; ESC still goes back (onBack).
    bool onKey(uint32_t key, uint8_t mods) override {
        if (view_ != View::Keys) return false;
        keyLog_.push_front(describeKey(key, mods));
        if (keyLog_.size() > kKeyLines) keyLog_.pop_back();
        if (keyLabel_) {
            std::string text;
            for (const std::string& line : keyLog_) text += line + "\n";
            lv_label_set_text(keyLabel_, text.c_str());
        }
        return true;
    }

    bool onBack() override {
        if (view_ == View::Main) return false;
        view_ = View::Main;
        shell_.rebuild();
        return true;
    }

private:
    enum class View { Main, Touch, Keys, Radio };
    static constexpr size_t kKeyLines = 14;
    static constexpr int kTouchCols = 16, kTouchRows = 8;

    void buildMain(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap, half = (frame.w - g) / 2, panelH = frame.h - ui::kTouchBig - g;
        lv_obj_t* power = ui::panel(shell_, frame.body, 0, 0, half, panelH);
        ui::label(power, "POWER", fonts().title, t.ink);
        power_ = ui::label(power, "", fonts().body, t.ink);
        lv_obj_set_pos(power_, 0, 70);
        lv_obj_t* sys = ui::panel(shell_, frame.body, half + g, 0, half, panelH);
        ui::label(sys, "SYSTEM", fonts().title, t.ink);
        system_ = ui::label(sys, "", fonts().body, t.ink);
        lv_obj_set_pos(system_, 0, 70);
        refresh();

        const int by = frame.h - ui::kTouchBig, bw = (frame.w - 2 * g) / 3;
        openButton(frame, "TOUCH TEST", View::Touch, 0, by, bw);
        openButton(frame, "KEY LOG", View::Keys, bw + g, by, bw);
        openButton(frame, "RADIO", View::Radio, 2 * (bw + g), by, bw);
    }

    void openButton(Frame& frame, const char* text, View to, int x, int y, int w) {
        lv_obj_t* b = ui::button(shell_, frame.body, text, [this, to] { view_ = to; shell_.rebuild(); }, w, ui::kTouchBig);
        lv_obj_set_pos(b, x, y);
    }

    void backButton(Frame& frame) {
        lv_obj_t* b = ui::button(shell_, frame.body, "BACK", [this] { onBack(); }, 200, ui::kTouchBig);
        lv_obj_set_pos(b, 0, frame.h - ui::kTouchBig);
    }

    // Sixteen by eight boxes; each turns solid when touched, so dead spots show as boxes that stay empty.
    void buildTouch(Frame& frame) {
        const Theme& t = shell_.th();
        const int labelH = 40, areaH = frame.h - ui::kTouchBig - labelH;
        lv_obj_t* hint = ui::label(frame.body, "TOUCH TEST: TAP EVERY BOX. BACK LEAVES.", fonts().small, t.muted);
        lv_obj_set_pos(hint, 0, 0);
        const int cw = frame.w / kTouchCols, ch = areaH / kTouchRows;
        for (int r = 0; r < kTouchRows; r++)
            for (int c = 0; c < kTouchCols; c++) {
                lv_obj_t* cell = lv_obj_create(frame.body);
                lv_obj_remove_style_all(cell);
                lv_obj_set_size(cell, cw - 2, ch - 2);
                lv_obj_set_pos(cell, c * cw + 1, labelH + r * ch + 1);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_color(cell, t.bg, 0);
                lv_obj_set_style_border_width(cell, 2, 0);
                lv_obj_set_style_border_color(cell, t.muted, 0);
                lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
                ui::onEvent(cell, LV_EVENT_PRESSED, [this, cell] { lv_obj_set_style_bg_color(cell, shell_.th().ink, 0); });
            }
        backButton(frame);
    }

    void buildKeys(Frame& frame) {
        const Theme& t = shell_.th();
        lv_obj_t* hint = ui::label(frame.body, "KEY LOG: PRESS KEYS. NEWEST FIRST. BACK LEAVES.", fonts().small, t.muted);
        lv_obj_set_pos(hint, 0, 0);
        keyLabel_ = ui::label(frame.body, "", fonts().body, t.ink);
        lv_obj_set_pos(keyLabel_, 0, 60);
        backButton(frame);
    }

    void buildRadio(Frame& frame) {
        const Theme& t = shell_.th();
        lv_obj_t* hint = ui::label(frame.body, "RADIO: MESHTASTIC LINK AND CHANNELS.", fonts().small, t.muted);
        lv_obj_set_pos(hint, 0, 0);
        radio_ = ui::label(frame.body, "", fonts().body, t.ink);
        lv_obj_set_pos(radio_, 0, 60);
        refreshRadio();
        backButton(frame);
    }

    static std::string describeKey(uint32_t key, uint8_t mods) {
        char buf[96];
        if (key >= 32 && key < 127) snprintf(buf, sizeof buf, "%3u  0x%02X  '%c'", (unsigned)key, (unsigned)key, (char)key);
        else snprintf(buf, sizeof buf, "%3u  0x%02X", (unsigned)key, (unsigned)key);
        std::string s = buf;
        if (mods) {
            snprintf(buf, sizeof buf, "  MODS 0x%02X", (unsigned)mods);
            s += buf;
        }
        return s;
    }

    void refresh() {
        if (!power_) return;
        Platform& p = shell_.platform();
        PowerMonitor& pm = shell_.power();
        int mv = 0, ma = 0;
        bool haveMv = p.batteryMillivolts(mv), haveMa = p.batteryMilliamps(ma);
        char buf[512];
        int mins = pm.minutesLeft(shell_.capacityMah());
        snprintf(buf, sizeof buf,
                 "VOLTAGE NOW   %s\nCURRENT NOW   %s\nAVERAGE 1 MIN %s\nPOWER         %s\nBATTERY       %s\n"
                 "TIME LEFT     %s\nSUPPLY        %s\n\nPACK SIZE %d MAH (SETTING)",
                 haveMv ? (std::to_string(mv / 1000) + "." + std::to_string(mv % 1000 / 10) + " V").c_str() : "NOT MEASURED",
                 haveMa ? (std::to_string(ma) + " MA").c_str() : "NOT MEASURED",
                 pm.haveCurrent() ? (std::to_string(pm.averageMilliamps()) + " MA").c_str() : "--",
                 haveMv && haveMa ? (std::to_string((long)mv * ma / 1000) + " MW").c_str() : "--",
                 pm.percent() < 0 ? "--" : (std::to_string(pm.percent()) + " %").c_str(),
                 mins < 0 ? "--" : (std::to_string(mins / 60) + " H " + std::to_string(mins % 60) + " MIN").c_str(),
                 pm.external() ? "EXTERNAL POWER" : "BATTERY", shell_.capacityMah());
        lv_label_set_text(power_, buf);
        uint32_t up = p.uptimeMs() / 1000;
        snprintf(buf, sizeof buf, "VERSION  %s\nPLATFORM %s\nUPTIME   %uh %02um %02us\nSTORAGE  %s\nEVENTS DROPPED %u",
                 MESHDECK_CR_VERSION, p.name(), (unsigned)(up / 3600), (unsigned)(up / 60 % 60), (unsigned)(up % 60),
                 shell_.paths().available() ? p.dataRoot().c_str() : "NONE", (unsigned)shell_.events().dropped());
        lv_label_set_text(system_, buf);
    }

    void refreshRadio() {
        if (!radio_) return;
        char buf[64];
        std::string text = std::string("LINK         ") + mesh_.linkText() + "\n";
        snprintf(buf, sizeof buf, "MY NODE      !%08x\n", (unsigned)mesh_.myNum());
        text += buf;
        text += "NODES HEARD  " + std::to_string(mesh_.nodes().size()) + "\n\nCHANNELS\n";
        for (const MeshService::Channel& c : mesh_.channels()) {
            text += "  " + std::to_string(c.index) + "  " + mesh_.channelName(c.index);
            if (c.role == 1) text += "  (PRIMARY)";
            text += "\n";
        }
        if (mesh_.channels().empty()) text += "  NONE YET\n";
        lv_label_set_text(radio_, text.c_str());
    }

    MeshService& mesh_;
    View view_ = View::Main;
    lv_obj_t* power_ = nullptr;
    lv_obj_t* system_ = nullptr;
    lv_obj_t* radio_ = nullptr;
    lv_obj_t* keyLabel_ = nullptr;
    std::deque<std::string> keyLog_;
    uint32_t last_ = 0;
};

std::unique_ptr<App> makeDiagnosticsApp(Shell& shell, Services& services) {
    return std::make_unique<DiagnosticsApp>(shell, services);
}

} // namespace meshdeck
