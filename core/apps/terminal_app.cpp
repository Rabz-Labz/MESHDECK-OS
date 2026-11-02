// terminal_app.cpp - TERMINAL: type a command, press ENTER, read the answer. The commands are in
// services/terminal.h; this screen keeps the output, shows it, and carries out CLEAR and SCREENSHOT.
// Type HELP to see the list.
//
// Running a command does not rebuild the screen. The answer's lines are added to the output box in
// place, so the RUN button that was just pressed is never torn down while it is still in use.
#include <memory>
#include <string>
#include <vector>
#include "apps/apps.h"
#include "services/mesh.h"
#include "services/terminal.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {
namespace {

const size_t kMaxLines = 200;        // the output kept on screen; the oldest lines go first
const size_t kMaxLineChars = 120;    // a longer line is cut to this
const size_t kMaxCommand = 80;       // the most a typed command can hold

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \r\n\t"), b = s.find_last_not_of(" \r\n\t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

class TerminalApp : public App {
public:
    TerminalApp(Shell& shell, MeshService& mesh) : App(shell), mesh_(mesh) {
        lines_.push_back("MESHDECK TERMINAL. TYPE HELP, THEN ENTER.");
    }

    const char* name() const override { return "TERMINAL"; }
    const char* subtitle() const override { return "Commands and logs"; }

    void build(Frame& frame) override {
        const Theme& t = shell_.th();
        const int g = ui::kGap;
        const int outH = frame.h - ui::kTouchMin - 2 * g;   // the output, above the command line
        outW_ = frame.w;

        // The output: one label per line, in a box that scrolls and always shows the newest line.
        out_ = ui::box(frame.body, 0, 0, frame.w, outH);
        lv_obj_add_flag(out_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_flex_flow(out_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(out_, 6, 0);
        shell_.focusable(out_);
        for (const std::string& line : lines_) addLabel(line, t);
        lv_obj_scroll_to_y(out_, LV_COORD_MAX, LV_ANIM_OFF);

        // The command line, with RUN beside it. ENTER runs the command too.
        const int rowY = outH + g, runW = 160;
        field_ = ui::textField(shell_, frame.body, frame.w - runW - g, "TYPE A COMMAND, E.G. HELP", kMaxCommand);
        lv_obj_set_pos(field_, 0, rowY);
        lv_obj_set_height(field_, ui::kTouchMin);
        lv_textarea_set_text(field_, draft_.c_str());
        ui::onEvent(field_, LV_EVENT_VALUE_CHANGED, [this] { draft_ = lv_textarea_get_text(field_); });
        ui::onEvent(field_, LV_EVENT_READY, [this] { runDraft(); });
        lv_obj_t* run = ui::button(shell_, frame.body, "RUN", [this] { runDraft(); }, runW, ui::kTouchMin);
        lv_obj_set_pos(run, frame.w - runW, rowY);
        shell_.preferFocus(field_);
    }

    const char* hint() const override { return "TYPE A COMMAND, THEN ENTER  |  HELP LISTS THEM  |  ESC BACK"; }

private:
    // One line as a label at the bottom of the output box, scrolled into view.
    void addLabel(const std::string& text, const Theme& t) {
        if (!out_) return;
        lv_obj_t* l = ui::label(out_, text, fonts().small, t.ink);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, outW_ - 2 * ui::kGap);
    }

    // Adds one line to the output, cutting it to size and keeping only the newest kMaxLines.
    void print(const std::string& text) {
        lines_.push_back(text.substr(0, kMaxLineChars));
        addLabel(lines_.back(), shell_.th());
        if (lines_.size() > kMaxLines) {
            lines_.erase(lines_.begin());
            if (out_ && lv_obj_get_child_count(out_) > 0) lv_obj_delete(lv_obj_get_child(out_, 0));
        }
        if (out_) lv_obj_scroll_to_y(out_, LV_COORD_MAX, LV_ANIM_OFF);
    }

    // Runs what is typed: the command is echoed, then its answer printed. Only the output and the
    // command line change; the screen itself is not rebuilt.
    void runDraft() {
        std::string typed = trimmed(draft_);
        draft_.clear();
        if (field_) lv_textarea_set_text(field_, "");
        if (typed.empty()) return;
        print("> " + typed);
        Terminal::Context ctx{shell_.platform(), mesh_, shell_.power(), shell_.capacityMah(),
                              [this] { return shell_.nowUtc(); }};
        Terminal::Result result = Terminal::run(typed, ctx);
        if (result.action == Terminal::Action::Clear) {
            lines_.clear();
            if (out_) lv_obj_clean(out_);
            return;
        }
        for (const std::string& line : result.lines) print(line);
        if (result.action == Terminal::Action::Screenshot) shell_.screenshot();
    }

    MeshService& mesh_;
    std::vector<std::string> lines_;   // the output on screen
    std::string draft_;                // the command being typed
    lv_obj_t* out_ = nullptr;          // the output box, while this screen is shown
    lv_obj_t* field_ = nullptr;        // the command line, while this screen is shown
    int outW_ = 0;                     // the width of the output box
};

} // namespace

std::unique_ptr<App> makeTerminalApp(Shell& shell, MeshService& mesh) { return std::make_unique<TerminalApp>(shell, mesh); }

} // namespace meshdeck
