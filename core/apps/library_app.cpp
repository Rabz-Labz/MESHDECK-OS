// library_app.cpp - LIBRARY: reference cards, and books (PDF and EPUB) with a reader.
//
// Screens:
//   CARDS     built-in UK reference cards (emergency contacts, supplies...), searchable
//   BOOKS     the shelf: every book in MESHDECK/books (made by tools/build_books.py)
//   READER    a PDF page drawn in the theme colour, or an EPUB section as text; previous / next,
//             contents, search inside the book, go to a page number. Your place is remembered.
// Books open where you left them (MESHDECK/books/positions.txt).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include "apps/library_core.h"
#include "apps/apps.h"
#include "formats/book_pack.h"
#include "kernel/storage.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

struct ShelfBook {
    std::string file, title, author, category;
    BookPack::Kind kind;
    uint32_t count;
};

} // namespace

class LibraryApp : public App {
public:
    LibraryApp(Shell& shell) : App(shell) {}
    ~LibraryApp() override {
        if (pageBuf_) lv_draw_buf_destroy(pageBuf_);
    }

    const char* name() const override { return "LIBRARY"; }
    const char* subtitle() const override { return "Cards, PDF and EPUB books"; }

    void onOpen() override {
        view_ = View::Cards;
        scanShelf();   // each time, so a book imported from FILES is on the shelf straight away
    }

    void onClose() override { savePositions(); }

    void build(Frame& frame) override {
        switch (view_) {
        case View::Cards: buildCards(frame); break;
        case View::Shelf: buildShelf(frame); break;
        case View::Reader: buildReader(frame); break;
        case View::Contents: buildContents(frame); break;
        case View::Results: buildResults(frame); break;
        }
    }

    bool onBack() override {
        switch (view_) {
        case View::Cards: return false;
        case View::Shelf: view_ = View::Cards; break;
        case View::Reader: savePositions(); view_ = View::Shelf; break;
        case View::Contents: case View::Results: view_ = View::Reader; break;
        }
        shell_.rebuild();
        return true;
    }

    // While reading: + / - turn the page or section, the same as the on-screen buttons.
    bool onKey(uint32_t key, uint8_t) override {
        if (view_ != View::Reader) return false;
        if (key == '+' || key == '=') { turn(1); return true; }
        if (key == '-' || key == '_') { turn(-1); return true; }
        return false;
    }

    const char* hint() const override {
        switch (view_) {
        case View::Reader:
            return book_.kind() == BookPack::Pages ? "+ / - TURN THE PAGE  |  UP/DOWN SCROLL  |  ESC BACK TO THE SHELF"
                                                   : "+ / - NEXT OR PREVIOUS SECTION  |  UP/DOWN SCROLL  |  ESC BACK";
        case View::Shelf: return "ARROWS + ENTER OPEN A BOOK (IT REOPENS WHERE YOU LEFT OFF)";
        default: return "TYPE TO SEARCH THE CARDS  |  ARROWS + ENTER";
        }
    }

private:
    enum class View { Cards, Shelf, Reader, Contents, Results };

    // ---- CARDS ------------------------------------------------------------------------------

    void buildCards(Frame& frame) {
        const Theme& t = shell_.th();
        frame.action("BOOKS", [this] { go(View::Shelf); });
        std::string q = lower(query_);
        std::vector<int> hits;
        for (int i = 0; i < kCoreCardCount; i++)
            if (q.empty() || lower(std::string(kCoreCards[i].title) + "\n" + kCoreCards[i].body).find(q) != std::string::npos)
                hits.push_back(i);
        if (selected_ < 0 && !hits.empty()) selected_ = hits.front();

        const int listW = 460;
        lv_obj_t* field = ui::textField(shell_, frame.body, listW, "SEARCH THE CARDS, THEN ENTER", 40);
        lv_obj_set_pos(field, 0, 0);
        lv_textarea_set_text(field, query_.c_str());
        ui::onEvent(field, LV_EVENT_VALUE_CHANGED, [this, field] {
            query_ = lv_textarea_get_text(field);
            selected_ = -1;
            shell_.rebuild();
        });
        shell_.preferFocus(field);

        std::vector<ui::Row> rows;
        if (hits.empty()) rows.push_back({query_.empty() ? "NO CARDS" : "NO MATCHES", "", nullptr});
        for (int index : hits) {
            rows.push_back({kCoreCards[index].title, "", [this, index] {
                                selected_ = index;
                                shell_.rebuild();
                            }});
        }
        ui::list(shell_, frame.body, 0, 70, listW, frame.h - 70, rows);

        lv_obj_t* view = ui::box(frame.body, listW + 2 * ui::kGap, 0, frame.w - listW - 2 * ui::kGap, frame.h);
        lv_obj_set_style_pad_all(view, 0, 0);
        lv_obj_add_flag(view, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));   // long cards scroll
        lv_obj_set_scroll_dir(view, LV_DIR_VER);
        shell_.focusable(view);
        if (selected_ < 0 || selected_ >= kCoreCardCount) {
            ui::label(view, "CHOOSE A CARD", fonts().body, t.muted);
            return;
        }
        const CoreCard& card = kCoreCards[selected_];
        std::string text = std::string(card.title) + "\n\nSCOPE: " + card.scope + "\nREVIEWED " + kLibraryReviewed +
                           "\n\n" + card.body + "\n\nSOURCES:\n" + card.sources;
        lv_obj_t* l = ui::label(view, text, fonts().small, t.ink);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, frame.w - listW - 4 * ui::kGap);
    }

    // ---- SHELF ------------------------------------------------------------------------------

    void scanShelf() {
        shelf_.clear();
        Paths p = shell_.paths();
        if (!p.available()) return;
        dir_ = p.folder(Folder::Books);
        for (const DirEntry& e : listDir(dir_, 2000)) {
            std::string n = lower(e.name);
            if (e.isDir || n.size() < 4 || n.substr(n.size() - 3) != ".bk") continue;
            BookPack pack;
            std::string err;
            if (!pack.open(dir_ + "/" + e.name, err, true)) continue;
            shelf_.push_back({e.name, pack.title(), pack.author(), pack.category(), pack.kind(), pack.count()});
        }
        std::sort(shelf_.begin(), shelf_.end(), [](const ShelfBook& a, const ShelfBook& b) {
            return a.category != b.category ? a.category < b.category : a.title < b.title;
        });
        loadPositions();
    }

    void buildShelf(Frame& frame) {
        const Theme& t = shell_.th();
        if (shelf_.empty()) {
            lv_obj_t* l = ui::label(frame.body,
                                    "NO BOOKS YET. COPY .BK FILES (MADE BY TOOLS/BUILD_BOOKS.PY) TO MESHDECK/BOOKS "
                                    "ON THE SD CARD, OR SEND THEM WITH TOOLS/UPLOAD.PY.",
                                    fonts().body, t.muted);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, frame.w);
            return;
        }
        std::vector<ui::Row> rows;
        std::string category;
        for (size_t i = 0; i < shelf_.size(); i++) {
            const ShelfBook& b = shelf_[i];
            if (b.category != category) {
                category = b.category;
                rows.push_back({"== " + category + " ==", "", nullptr});
            }
            char info[48];
            snprintf(info, sizeof info, "%u %s%s", (unsigned)b.count, b.kind == BookPack::Pages ? "PAGES" : "SECTIONS",
                     positions_.count(b.file) ? "  READING" : "");
            size_t index = i;
            rows.push_back({b.title, info, [this, index] { openBook(index); }});
        }
        ui::list(shell_, frame.body, 0, 0, frame.w, frame.h, rows);
    }

    void openBook(size_t i) {
        std::string err;
        if (!book_.open(dir_ + "/" + shelf_[i].file, err)) {
            shell_.notify("BOOK: " + err);
            return;
        }
        bookFile_ = shelf_[i].file;
        auto it = positions_.find(bookFile_);
        page_ = it != positions_.end() && it->second < book_.count() ? it->second : 0;
        loaded_ = UINT32_MAX;
        go(View::Reader);
    }

    // ---- READER -----------------------------------------------------------------------------

    void turn(int d) {
        if (!book_.isOpen()) return;
        int64_t p = (int64_t)page_ + d;
        if (p < 0 || p >= (int64_t)book_.count()) {
            shell_.notify(d > 0 ? "LAST PAGE" : "FIRST PAGE");
            return;
        }
        goTo((uint32_t)p);
    }

    void goTo(uint32_t p) {
        if (p >= book_.count()) return;
        page_ = p;
        positions_[bookFile_] = page_;
        savePositions();
        view_ = View::Reader;
        shell_.rebuild();
    }

    void buildReader(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap;
        const char* unit = book_.kind() == BookPack::Pages ? "PAGE" : "SECTION";
        char pos[64];
        snprintf(pos, sizeof pos, "%s %u OF %u", unit, (unsigned)page_ + 1, (unsigned)book_.count());
        std::string title = book_.title().size() > 40 ? book_.title().substr(0, 38) + ".." : book_.title();
        lv_obj_t* head = ui::label(frame.body, title + "   " + pos, fonts().small, t.muted);
        lv_obj_set_pos(head, 0, 0);

        frame.action("<", [this] { turn(-1); });
        frame.action(">", [this] { turn(1); });
        if (!book_.toc().empty()) frame.action("CONTENTS", [this] { go(View::Contents); });
        frame.action("FIND", [this] { focusFind(); });
        frame.action("SHELF", [this] { savePositions(); go(View::Shelf); });

        // The search and page-number boxes sit under the title, so the page has the whole screen.
        const int boxY = 40, boxH = ui::kTouchMin;
        lv_obj_t* find = ui::textField(shell_, frame.body, 360, "SEARCH THIS BOOK", 40);
        lv_obj_set_pos(find, 0, boxY);
        lv_obj_set_height(find, boxH);
        ui::onEvent(find, LV_EVENT_READY, [this, find] { doFind(lv_textarea_get_text(find)); });
        findField_ = find;
        lv_obj_t* gotoBox = ui::textField(shell_, frame.body, 160, "PAGE", 6);
        lv_obj_set_pos(gotoBox, 376, boxY);
        lv_obj_set_height(gotoBox, boxH);
        lv_textarea_set_accepted_chars(gotoBox, "0123456789");
        ui::onEvent(gotoBox, LV_EVENT_READY, [this, gotoBox] {
            long p = atol(lv_textarea_get_text(gotoBox));
            if (p < 1 || p > (long)book_.count()) shell_.notify("NO SUCH PAGE");
            else goTo((uint32_t)(p - 1));
        });

        int top = boxY + boxH + g, viewH = frame.h - top;
        lv_obj_t* view = ui::box(frame.body, 0, top, frame.w, viewH);
        lv_obj_add_flag(view, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_scroll_dir(view, LV_DIR_VER);
        lv_obj_set_style_pad_right(view, 12, 0);
        shell_.focusable(view);
        shell_.preferFocus(view);

        if (book_.kind() == BookPack::Pages) {
            std::string err;
            if (loaded_ != page_) {
                std::vector<uint8_t> px;
                uint32_t pw = 0, ph = 0;
                if (!book_.pageImage(page_, px, pw, ph, err)) {
                    ui::label(view, err, fonts().small, t.warn);
                    return;
                }
                loadPage(px, pw, ph);
                loaded_ = page_;
            }
            if (pageBuf_) {
                lv_obj_t* canvas = lv_canvas_create(view);
                lv_canvas_set_draw_buf(canvas, pageBuf_);
                lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
            }
        } else {
            std::string text;
            book_.text(page_, text);
            lv_obj_t* l = ui::label(view, text, fonts().body, t.ink);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, frame.w - 24);
        }
    }

    // Brightness values (0 background, 255 ink) to the theme colours, once per page: the same
    // palette trick as the map, so the page is drawn without per-pixel work at draw time.
    void loadPage(const std::vector<uint8_t>& px, uint32_t pw, uint32_t ph) {
        if (pageBuf_) lv_draw_buf_destroy(pageBuf_);
        pageBuf_ = lv_draw_buf_create(pw, ph, LV_COLOR_FORMAT_RGB565, 0);
        if (!pageBuf_) return;
        uint16_t lut[256];
        for (int v = 0; v < 256; v++) lut[v] = lv_color_to_u16(lv_color_mix(shell_.th().ink, shell_.th().bg, (uint8_t)v));
        for (uint32_t y = 0; y < ph; y++) {
            uint16_t* row = reinterpret_cast<uint16_t*>(pageBuf_->data + y * pageBuf_->header.stride);
            const uint8_t* src = px.data() + (size_t)y * pw;
            for (uint32_t x = 0; x < pw; x++) row[x] = lut[src[x]];
        }
    }

    void focusFind() {
        if (findField_) {
            lv_group_focus_obj(findField_);
            shell_.preferFocus(findField_);
        }
    }

    void doFind(const char* text) {
        std::string q = text;
        if (q.find_first_not_of(' ') == std::string::npos) return;
        lastFind_ = q;
        hits_ = book_.search(q);
        view_ = View::Results;
        shell_.rebuild();
    }

    // ---- CONTENTS and SEARCH RESULTS --------------------------------------------------------

    void buildContents(Frame& frame) {
        const Theme& t = shell_.th();
        std::vector<ui::Row> rows;
        const char* unit = book_.kind() == BookPack::Pages ? "P" : "S";
        for (const BookPack::TocEntry& e : book_.toc()) {
            std::string text = std::string(e.level > 1 ? "    " : "") + e.title;
            std::string detail = unit + std::to_string(e.index + 1);
            uint32_t index = e.index;
            rows.push_back({text, detail, [this, index] { goTo(index); }});
        }
        lv_obj_t* head = ui::label(frame.body, "CONTENTS: " + book_.title(), fonts().small, t.ink);
        lv_obj_set_pos(head, 0, 0);
        ui::list(shell_, frame.body, 0, 36, frame.w, frame.h - 36, rows);
    }

    void buildResults(Frame& frame) {
        const Theme& t = shell_.th();
        std::string head = "SEARCH \"" + lastFind_ + "\": " + std::to_string(hits_.size()) +
                           (hits_.size() >= 60 ? "+ MATCHES (FIRST 60)" : " MATCHES");
        lv_obj_t* h = ui::label(frame.body, head, fonts().small, t.ink);
        lv_obj_set_pos(h, 0, 0);
        std::vector<ui::Row> rows;
        const char* unit = book_.kind() == BookPack::Pages ? "P" : "S";
        if (hits_.empty()) rows.push_back({"NO MATCHES", "", nullptr});
        for (const BookPack::Hit& hit : hits_) {
            uint32_t index = hit.index;
            rows.push_back({unit + std::to_string(hit.index + 1) + "  " + hit.snippet, "", [this, index] { goTo(index); }});
        }
        ui::list(shell_, frame.body, 0, 36, frame.w, frame.h - 36, rows);
    }

    // ---- positions and navigation -----------------------------------------------------------

    // Where each book was left: MESHDECK/books/positions.txt, "file page" per line.
    void loadPositions() {
        positions_.clear();
        std::string data;
        if (dir_.empty() || !readFile(dir_ + "/positions.txt", data, 64 * 1024)) return;
        size_t pos = 0;
        while (pos < data.size()) {
            size_t end = data.find('\n', pos);
            if (end == std::string::npos) end = data.size();
            std::string line = data.substr(pos, end - pos);
            pos = end + 1;
            size_t sp = line.rfind(' ');
            if (sp != std::string::npos) positions_[line.substr(0, sp)] = (uint32_t)atol(line.c_str() + sp + 1);
        }
    }

    void savePositions() {
        if (dir_.empty() || positions_.empty()) return;
        std::string out;
        for (auto& kv : positions_) out += kv.first + " " + std::to_string(kv.second) + "\n";
        writeFileAtomic(dir_ + "/positions.txt", out);
    }

    void go(View v) {
        view_ = v;
        shell_.rebuild();
    }

    View view_ = View::Cards;
    std::string query_;
    int selected_ = -1;
    std::vector<ShelfBook> shelf_;
    std::string dir_, bookFile_, lastFind_;
    std::map<std::string, uint32_t> positions_;
    BookPack book_;
    uint32_t page_ = 0, loaded_ = UINT32_MAX;
    lv_draw_buf_t* pageBuf_ = nullptr;
    std::vector<BookPack::Hit> hits_;
    lv_obj_t* findField_ = nullptr;
};

std::unique_ptr<App> makeLibraryApp(Shell& shell) { return std::make_unique<LibraryApp>(shell); }

} // namespace meshdeck
