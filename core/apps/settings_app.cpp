// settings_app.cpp - SETTINGS: a hub of DISPLAY, CLOCK and ABOUT.
//
//   DISPLAY  theme, brightness, take a screenshot
//   CLOCK    set the date and time (UK local time)
//   ABOUT    version, platform, storage, licences; CREDITS opens the author's logo and sentence
// Timers and the stopwatch (part of CLOCK in the plan) arrive in V7.
#include <memory>
#include "ui/credits.h"
#include "ui/clock_setter.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"
#include "version.h"

namespace meshdeck {

class SettingsApp : public App {
public:
    using App::App;
    const char* name() const override { return "SETTINGS"; }
    const char* subtitle() const override { return "Display, clock, about"; }
    void onOpen() override { part_ = Hub; }

    void build(Frame& frame) override {
        brightLabel_ = nullptr;
        switch (part_) {
        case Hub: buildHub(frame); break;
        case Display: buildDisplay(frame); break;
        case Clock: buildClock(frame); break;
        case About: buildAbout(frame); break;
        case Credits: buildCredits(frame); break;
        }
    }

    bool onBack() override {
        if (part_ == Hub) return false;
        part_ = part_ == Credits ? About : Hub;   // CREDITS sits under ABOUT
        shell_.rebuild();
        return true;
    }

    bool onKey(uint32_t key, uint8_t) override {
        if (part_ == Hub && key >= '1' && key <= '3') { open(Part(key - '0')); return true; }
        if (part_ == Display && (key == '+' || key == '=')) { change(+10); return true; }
        if (part_ == Display && key == '-') { change(-10); return true; }
        return false;
    }

    const char* hint() const override {
        switch (part_) {
        case Hub: return "1-3 OPEN A PART  |  ARROWS + ENTER";
        case Display: return "+/- BRIGHTNESS  |  CTRL+N THEME  |  CTRL+P SCREENSHOT";
        default: return nullptr;
        }
    }

private:
    enum Part { Hub, Display, Clock, About, Credits };   // 1-3 on the hub open Display, Clock, About

    void open(Part p) {
        part_ = p;
        if (p == Clock) clock_.start(shell_.nowUtc());
        shell_.rebuild();
    }

    void buildHub(Frame& frame) {
        int64_t now = shell_.nowUtc();
        char clock[40] = "TIME NOT SET";
        if (now) formatLocal(now, clock, sizeof clock);
        ui::hub(shell_, frame.body, frame.w, frame.h,
                {{"DISPLAY", std::string("Theme ") + shell_.th().name + ", brightness " + std::to_string(shell_.brightness()) + "%",
                  [this] { open(Display); }},
                 {"CLOCK", clock, [this] { open(Clock); }},
                 {"ABOUT", std::string("MESHDECK ") + MESHDECK_CR_VERSION, [this] { open(About); }}},
                3);
    }

    void buildDisplay(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap;
        lv_obj_t* p = ui::panel(shell_, frame.body, 0, 0, frame.w, frame.h);
        int inner = frame.w - 2 * g - 4;
        ui::label(p, "THEME", fonts().body, t.muted);
        lv_obj_t* themes = ui::box(p, 0, 40, inner, ui::kTouchBig);
        lv_obj_set_flex_flow(themes, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(themes, g, 0);
        for (int i = 0; i < (int)ThemeId::Count; i++) {
            ThemeId id = (ThemeId)i;
            lv_obj_t* b = ui::button(shell_, themes, theme(id).name, [this, id] { shell_.setTheme(id); },
                                     (inner - 2 * g) / 3, ui::kTouchBig);
            if (id == shell_.themeId()) lv_obj_set_style_bg_color(b, t.line, 0);
        }
        lv_obj_t* bl = ui::label(p, "BRIGHTNESS", fonts().body, t.muted);
        lv_obj_set_pos(bl, 0, 170);
        lv_obj_t* minus = ui::button(shell_, p, "-", [this] { change(-10); }, ui::kTouchBig + 40, ui::kTouchBig);
        lv_obj_set_pos(minus, 0, 210);
        brightLabel_ = ui::label(p, "", fonts().title, t.ink);
        lv_obj_set_pos(brightLabel_, ui::kTouchBig + 40 + 3 * g, 232);
        lv_obj_t* plus = ui::button(shell_, p, "+", [this] { change(+10); }, ui::kTouchBig + 40, ui::kTouchBig);
        lv_obj_set_pos(plus, ui::kTouchBig + 40 + 220, 210);
        showBrightness();
        lv_obj_t* shot = ui::button(shell_, p, "TAKE SCREENSHOT", [this] { shell_.screenshot(); }, 420, ui::kTouchMin + 8);
        lv_obj_align(shot, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    void buildClock(Frame& frame) {
        const Theme& t = shell_.th();
        lv_obj_t* l = ui::label(frame.body, "UK LOCAL TIME. GMT OR BST IS WORKED OUT FOR YOU.", fonts().body, t.muted);
        lv_obj_set_pos(l, 0, 0);
        clock_.build(shell_, frame.body, 50, frame.w);
        lv_obj_t* save = ui::button(shell_, frame.body, "SAVE CLOCK", [this] { saveClock(); }, 320, ui::kTouchMin + 8);
        lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_t* later = ui::label(frame.body, "TIMERS AND STOPWATCH: V7", fonts().small, t.muted);
        lv_obj_align(later, LV_ALIGN_BOTTOM_LEFT, 0, -20);
    }

    void buildAbout(Frame& frame) {
        const Theme& t = shell_.th();
        lv_obj_t* p = ui::panel(shell_, frame.body, 0, 0, frame.w, frame.h);
        std::string text = std::string("MESHDECK CR // OS ") + MESHDECK_CR_VERSION +
                           "\nCR: THE CIVILIAN RESILIENCE EDITION\nRUNNING ON " + shell_.platform().name() +
                           "\nSTORAGE " + (shell_.paths().available() ? shell_.platform().dataRoot() : "NONE") +
                           "\n\nFREE SOFTWARE UNDER THE MIT LICENCE.\nINCLUDES LVGL (MIT) AND THE VT323 FONT (SIL OFL)."
                           "\nMAP DATA: CONTAINS OS DATA (C) CROWN COPYRIGHT AND DATABASE RIGHT.";
        ui::label(p, text, fonts().body, t.ink);
        lv_obj_t* credits = ui::button(shell_, p, "CREDITS", [this] { open(Credits); }, 300, ui::kTouchMin + 8);
        lv_obj_align(credits, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    // The author's logo (a GIF, up to the page's width) across the top, their words below it.
    void buildCredits(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap, logoH = 220, innerW = frame.w - 2 * g - 4;
        lv_obj_t* p = ui::panel(shell_, frame.body, 0, 0, frame.w, frame.h);
        lv_obj_set_style_pad_all(p, g, 0);
        lv_obj_t* logoArea = ui::box(p, 0, 0, innerW, logoH);
        if (kCreditsLogoGifSize > 0) {
            // LVGL reads the GIF straight from the firmware: a descriptor pointing at its bytes.
            static lv_image_dsc_t logo;
            logo.header.magic = LV_IMAGE_HEADER_MAGIC;
            logo.header.cf = LV_COLOR_FORMAT_RAW;
            logo.data = kCreditsLogoGif;
            logo.data_size = kCreditsLogoGifSize;
            lv_obj_t* gif = lv_gif_create(logoArea);
            lv_gif_set_src(gif, &logo);
            lv_obj_center(gif);
        } else {
            lv_obj_set_style_border_color(logoArea, t.line, 0);
            lv_obj_set_style_border_width(logoArea, 2, 0);
            lv_obj_t* note = ui::label(logoArea, "LOGO", fonts().title, t.muted);
            lv_obj_center(note);
        }
        lv_obj_t* text = ui::label(p, kCreditsText, fonts().body, t.ink);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(text, innerW);
        lv_obj_set_pos(text, 0, logoH + g);
    }

    void saveClock() {
        if (shell_.platform().rtcWrite(clock_.utc())) {
            shell_.notify("CLOCK SET");
            part_ = Hub;
            shell_.rebuild();
        } else {
            shell_.notify("THE CLOCK COULDN'T BE SET ON THIS DEVICE");
        }
    }

    void change(int delta) {
        shell_.setBrightness(shell_.brightness() + delta);
        showBrightness();
    }
    void showBrightness() {
        if (brightLabel_) lv_label_set_text(brightLabel_, (std::to_string(shell_.brightness()) + "%").c_str());
    }

    Part part_ = Hub;
    ClockSetter clock_;
    lv_obj_t* brightLabel_ = nullptr;
};

std::unique_ptr<App> makeSettingsApp(Shell& shell) { return std::make_unique<SettingsApp>(shell); }

} // namespace meshdeck
