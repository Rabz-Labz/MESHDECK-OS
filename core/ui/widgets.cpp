// widgets.cpp - see widgets.h.
#include "ui/widgets.h"
#include "ui/fonts.h"
#include "ui/shell.h"

namespace meshdeck {
namespace ui {

// The handler is stored on the heap; LV_EVENT_DELETE frees it when the object goes.
void onClick(lv_obj_t* obj, std::function<void()> fn) {
    auto* stored = new std::function<void()>(std::move(fn));
    lv_obj_add_event_cb(
        obj, [](lv_event_t* e) { (*static_cast<std::function<void()>*>(lv_event_get_user_data(e)))(); },
        LV_EVENT_CLICKED, stored);
    lv_obj_add_event_cb(
        obj, [](lv_event_t* e) { delete static_cast<std::function<void()>*>(lv_event_get_user_data(e)); },
        LV_EVENT_DELETE, stored);
}

void onEvent(lv_obj_t* obj, lv_event_code_t code, std::function<void()> fn) {
    auto* stored = new std::function<void()>(std::move(fn));
    lv_obj_add_event_cb(
        obj, [](lv_event_t* e) { (*static_cast<std::function<void()>*>(lv_event_get_user_data(e)))(); }, code, stored);
    lv_obj_add_event_cb(
        obj, [](lv_event_t* e) { delete static_cast<std::function<void()>*>(lv_event_get_user_data(e)); },
        LV_EVENT_DELETE, stored);
}

lv_obj_t* textField(Shell& shell, lv_obj_t* parent, int w, const std::string& placeholder, size_t maxLength) {
    const Theme& t = shell.th();
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, (uint32_t)maxLength);
    lv_textarea_set_placeholder_text(ta, placeholder.c_str());
    lv_obj_set_width(ta, w);
    lv_obj_set_style_text_font(ta, fonts().body, 0);
    lv_obj_set_style_text_color(ta, t.ink, 0);
    lv_obj_set_style_text_color(ta, t.muted, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, t.surface, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, t.line, 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_border_color(ta, t.ink, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 4, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_pad_all(ta, 14, 0);
    lv_obj_set_style_bg_color(ta, t.ink, LV_PART_CURSOR);   // a block cursor in the theme colour
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
    shell.focusable(ta);
    return ta;
}

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    // Not clickable: a layout box must never catch taps meant for a button underneath it (the
    // title row's action box once covered BACK this way).
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* label(lv_obj_t* parent, const std::string& text, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text.c_str());
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

// Shared look for anything pressable: surface fill and a thin border; a thick ink border when it
// has keyboard focus; the border colour fills it while pressed.
static void stylePressable(Shell& shell, lv_obj_t* o) {
    const Theme& t = shell.th();
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, t.surface, 0);
    lv_obj_set_style_bg_color(o, t.line, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(o, t.line, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, t.ink, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(o, 4, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(o, t.ink, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* button(Shell& shell, lv_obj_t* parent, const std::string& text, std::function<void()> fn, int w, int h) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, w, h < kTouchMin ? kTouchMin : h);
    stylePressable(shell, b);
    lv_obj_t* l = label(b, text, fonts().body, shell.th().ink);
    lv_obj_center(l);
    onClick(b, std::move(fn));
    shell.focusable(b);
    return b;
}

lv_obj_t* panel(Shell& shell, lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* p = box(parent, x, y, w, h);
    const Theme& t = shell.th();
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, t.surface, 0);
    lv_obj_set_style_border_color(p, t.line, 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_pad_all(p, kGap, 0);
    return p;
}

lv_obj_t* tile(Shell& shell, lv_obj_t* parent, int x, int y, int w, int h, const std::string& key,
               const std::string& name, const std::string& subtitle, std::function<void()> fn) {
    const Theme& t = shell.th();
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    stylePressable(shell, b);
    lv_obj_set_style_pad_all(b, kGap, 0);
    if (!key.empty()) {
        lv_obj_t* k = label(b, key, fonts().body, t.muted);
        lv_obj_align(k, LV_ALIGN_TOP_RIGHT, 0, -6);
    }
    lv_obj_t* n = label(b, name, fonts().title, t.ink);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, -14);
    lv_obj_t* s = label(b, subtitle, fonts().small, t.muted);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s, w - 2 * kGap - 8);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    onClick(b, std::move(fn));
    shell.focusable(b);
    return b;
}

lv_obj_t* list(Shell& shell, lv_obj_t* parent, int x, int y, int w, int h, const std::vector<Row>& rows) {
    const Theme& t = shell.th();
    lv_obj_t* l = box(parent, x, y, w, h);
    lv_obj_add_flag(l, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));   // a drag scrolls it
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l, 8, 0);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_AUTO);
    for (const Row& r : rows) {
        lv_obj_t* row = lv_button_create(l);
        lv_obj_set_size(row, w - 12, kTouchMin + 8);
        stylePressable(shell, row);
        lv_obj_set_style_pad_hor(row, kGap, 0);
        lv_obj_t* txt = label(row, r.text, fonts().body, t.ink);
        lv_label_set_long_mode(txt, LV_LABEL_LONG_DOT);
        lv_obj_set_width(txt, r.detail.empty() ? w - 60 : w - 340);
        lv_obj_align(txt, LV_ALIGN_LEFT_MID, 0, 0);
        if (!r.detail.empty()) {
            lv_obj_t* d = label(row, r.detail, fonts().small, t.muted);
            lv_obj_align(d, LV_ALIGN_RIGHT_MID, 0, 0);
        }
        if (r.fn) onClick(row, r.fn);
        shell.focusable(row);   // focusable even without an action, so the list can be scrolled by key
    }
    return l;
}

void hub(Shell& shell, lv_obj_t* parent, int w, int h, const std::vector<HubItem>& items, int columns) {
    int n = (int)items.size();
    if (n == 0) return;
    int rows = (n + columns - 1) / columns;
    int tw = (w - (columns - 1) * kGap) / columns, th = (h - (rows - 1) * kGap) / rows;
    for (int i = 0; i < n; i++) {
        int c = i % columns, r = i / columns;
        tile(shell, parent, c * (tw + kGap), r * (th + kGap), tw, th, "", items[i].name, items[i].subtitle,
             items[i].fn);
    }
}

} // namespace ui
} // namespace meshdeck
