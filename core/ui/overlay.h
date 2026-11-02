// overlay.h - full-screen modes that sit above the home screen and apps.
//
// The lock screen and the first-start setup are overlays. While one is showing, the shell builds
// it instead of home or the open app (the status bar stays), sends it keys first, and turns off
// the tile keys. Closing it brings back exactly what was underneath.
#pragma once
#include <stdint.h>
#include <memory>
#include "lvgl.h"

namespace meshdeck {

class Shell;

class Overlay {
public:
    explicit Overlay(Shell& shell) : shell_(shell) {}
    virtual ~Overlay() = default;
    // Fills the area under the status bar (parent is w x h, origin at its top-left).
    virtual void build(lv_obj_t* parent, int w, int h) = 0;
    virtual void tick() {}
    // Keys reach the overlay before focus navigation. Return true if used.
    virtual bool onKey(uint32_t key, uint8_t mods) { (void)key; (void)mods; return false; }
    virtual const char* hint() const { return nullptr; }
    // The lock screen blocks Ctrl shortcuts (screenshot, theme); setup allows them.
    virtual bool allowsShortcuts() const { return true; }

protected:
    Shell& shell_;
};

std::unique_ptr<Overlay> makeLockScreen(Shell& shell);     // ui/lock_screen.cpp
std::unique_ptr<Overlay> makeSetupWizard(Shell& shell);    // apps/setup_wizard.cpp
std::unique_ptr<Overlay> makeBootSplash(Shell& shell);     // ui/boot_splash.cpp

} // namespace meshdeck
