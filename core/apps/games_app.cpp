// games_app.cpp - GAMES: a hub of games and their best scores, and RALLY (pong against the CPU).
// The rules are in games/pong.h; this file draws the court and reads the keys.
//
// Keys on RALLY: W/S or UP/DOWN move the paddle (it keeps moving for a moment after each press, since
// the keyboard sends presses, not holds); SPACE starts a match or a new one. ESC goes back.
#include <math.h>
#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>
#include "apps/apps.h"
#include "games/pong.h"
#include "games/klondike.h"
#include "games/sea_battle.h"
#include "kernel/clock.h"
#include "kernel/storage.h"
#include "services/high_scores.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {
namespace {

const char* kRally = "RALLY";
const char* kSeaBattle = "SEA BATTLE";
const char* kKlondike = "KLONDIKE";

// The court on screen: the 1000 x 600 game field scaled by kScale, centred. Positions are measured
// from the top of the content area (below the title row), which is about 560 pixels tall.
const double kScale = 0.6;
const int kCourtW = 600, kCourtH = 360;     // 1000 x 600 units at kScale
const int kFieldX = (1280 - kCourtW) / 2, kFieldY = 56;
const int kSeaCell = 28, kSeaPitch = 30;   // a square on SEA BATTLE, and its spacing
const uint32_t kRedrawMs = 33;        // the court is redrawn about 30 times a second
const uint32_t kHeldMs = 160;         // a key press moves the paddle this long (keys send presses, not holds)

std::string lower(char c) { return std::string(1, (char)tolower((unsigned char)c)); }

class GamesApp : public App {
public:
    using App::App;

    const char* name() const override { return "GAMES"; }
    const char* subtitle() const override { return "Rally, sea battle, klondike"; }

    void onOpen() override {
        view_ = View::Hub;
        Paths p = shell_.paths();
        if (p.available()) {
            makeDirs(p.folder(Folder::Games));
            scoresOk_ = scores_.load(p.file(Folder::Games, "scores.txt"));
        }
    }

    void build(Frame& frame) override {
        court_ = nullptr;
        ball_ = nullptr;
        paddles_[0] = paddles_[1] = nullptr;
        scoreLabels_[0] = scoreLabels_[1] = status_ = nullptr;
        for (auto& side : seaCells_)
            for (lv_obj_t*& cell : side) cell = nullptr;
        seaStatus_ = seaShots_ = nullptr;
        if (view_ == View::Rally) buildRally(frame);
        else if (view_ == View::Sea) buildSea(frame);
        else if (view_ == View::Klondike) buildKlondike(frame);
        else buildHub(frame);
    }

    void tick() override {
        if (view_ != View::Rally) return;
        const uint32_t now = shell_.platform().uptimeMs();
        double dt = (now - lastTick_) / 1000.0;
        lastTick_ = now;
        int move = now < moveUntil_ ? moveDir_ : 0;
        Pong::Phase before = pong_.phase();
        pong_.step(dt, move);
        if (before != Pong::Phase::Over && pong_.phase() == Pong::Phase::Over) matchEnded();
        if (court_ && now - lastDraw_ >= kRedrawMs) {
            lastDraw_ = now;
            drawCourt();
        }
    }

    bool onKey(uint32_t key, uint8_t) override {
        if (view_ == View::Hub) {
            // 1, 2 and 3 open RALLY, SEA BATTLE and KLONDIKE from the hub (as LOGBOOK's 1-4 open its parts).
            if (key == '1') { open(View::Rally); return true; }
            if (key == '2') { open(View::Sea); return true; }
            if (key == '3') { open(View::Klondike); return true; }
            return false;
        }
        if (view_ == View::Sea) return seaKey(key);
        if (view_ == View::Klondike) return klonKey(key);
        const uint32_t now = shell_.platform().uptimeMs();
        if (key == LV_KEY_UP || (key < 128 && lower((char)key) == "w")) {
            moveDir_ = -1;
            moveUntil_ = now + kHeldMs;
            return true;
        }
        if (key == LV_KEY_DOWN || (key < 128 && lower((char)key) == "s")) {
            moveDir_ = 1;
            moveUntil_ = now + kHeldMs;
            return true;
        }
        if (key == ' ' || key == LV_KEY_ENTER) {   // ENTER also starts, so it never presses BACK here
            if (pong_.phase() == Pong::Phase::Ready || pong_.phase() == Pong::Phase::Over) {
                pong_.start();
                shell_.rebuild();   // the status line and the hint change
            }
            return true;
        }
        return false;
    }

    bool onBack() override {
        if (view_ == View::Hub) return false;
        view_ = View::Hub;
        shell_.rebuild();
        return true;
    }

    const char* hint() const override {
        if (view_ == View::Rally) return "W/S OR UP/DOWN MOVE  |  SPACE STARTS A MATCH  |  ESC BACK";
        if (view_ == View::Sea) return "ARROWS MOVE  |  ENTER FIRES  |  N FOR A NEW FLEET  |  ESC BACK";
        if (view_ == View::Klondike)
            return "ARROWS MOVE  |  ENTER PICKS UP AND PUTS DOWN  |  D DRAW  |  U UNDO  |  N NEW  |  ESC BACK";
        return "CHOOSE A GAME  |  ARROWS + ENTER";
    }

private:
    enum class View { Hub, Rally, Sea, Klondike };

    // ---- hub -----------------------------------------------------------------------------------

    std::string bestText(const char* game) const {
        int best = scoresOk_ ? scores_.best(game) : 0;
        return best ? "BEST " + std::to_string(best) : "NO SCORE YET";
    }

    void buildHub(Frame& frame) {
        std::vector<ui::HubItem> items;
        items.push_back({kRally, bestText(kRally), [this] { open(View::Rally); }});
        items.push_back({kSeaBattle, bestText(kSeaBattle), [this] { open(View::Sea); }});
        items.push_back({kKlondike, bestText(kKlondike), [this] { open(View::Klondike); }});
        ui::hub(shell_, frame.body, frame.w, frame.h, items, 2);
        if (lv_obj_get_child_count(frame.body) > 0) shell_.preferFocus(lv_obj_get_child(frame.body, 0));
    }

    void open(View v) {
        view_ = v;
        if (v == View::Sea && seaGame_.phase() == SeaBattle::Phase::Ready) seaGame_.start();
        if (v == View::Klondike) klonSelLoc_ = -1;   // nothing is picked up when the game is shown again
        shell_.rebuild();
    }

    // ---- SEA BATTLE ----------------------------------------------------------------------------
    // Two 10 x 10 seas: YOUR FLEET (left), where the CPU fires, and the CPU's sea (right), where you
    // fire. Squares are updated in place when a shot is fired, so the screen is not rebuilt; a tap
    // on a square fires at it, the same as moving the cursor there and pressing ENTER.

    static std::string squareName(int i) {
        return std::string(1, (char)('A' + i % SeaBattle::kSize)) + std::to_string(i / SeaBattle::kSize + 1);
    }

    void buildSea(Frame& frame) {
        const Theme& t = shell_.th();
        const int pitch = kSeaPitch, gridW = SeaBattle::kSize * pitch;
        const int leftX = 180, rightX = frame.w - 180 - gridW, gridY = 64;
        const char* titles[2] = {"YOUR FLEET", "CPU SEA: FIRE HERE"};
        for (int side = 0; side < 2; side++) {
            int x0 = side == 0 ? leftX : rightX;
            lv_obj_t* title = ui::label(frame.body, titles[side], fonts().small, t.muted);
            lv_obj_set_pos(title, x0, gridY - 30);
            for (int i = 0; i < SeaBattle::kCells; i++) {
                lv_obj_t* cell = ui::box(frame.body, x0 + (i % SeaBattle::kSize) * pitch, gridY + (i / SeaBattle::kSize) * pitch,
                                         kSeaCell, kSeaCell);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                if (side == 1) {
                    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
                    ui::onClick(cell, [this, i] {
                        seaCursor_ = i;
                        fireAt(i);
                    });
                }
                seaCells_[side][i] = cell;
            }
        }
        seaStatus_ = ui::label(frame.body, "", fonts().body, t.ink);
        lv_obj_set_pos(seaStatus_, leftX, gridY + gridW + 18);
        lv_obj_set_width(seaStatus_, frame.w - 2 * leftX);
        seaShots_ = ui::label(frame.body, "", fonts().small, t.muted);
        lv_obj_set_pos(seaShots_, leftX, gridY + gridW + 60);
        refreshSea();
    }

    // Colours the squares from the game state, and the message and shot count.
    void refreshSea() {
        if (!seaStatus_) return;
        const Theme& t = shell_.th();
        for (int i = 0; i < SeaBattle::kCells; i++) {
            const SeaBattle::Sea& mine = seaGame_.player();
            bool fired = mine.fired[i], ship = mine.ship[i] >= 0;
            setSquare(seaCells_[0][i], fired ? (ship ? t.warn : t.bg) : (ship ? t.line : t.surface), false);
            const SeaBattle::Sea& theirs = seaGame_.cpu();
            bool shot = theirs.fired[i], hit = theirs.ship[i] >= 0;
            setSquare(seaCells_[1][i], shot ? (hit ? t.warn : t.line) : t.surface, i == seaCursor_);   // a miss shows as a grey square
        }
        std::string message = seaMessage_;
        if (message.empty()) message = "CHOOSE A SQUARE ON THE RIGHT, THEN ENTER TO FIRE.";
        lv_label_set_text(seaStatus_, message.c_str());
        std::string shots = "SHOTS " + std::to_string(seaGame_.shots());
        lv_label_set_text(seaShots_, shots.c_str());
    }

    void setSquare(lv_obj_t* cell, lv_color_t colour, bool cursor) {
        lv_obj_set_style_bg_color(cell, colour, 0);
        lv_obj_set_style_border_width(cell, cursor ? 3 : 0, 0);
        lv_obj_set_style_border_color(cell, shell_.th().ink, 0);
    }

    void fireAt(int i) {
        SeaBattle::Shot shot = seaGame_.fire(i);
        if (shot == SeaBattle::Shot::Repeat) {
            if (seaGame_.phase() == SeaBattle::Phase::Playing) shell_.notify("YOU HAVE FIRED THERE ALREADY");
            return;
        }
        const char* yours = shot == SeaBattle::Shot::Hit ? "HIT." : shot == SeaBattle::Shot::Sunk ? "YOU SANK A SHIP." : "MISS.";
        seaMessage_ = "YOU FIRED AT " + squareName(i) + ": " + yours;
        if (seaGame_.lastCpuShot() >= 0) seaMessage_ += " THE CPU FIRED AT " + squareName(seaGame_.lastCpuShot()) + ".";
        refreshSea();
        if (seaGame_.phase() == SeaBattle::Phase::Over) seaEnded();
    }

    // Records a won game in the best scores (fewest shots is best), and says how it ended.
    void seaEnded() {
        if (!seaGame_.playerWon()) {
            seaMessage_ = "THE CPU SANK YOUR FLEET. N FOR A NEW FLEET.";
            refreshSea();
            return;
        }
        bool best = scores_.record(kSeaBattle, seaGame_.points());
        if (scoresOk_) {
            Paths p = shell_.paths();
            if (p.available()) makeDirs(p.folder(Folder::Games));
            if (best && !scores_.save()) shell_.notify("BEST SCORE NOT SAVED: THE CARD WRITE FAILED");
        }
        seaMessage_ = "YOU SANK THE FLEET IN " + std::to_string(seaGame_.shots()) + " SHOTS. N FOR A NEW FLEET.";
        if (best) shell_.notify("NEW BEST: " + std::to_string(seaGame_.points()) + " POINTS");
        refreshSea();
    }

    void newFleet() {
        seaGame_.start();
        seaMessage_.clear();
        refreshSea();
    }

    // Keys on SEA BATTLE: arrows move the cursor, ENTER or SPACE fires, N starts a new game.
    bool seaKey(uint32_t key) {
        int row = seaCursor_ / SeaBattle::kSize, col = seaCursor_ % SeaBattle::kSize;
        if (key == LV_KEY_LEFT) col = std::max(0, col - 1);
        else if (key == LV_KEY_RIGHT) col = std::min(SeaBattle::kSize - 1, col + 1);
        else if (key == LV_KEY_UP) row = std::max(0, row - 1);
        else if (key == LV_KEY_DOWN) row = std::min(SeaBattle::kSize - 1, row + 1);
        else if (key == LV_KEY_ENTER || key == ' ') {
            fireAt(seaCursor_);
            return true;
        } else if (key < 128 && lower((char)key) == "n") {
            newFleet();
            return true;
        } else {
            return false;
        }
        seaCursor_ = row * SeaBattle::kSize + col;
        refreshSea();
        return true;
    }

    // ---- KLONDIKE ------------------------------------------------------------------------------
    // The layout: the stock and waste at the top left, the four foundations at the top right, and
    // seven columns below. The cursor moves over 13 places: stock, waste, the foundations, the
    // columns. ENTER picks up a card (or a run, from a column) and ENTER on another place puts it
    // down if the rules allow. The screen is rebuilt after each key, since keys are the only input here.

    static constexpr int kKlonLocs = 13;
    static constexpr int kCardW = 92, kCardH = 120, kCardGap = 16, kColPitch = 110, kColY = 150;

    static std::string cardLabel(const Klondike::Card& c) {
        static const char* ranks[] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
        static const char suits[] = {'S', 'H', 'D', 'C'};
        return std::string(ranks[c.rank]) + suits[c.suit];
    }

    // The top-left corner of a place on the screen, from the content area's top-left.
    void placeXY(int loc, int width, int& x, int& y) const {
        if (loc == 0) { x = 40; y = 0; }
        else if (loc == 1) { x = 40 + kCardW + kCardGap; y = 0; }
        else if (loc < 6) { x = width - 40 - kCardW - (5 - loc) * (kCardW + kCardGap); y = 0; }
        else { x = (width - (6 * kColPitch + kCardW)) / 2 + (loc - 6) * kColPitch; y = kColY; }
    }

    lv_obj_t* drawCard(lv_obj_t* parent, int x, int y, const Klondike::Card* c, bool selected) {
        const Theme& t = shell_.th();
        lv_obj_t* card = ui::box(parent, x, y, kCardW, kCardH);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, selected ? 3 : 1, 0);
        lv_obj_set_style_border_color(card, selected ? t.ink : t.line, 0);
        lv_obj_set_style_bg_color(card, t.bg, 0);
        if (!c) return card;                       // an empty place: just the outline
        if (!c->up) {
            lv_obj_set_style_bg_color(card, t.line, 0);   // face down: the back of the card
            return card;
        }
        lv_obj_set_style_bg_color(card, t.surface, 0);
        bool redSuit = c->suit == 1 || c->suit == 2;
        lv_obj_t* label = ui::label(card, cardLabel(*c), fonts().body, redSuit ? t.warn : t.ink);
        lv_obj_set_pos(label, 8, 6);
        return card;
    }

    void buildKlondike(Frame& frame) {
        const Theme& t = shell_.th();
        // The stock, the waste and the foundations (the top row), then the columns.
        const Klondike::Card* top = nullptr;
        if (!klon_.stock().empty()) {
            Klondike::Card back = klon_.stock().back();
            drawCard(frame.body, 40, 0, &back, false);
        } else {
            drawCard(frame.body, 40, 0, nullptr, false);
        }
        int x = 0, y = 0;
        placeXY(1, frame.w, x, y);
        top = klon_.waste().empty() ? nullptr : &klon_.waste().back();
        drawCard(frame.body, x, y, top, selectedTop(1));
        for (int f = 0; f < Klondike::kFoundations; f++) {
            placeXY(2 + f, frame.w, x, y);
            top = klon_.foundation(f).empty() ? nullptr : &klon_.foundation(f).back();
            drawCard(frame.body, x, y, top, selectedTop(2 + f));
        }
        for (int c = 0; c < Klondike::kColumns; c++) {
            placeXY(6 + c, frame.w, x, y);
            const auto& column = klon_.column(c);
            if (column.empty()) {
                drawCard(frame.body, x, y, nullptr, false);
                continue;
            }
            int n = (int)column.size();
            int step = n <= 1 ? 0 : std::max(10, std::min(30, 310 / (n - 1)));   // fewer pixels when a column is tall
            for (int i = 0; i < n; i++) {
                bool selected = klonSelLoc_ == 6 + c && i >= klonSelIdx_ && column[i].up;
                drawCard(frame.body, x, y + i * step, &column[i], selected);
            }
        }
        // The cursor: an outline over the place it is on.
        placeXY(klonCursor_, frame.w, x, y);
        lv_obj_t* cursor = ui::box(frame.body, x - 4, y - 4, kCardW + 8, kCardH + 8);
        lv_obj_set_style_bg_opa(cursor, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cursor, 2, 0);
        lv_obj_set_style_border_color(cursor, t.ink, 0);

        // The score and the message.
        std::string moves = "MOVES " + std::to_string(klon_.moves()) + "   ON THE FOUNDATIONS " +
                            std::to_string(klon_.cardsOnFoundations()) + " OF 52";
        lv_obj_t* counts = ui::label(frame.body, moves, fonts().small, t.muted);
        lv_obj_set_pos(counts, 40, frame.h - 66);
        std::string message = klon_.won() ? "YOU WON IN " + std::to_string(klon_.moves()) + " MOVES. N FOR A NEW GAME." : klonMessage_;
        lv_obj_t* status = ui::label(frame.body, message, fonts().body, t.ink);
        lv_obj_set_pos(status, 40, frame.h - 36);
        lv_obj_set_width(status, frame.w - 80);
    }

    // Whether the top card of a place is the one picked up.
    bool selectedTop(int loc) const { return klonSelLoc_ == loc; }

    // The cards a place can give up: a waste or foundation top, or a face-up run in a column.
    bool canPickUp(int loc) const {
        if (loc == 1) return !klon_.waste().empty();
        if (loc >= 2 && loc <= 5) return !klon_.foundation(loc - 2).empty();
        if (loc >= 6) {
            const auto& column = klon_.column(loc - 6);
            return !column.empty() && column.back().up;
        }
        return false;
    }

    // The first face-up card of a column, which is the longest run that can be picked up.
    int firstFaceUp(int col) const {
        const auto& column = klon_.column(col);
        int i = 0;
        while (i < (int)column.size() && !column[i].up) i++;
        return i;
    }

    // Picks the place up, or puts the picked-up cards down there.
    void klonPress() {
        const int loc = klonCursor_;
        if (klonSelLoc_ < 0) {
            if (!canPickUp(loc)) {
                klonMessage_ = "NOTHING THERE TO PICK UP.";
                return;
            }
            klonSelLoc_ = loc;
            klonSelIdx_ = loc >= 6 ? firstFaceUp(loc - 6) : 0;
            klonMessage_ = "PICKED UP. MOVE TO A PLACE AND PRESS ENTER TO PUT DOWN.";
            return;
        }
        if (loc == klonSelLoc_) {
            klonSelLoc_ = -1;
            klonMessage_ = "PUT BACK.";
            return;
        }
        bool moved = false;
        const int src = klonSelLoc_;
        if (src == 1) {
            if (loc >= 6) moved = klon_.wasteToColumn(loc - 6);
            else if (loc >= 2 && loc <= 5) moved = klon_.wasteToFoundation(loc - 2);
        } else if (src >= 2 && src <= 5) {
            if (loc >= 6) moved = klon_.foundationToColumn(src - 2, loc - 6);
        } else if (src >= 6) {
            int col = src - 6;
            if (loc >= 6) moved = klon_.moveRun(col, klonSelIdx_, loc - 6);
            else if (loc >= 2 && loc <= 5) {
                // Only the single top card can go to a foundation.
                if (klonSelIdx_ == (int)klon_.column(col).size() - 1) moved = klon_.columnToFoundation(col, loc - 2);
            }
        }
        klonSelLoc_ = -1;
        if (!moved) {
            klonMessage_ = "NOT ALLOWED THERE.";
            return;
        }
        klonMessage_.clear();
        if (klon_.won()) klonWon();
    }

    // Records a won game: the fewest moves is best, so it is kept as 1000 minus the moves.
    void klonWon() {
        int points = std::max(1, 1000 - klon_.moves());
        bool best = scores_.record(kKlondike, points);
        if (scoresOk_) {
            Paths p = shell_.paths();
            if (p.available()) makeDirs(p.folder(Folder::Games));
            if (best && !scores_.save()) shell_.notify("BEST SCORE NOT SAVED: THE CARD WRITE FAILED");
        }
        if (best) shell_.notify("NEW BEST: " + std::to_string(klon_.moves()) + " MOVES");
    }

    bool klonKey(uint32_t key) {
        if (key == LV_KEY_LEFT) klonCursor_ = std::max(0, klonCursor_ - 1);
        else if (key == LV_KEY_RIGHT) klonCursor_ = std::min(kKlonLocs - 1, klonCursor_ + 1);
        else if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
            // While a column's run is picked up, UP and DOWN choose how much of it to take.
            if (klonSelLoc_ >= 6) {
                const int col = klonSelLoc_ - 6;
                if (key == LV_KEY_UP) klonSelIdx_ = std::max(firstFaceUp(col), klonSelIdx_ - 1);
                else klonSelIdx_ = std::min((int)klon_.column(col).size() - 1, klonSelIdx_ + 1);
            }
        } else if (key == LV_KEY_ENTER || key == ' ') {
            if (klonCursor_ == 0) {
                if (!klon_.draw()) klonMessage_ = "NOTHING LEFT TO DRAW.";
                else klonMessage_.clear();
            } else {
                klonPress();
            }
        } else if (key < 128 && lower((char)key) == "d") {
            if (!klon_.draw()) klonMessage_ = "NOTHING LEFT TO DRAW.";
            else klonMessage_.clear();
        } else if (key < 128 && lower((char)key) == "u") {
            klonSelLoc_ = -1;
            klonMessage_ = klon_.undo() ? "UNDONE." : "NOTHING TO UNDO.";
        } else if (key < 128 && lower((char)key) == "n") {
            klon_.newGame();
            klonSelLoc_ = -1;
            klonMessage_ = "A NEW DEAL.";
        } else {
            return false;
        }
        shell_.rebuild();   // keys are the only input here, so a rebuild is safe
        return true;
    }

    // ---- RALLY ---------------------------------------------------------------------------------

    void buildRally(Frame& frame) {
        const Theme& t = shell_.th();
        lastTick_ = shell_.platform().uptimeMs();
        lastDraw_ = 0;

        // The score line above the court.
        const int scoreY = kFieldY - 44;
        scoreLabels_[0] = ui::label(frame.body, "YOU 0", fonts().title, t.ink);
        lv_obj_set_pos(scoreLabels_[0], kFieldX, scoreY);
        scoreLabels_[1] = ui::label(frame.body, "CPU 0", fonts().title, t.ink);
        lv_obj_set_pos(scoreLabels_[1], kFieldX + kCourtW - 160, scoreY);
        lv_obj_set_style_text_align(scoreLabels_[1], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(scoreLabels_[1], 160);

        // The court: a border, a dashed centre line, two paddles and the ball.
        court_ = ui::panel(shell_, frame.body, kFieldX, kFieldY, kCourtW, kCourtH);
        for (int y = 8; y < kCourtH; y += 26) {
            lv_obj_t* dash = ui::box(court_, kCourtW / 2 - 2, y, 4, 14);
            lv_obj_set_style_bg_color(dash, t.line, 0);
            lv_obj_set_style_bg_opa(dash, LV_OPA_COVER, 0);
        }
        for (int i = 0; i < 2; i++) {
            paddles_[i] = ui::box(frame.body, 0, 0, 1, 1);
            lv_obj_set_style_bg_color(paddles_[i], t.ink, 0);
            lv_obj_set_style_bg_opa(paddles_[i], LV_OPA_COVER, 0);
        }
        ball_ = ui::box(frame.body, 0, 0, 1, 1);
        lv_obj_set_style_bg_color(ball_, t.ink, 0);
        lv_obj_set_style_bg_opa(ball_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(ball_, LV_RADIUS_CIRCLE, 0);

        status_ = ui::label(frame.body, "", fonts().small, t.muted);
        lv_obj_set_pos(status_, kFieldX, kFieldY + kCourtH + 14);
        lv_obj_set_width(status_, kCourtW);
        lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
        refreshText();
        drawCourt();
        shell_.preferFocus(nullptr);
    }

    // Places the paddles and the ball on screen from the game state.
    void drawCourt() {
        if (!court_) return;
        const double s = kScale, half = Pong::kPaddleH / 2;
        auto place = [&](lv_obj_t* o, double x, double y, double w, double h) {
            lv_obj_set_pos(o, kFieldX + (int)lround(x * s), kFieldY + (int)lround(y * s));
            lv_obj_set_size(o, (int)lround(w * s), (int)lround(h * s));
        };
        place(paddles_[0], Pong::kPaddleInset, pong_.playerY() - half, Pong::kPaddleW, Pong::kPaddleH);
        place(paddles_[1], Pong::kWidth - Pong::kPaddleInset - Pong::kPaddleW, pong_.cpuY() - half, Pong::kPaddleW, Pong::kPaddleH);
        place(ball_, pong_.ballX() - Pong::kBall, pong_.ballY() - Pong::kBall, 2 * Pong::kBall, 2 * Pong::kBall);
        refreshText();
    }

    // The scores and the status line, when they change.
    void refreshText() {
        if (!scoreLabels_[0]) return;
        char you[24], cpu[24];
        snprintf(you, sizeof you, "YOU %d", pong_.playerScore());
        snprintf(cpu, sizeof cpu, "CPU %d", pong_.cpuScore());
        if (shownYou_ != you) { lv_label_set_text(scoreLabels_[0], you); shownYou_ = you; }
        if (shownCpu_ != cpu) { lv_label_set_text(scoreLabels_[1], cpu); shownCpu_ = cpu; }
        std::string status;
        switch (pong_.phase()) {
        case Pong::Phase::Ready: status = "PRESS SPACE TO START. FIRST TO 7 WINS."; break;
        case Pong::Phase::Point: status = "POINT"; break;
        case Pong::Phase::Playing: status = ""; break;
        case Pong::Phase::Over:
            status = pong_.playerWon() ? "YOU WON. SPACE FOR A NEW MATCH." : "THE CPU WON. SPACE FOR A NEW MATCH.";
            break;
        }
        if (status != shownStatus_) { lv_label_set_text(status_, status.c_str()); shownStatus_ = status; }
    }

    // Records the match in the best scores, on the card, and says whether it was a new best.
    void matchEnded() {
        int points = pong_.playerScore();
        bool best = scores_.record(kRally, points);
        if (scoresOk_) {
            Paths p = shell_.paths();
            if (p.available()) makeDirs(p.folder(Folder::Games));
            if (best && !scores_.save()) shell_.notify("BEST SCORE NOT SAVED: THE CARD WRITE FAILED");
        }
        if (best) shell_.notify("NEW BEST: " + std::to_string(points) + " POINTS");
        refreshText();
    }

    View view_ = View::Hub;
    SeaBattle seaGame_{5};
    Klondike klon_{9};
    int klonCursor_ = 0;                // 0 stock, 1 waste, 2-5 foundations, 6-12 columns
    int klonSelLoc_ = -1;               // the location picked up, or -1
    int klonSelIdx_ = 0;                // for a column: the first card of the run picked up
    std::string klonMessage_;
    lv_obj_t* seaCells_[2][SeaBattle::kCells] = {};   // [0] your fleet, [1] the CPU's sea
    lv_obj_t* seaStatus_ = nullptr;
    lv_obj_t* seaShots_ = nullptr;
    int seaCursor_ = 0;
    std::string seaMessage_;
    Pong pong_{1};
    HighScores scores_;
    bool scoresOk_ = false;
    int moveDir_ = 0;
    uint32_t moveUntil_ = 0;
    uint32_t lastTick_ = 0, lastDraw_ = 0;
    lv_obj_t* court_ = nullptr;
    lv_obj_t* ball_ = nullptr;
    lv_obj_t* paddles_[2] = {nullptr, nullptr};
    lv_obj_t* scoreLabels_[2] = {nullptr, nullptr};
    lv_obj_t* status_ = nullptr;
    std::string shownYou_, shownCpu_, shownStatus_;
};

} // namespace

std::unique_ptr<App> makeGamesApp(Shell& shell) { return std::make_unique<GamesApp>(shell); }

} // namespace meshdeck
