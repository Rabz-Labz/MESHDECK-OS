// widgets.h - MESHDECK's building blocks, themed and sized for gloved fingers.
//
// Apps build their screens only from these, so every screen looks and behaves the same way:
//   - touch targets are at least kTouchMin tall, frequent controls kTouchBig;
//   - everything that can be pressed is also reachable by keyboard (the shell's focus list);
//   - colours come from the current theme.
//
// Click handlers are std::function, so apps can use lambdas that capture what they need. A handler
// runs inside LVGL's event processing: anything that rebuilds the screen must go through
// Shell::later() (the shell's helpers already do).
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "lvgl.h"

namespace meshdeck {

class Shell;

namespace ui {

constexpr int kGap = 16;          // space between controls
constexpr int kTouchMin = 64;     // smallest touch target (px)
constexpr int kTouchBig = 96;     // frequent controls: zoom, page turn, send

// Calls fn when obj is clicked (touch release, or ENTER while it has keyboard focus). The
// function is freed with the object.
void onClick(lv_obj_t* obj, std::function<void()> fn);

// Calls fn when obj gets the LVGL event code (e.g. LV_EVENT_READY when ENTER is pressed in a
// one-line text box). The function is freed with the object.
void onEvent(lv_obj_t* obj, lv_event_code_t code, std::function<void()> fn);

// A one-line themed text box, added to the keyboard focus list. While it has focus, typing goes
// into it (the Tab5 keyboard switches to text mode); ENTER sends LV_EVENT_READY.
lv_obj_t* textField(Shell& shell, lv_obj_t* parent, int w, const std::string& placeholder, size_t maxLength);

// A plain container with no padding, border, background or scrolling.
lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h);

// A text label in the given font and colour.
lv_obj_t* label(lv_obj_t* parent, const std::string& text, const lv_font_t* font, lv_color_t color);

// A themed push button, added to the keyboard focus list. Size defaults to a standard button.
lv_obj_t* button(Shell& shell, lv_obj_t* parent, const std::string& text, std::function<void()> fn,
                 int w = 200, int h = kTouchMin);

// A themed panel (surface colour, thin border) for grouping content.
lv_obj_t* panel(Shell& shell, lv_obj_t* parent, int x, int y, int w, int h);

// A large tile: key hint in the corner, name, and a one-line subtitle. Used by the home screen
// and by hub screens (LOGBOOK, SETTINGS).
lv_obj_t* tile(Shell& shell, lv_obj_t* parent, int x, int y, int w, int h, const std::string& key,
               const std::string& name, const std::string& subtitle, std::function<void()> fn);

// A scrollable list of large rows. Each row is a button reachable by keyboard.
struct Row {
    std::string text;
    std::string detail;            // shown dim on the right (size, date...)
    std::function<void()> fn;      // nullptr: not pressable
};
lv_obj_t* list(Shell& shell, lv_obj_t* parent, int x, int y, int w, int h, const std::vector<Row>& rows);

// The hub template: a grid of tiles filling the area, for screens that are menus of sub-screens.
struct HubItem {
    std::string name, subtitle;
    std::function<void()> fn;
};
void hub(Shell& shell, lv_obj_t* parent, int w, int h, const std::vector<HubItem>& items, int columns = 2);

} // namespace ui
} // namespace meshdeck
