// placeholder_app.h - stands in for an app that a later milestone builds.
//
// It says plainly what the app will do and when it arrives, rather than showing a half-working
// screen.
#pragma once
#include <string>
#include "ui/app.h"

namespace meshdeck {

struct PlannedApp {
    const char* name;
    const char* subtitle;
    const char* milestone;      // e.g. "V3"
    const char* description;    // a few lines on what it will do
};

class PlaceholderApp : public App {
public:
    PlaceholderApp(Shell& shell, PlannedApp info) : App(shell), info_(info) {}
    const char* name() const override { return info_.name; }
    const char* subtitle() const override { return info_.subtitle; }
    void build(Frame& frame) override;

    // Draws the "arrives in" panel into any area (also used by hub sub-screens).
    static void draw(Shell& shell, lv_obj_t* parent, int w, int h, const PlannedApp& info);

private:
    PlannedApp info_;
};

} // namespace meshdeck
