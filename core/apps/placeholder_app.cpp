// placeholder_app.cpp - see placeholder_app.h.
#include "apps/placeholder_app.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {

void PlaceholderApp::draw(Shell& shell, lv_obj_t* parent, int w, int h, const PlannedApp& info) {
    const Theme& t = shell.th();
    lv_obj_t* p = ui::panel(shell, parent, 0, 0, w, h);
    lv_obj_t* head = ui::label(p, std::string("ARRIVES IN ") + info.milestone, fonts().title, t.ink);
    lv_obj_align(head, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t* text = ui::label(p, info.description, fonts().body, t.muted);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text, w - 4 * ui::kGap);
    lv_obj_align(text, LV_ALIGN_TOP_LEFT, 0, 72);
    lv_obj_t* note = ui::label(p, "MESHDECK 1.X HAS A WORKING VERSION OF THIS TODAY.", fonts().small, t.muted);
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void PlaceholderApp::build(Frame& frame) { draw(shell_, frame.body, frame.w, frame.h, info_); }

} // namespace meshdeck
