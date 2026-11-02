// boot_splash.cpp - the splash shown at power-on: green drips fall and light the words "MESHDECK - OS".
// Once the words are complete no new drips start, and the ones falling run off the bottom, leaving
// only the words on black; then the words fade out and the normal start (lock screen or setup, then
// home) takes over. Any key or tap skips it. The rain
// rules are in drip_rain.h; this file only places the boxes and shows the lit letter cells.
//
// The drips and the letter cells are boxes, moved and shown as the rain changes. Boxes are cheaper on
// the Tab5 than redrawing a full-screen picture each frame.
#include <memory>
#include <vector>
#include "ui/drip_rain.h"
#include "ui/overlay.h"
#include "ui/shell.h"
#include "ui/widgets.h"
#include "platform/platform.h"

namespace meshdeck {
namespace {

const int kCell = 14;              // one grid cell, a pixel of the word
const int kTrailRows = 6;          // how many cells a drip's trail shows behind its head
const uint32_t kRedrawMs = 33;     // the rain is moved about 30 times a second
const uint32_t kHoldMs = 600;      // the words alone on black stay this long before fading
const uint32_t kFadeMs = 1000;     // then fade out over this long

class BootSplash : public Overlay {
public:
    using Overlay::Overlay;

    void build(lv_obj_t* parent, int w, int h) override {
        const Theme& t = shell_.th();
        cols_ = w / kCell;
        rows_ = h / kCell;
        if (!rain_) {
            uint32_t seed = 1;
            shell_.platform().randomBytes(reinterpret_cast<uint8_t*>(&seed), sizeof seed);
            rain_ = std::make_unique<DripRain>(cols_, rows_, seed | 1);
            lastTick_ = shell_.platform().uptimeMs();
        }
        // A full-size box under everything: a tap anywhere skips the splash.
        lv_obj_t* background = ui::box(parent, 0, 0, w, h);
        lv_obj_set_style_bg_color(background, t.bg, 0);
        lv_obj_set_style_bg_opa(background, LV_OPA_COVER, 0);
        lv_obj_add_flag(background, LV_OBJ_FLAG_CLICKABLE);
        ui::onClick(background, [this] { finish(); });

        heads_.clear();
        trails_.clear();
        for (int c = 0; c < cols_; c++) {
            lv_obj_t* head = ui::box(parent, c * kCell, 0, kCell, kCell);
            lv_obj_set_style_bg_color(head, t.ink, 0);
            lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
            heads_.push_back(head);
            lv_obj_t* trail = ui::box(parent, c * kCell, 0, kCell, kCell);
            lv_obj_set_style_bg_color(trail, t.line, 0);
            lv_obj_set_style_bg_opa(trail, LV_OPA_COVER, 0);
            trails_.push_back(trail);
        }
        // The letter cells: each one is shown once the rain has lit it.
        letters_.assign((size_t)cols_ * rows_, nullptr);
        for (int r = 0; r < rows_; r++)
            for (int c = 0; c < cols_; c++) {
                if (!rain_->isWordCell(c, r)) continue;
                lv_obj_t* cell = ui::box(parent, c * kCell, r * kCell, kCell, kCell);
                lv_obj_set_style_bg_color(cell, t.ink, 0);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
                letters_[(size_t)r * cols_ + c] = cell;
            }
        placeDrips();
        revealLetters();
    }

    void tick() override {
        if (!rain_ || finished_) return;
        const uint32_t now = shell_.platform().uptimeMs();
        switch (phase_) {
            case Phase::Forming:
            case Phase::Draining: {
                double dt = (now - lastTick_) / 1000.0;
                lastTick_ = now;
                rain_->step(dt);
                if (phase_ == Phase::Forming && rain_->done()) {
                    rain_->stop();   // the words are complete: let the drips run off the screen
                    phase_ = Phase::Draining;
                }
                bool drained = rain_->drained();
                if (drained || now - lastDraw_ >= kRedrawMs) {
                    lastDraw_ = now;
                    placeDrips();
                    revealLetters();
                }
                if (drained) {
                    phase_ = Phase::Holding;
                    phaseAt_ = now;
                }
                break;
            }
            case Phase::Holding:
                if (now - phaseAt_ >= kHoldMs) {
                    phase_ = Phase::Fading;
                    phaseAt_ = now;
                }
                break;
            case Phase::Fading:
                if (now - phaseAt_ >= kFadeMs) {
                    finish();
                } else if (now - lastDraw_ >= kRedrawMs) {
                    lastDraw_ = now;
                    fadeLetters((lv_opa_t)(255 - 255 * (now - phaseAt_) / kFadeMs));
                }
                break;
        }
    }

    bool onKey(uint32_t, uint8_t) override {
        finish();
        return true;
    }

    const char* hint() const override { return "PRESS ANY KEY OR TAP TO SKIP"; }
    bool allowsShortcuts() const override { return false; }

private:
    // Moves each drip's head and trail to where the rain says they are.
    void placeDrips() {
        for (int c = 0; c < cols_ && c < (int)heads_.size(); c++) {
            double head = rain_->headRow(c);
            if (head < 0) {
                lv_obj_add_flag(heads_[c], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(trails_[c], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            int row = (int)head;
            lv_obj_clear_flag(heads_[c], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(heads_[c], c * kCell, row * kCell);
            int top = row - kTrailRows;
            int trailH = (row - (top < 0 ? 0 : top)) * kCell;
            if (trailH <= 0) {
                lv_obj_add_flag(trails_[c], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(trails_[c], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(trails_[c], c * kCell, (top < 0 ? 0 : top) * kCell);
                lv_obj_set_size(trails_[c], kCell, trailH);
            }
        }
    }

    // Shows each letter cell the rain has lit. A cell is shown once and then stays.
    void revealLetters() {
        for (int r = 0; r < rows_; r++)
            for (int c = 0; c < cols_; c++) {
                lv_obj_t* cell = letters_[(size_t)r * cols_ + c];
                if (cell && rain_->lit(c, r) && lv_obj_has_flag(cell, LV_OBJ_FLAG_HIDDEN))
                    lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
            }
    }

    // Sets every letter cell's opacity, for the fade at the end.
    void fadeLetters(lv_opa_t opa) {
        for (lv_obj_t* cell : letters_)
            if (cell) lv_obj_set_style_bg_opa(cell, opa, 0);
    }

    void finish() {
        if (finished_) return;
        finished_ = true;
        shell_.finishBootSplash();   // queued: the normal start takes over after this call returns
    }

    std::unique_ptr<DripRain> rain_;
    int cols_ = 0, rows_ = 0;
    std::vector<lv_obj_t*> heads_, trails_, letters_;
    enum class Phase { Forming, Draining, Holding, Fading };
    Phase phase_ = Phase::Forming;
    uint32_t lastTick_ = 0, lastDraw_ = 0, phaseAt_ = 0;
    bool finished_ = false;
};

} // namespace

std::unique_ptr<Overlay> makeBootSplash(Shell& shell) { return std::make_unique<BootSplash>(shell); }

} // namespace meshdeck
