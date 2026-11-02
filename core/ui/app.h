// app.h - what every MESHDECK app looks like to the shell.
//
// The shell owns the screen frame: the status bar at the top, the title row (BACK, the app's
// title, and the app's action buttons on the right) and the hint line at the bottom. An app fills
// the body in build(). Because the shell rebuilds the whole screen when the theme changes or the
// app asks (Shell::rebuild), apps keep their state in member variables, never only in widgets.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "lvgl.h"

namespace meshdeck {

class Shell;

// The frame the shell builds around an app's body.
class Frame {
public:
    Frame(Shell& shell, lv_obj_t* body, int w, int h, lv_obj_t* actions)
        : shell_(shell), body(body), w(w), h(h), actions_(actions) {}
    // Adds a large button to the right of the title row. Buttons appear left to right in the order
    // added.
    lv_obj_t* action(const std::string& label, std::function<void()> fn);

private:
    Shell& shell_;

public:
    lv_obj_t* body;   // the app's area; (0,0) is its top-left corner
    int w, h;         // its size in pixels

private:
    lv_obj_t* actions_;
};

class App {
public:
    explicit App(Shell& shell) : shell_(shell) {}
    virtual ~App() = default;

    virtual const char* name() const = 0;        // tile name and screen title, e.g. "MESSAGES"
    virtual const char* subtitle() const = 0;    // one line under the name on the home tile

    virtual void onOpen() {}                     // the app's screen is about to be shown
    virtual void onClose() {}                    // leaving the app: save anything pending
    virtual void build(Frame& frame) = 0;        // fill frame.body (called on every rebuild)
    virtual void tick() {}                       // every main-loop pass while open
    // A key the shell didn't use for itself. Return true if the app used it.
    virtual bool onKey(uint32_t key, uint8_t mods) { (void)key; (void)mods; return false; }
    // ESC or BACK. Return true if the app went back a step itself (e.g. closed a sub-screen);
    // false lets the shell return to the home screen.
    virtual bool onBack() { return false; }
    // The hint line for this screen, or nullptr for the standard one.
    virtual const char* hint() const { return nullptr; }
    // A short note shown on the app's home tile instead of its subtitle, e.g. "3 UNREAD".
    // "" = show the subtitle. Checked once a second.
    virtual std::string tileNote() const { return ""; }

protected:
    Shell& shell_;
};

} // namespace meshdeck
