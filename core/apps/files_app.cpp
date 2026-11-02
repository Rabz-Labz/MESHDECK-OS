// files_app.cpp - FILES: browse MESHDECK/Files on the SD card, preview text, view images, rename,
// copy, delete.
//
// Screens:
//   list      folders first, then files; Inbox and Screenshots sit at the top
//   details   the selected file: its name (RENAME), size, COPY, DELETE, a text preview, and for
//             PNG, JPEG and BMP files a VIEW IMAGE button
//   viewer    the picture scaled to fit the screen (ESC closes it)
// Delete asks twice: press DELETE, then CONFIRM DELETE within 5 seconds.
#include <cctype>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "apps/apps.h"
#include "formats/epub_import.h"
#include "formats/image_decode.h"
#include "kernel/storage.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {
namespace {

std::string sizeText(uint64_t b) {
    char buf[24];
    if (b < 1024) snprintf(buf, sizeof buf, "%u B", (unsigned)b);
    else if (b < 1024 * 1024) snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
    else snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
    return buf;
}

// Text if every byte is printable ASCII, whitespace, or part of a UTF-8 sequence.
bool looksLikeText(const std::string& data) {
    for (unsigned char c : data)
        if (c < 32 && c != '\t' && c != '\r' && c != '\n') return false;
    return true;
}

std::string lowerExt(const std::string& name) {
    size_t dot = name.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    return ext;
}

bool isImageName(const std::string& name) {
    std::string ext = lowerExt(name);
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp";
}

bool isEpubName(const std::string& name) { return lowerExt(name) == "epub"; }

// "photo.png" -> "photo-copy.png", then "photo-copy-2.png" and so on.
std::string copyName(const std::string& dir, const std::string& name) {
    size_t dot = name.find_last_of('.');
    std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    std::string ext = dot == std::string::npos ? "" : name.substr(dot);
    std::string candidate = stem + "-copy" + ext;
    for (int n = 2; fileExists(dir + "/" + candidate) && n < 1000; n++)
        candidate = stem + "-copy-" + std::to_string(n) + ext;
    return candidate;
}

// Copies a file in chunks, then syncs it to the card. False if anything fails.
bool copyFile(const std::string& from, const std::string& to) {
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    fclose(in);
    ok = ok && fflush(out) == 0 && syncFile(out);
    fclose(out);
    if (!ok) remove(to.c_str());   // never leave a half copy behind
    return ok;
}

} // namespace

class FilesApp : public App {
public:
    using App::App;
    ~FilesApp() override { closeView(); }

    const char* name() const override { return "FILES"; }
    const char* subtitle() const override { return "Screenshots, inbox, your files"; }

    void onOpen() override {
        closeView();
        rel_.clear();
        selected_ = -1;
        armed_ = false;
        refresh();
    }

    void build(Frame& frame) override {
        const Theme& t = shell_.th();
        Paths p = shell_.paths();
        if (!p.available()) {
            ui::label(frame.body, "NO STORAGE: INSERT A MICROSD CARD TO BROWSE YOUR FILES.", fonts().body, t.warn);
            return;
        }
        if (viewing_) { buildViewer(frame); return; }

        frame.action("UP", [this] { up(); });
        frame.action(armed_ ? "CONFIRM DELETE" : "DELETE", [this] { deletePressed(); });

        const int listW = 480, g = ui::kGap;
        std::string where = "MESHDECK/Files" + rel_;
        lv_obj_t* head = ui::label(frame.body, where, fonts().small, t.muted);
        lv_obj_set_pos(head, 0, 0);

        std::vector<ui::Row> rows;
        if (entries_.empty()) rows.push_back({"EMPTY FOLDER", "", nullptr});
        for (size_t i = 0; i < entries_.size(); i++) {
            const DirEntry& e = entries_[i];
            size_t index = i;
            if (e.isDir) {
                std::string dir = e.name;
                rows.push_back({"[" + e.name + "]", "", [this, dir] {
                                    rel_ += "/" + dir;
                                    refresh();
                                    shell_.rebuild();
                                }});
            } else {
                rows.push_back({e.name, sizeText(e.size), [this, index] {
                                    selected_ = (int)index;
                                    armed_ = false;
                                    shell_.rebuild();
                                }});
            }
        }
        ui::list(shell_, frame.body, 0, 40, listW, frame.h - 40, rows);

        // The details of the selected file, on the right.
        int px = listW + 2 * g, pw = frame.w - px;
        lv_obj_t* panel = ui::panel(shell_, frame.body, px, 40, pw, frame.h - 40);
        if (selected_ < 0 || selected_ >= (int)entries_.size()) {
            ui::label(panel, "SELECT A FILE", fonts().body, t.muted);
            return;
        }
        const DirEntry& e = entries_[selected_];
        bool image = isImageName(e.name);

        lv_obj_t* name = ui::label(panel, e.name, fonts().body, t.ink);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, pw - 4 * g);
        lv_obj_set_pos(name, 0, 0);
        lv_obj_t* size = ui::label(panel, sizeText(e.size), fonts().small, t.muted);
        lv_obj_set_pos(size, 0, 40);

        if (preview_.empty()) loadPreview(e);

        // Rename: the name box, then RENAME.
        nameField_ = ui::textField(shell_, panel, pw - 240 - 2 * g, "NEW NAME", 60);
        lv_obj_set_pos(nameField_, 0, 80);
        lv_textarea_set_text(nameField_, nameDraft_.c_str());
        ui::onEvent(nameField_, LV_EVENT_VALUE_CHANGED, [this] { nameDraft_ = lv_textarea_get_text(nameField_); });
        ui::onEvent(nameField_, LV_EVENT_READY, [this] { rename(); });
        lv_obj_t* ren = ui::button(shell_, panel, "RENAME", [this] { rename(); }, 240, ui::kTouchMin + 8);
        lv_obj_set_pos(ren, pw - 240 - 2 * g, 80);

        int rowY = 80 + ui::kTouchMin + 8 + g;
        lv_obj_t* copy = ui::button(shell_, panel, "COPY", [this] { copySelected(); }, 240, ui::kTouchMin + 8);
        lv_obj_set_pos(copy, 0, rowY);
        if (image) {
            lv_obj_t* view = ui::button(shell_, panel, "VIEW IMAGE", [this] { openViewer(); }, 240, ui::kTouchMin + 8);
            lv_obj_set_pos(view, 240 + g, rowY);
        }
        // An EPUB can be made into a book for LIBRARY, in the same place as the picture button.
        bool epub = isEpubName(e.name);
        if (epub) {
            lv_obj_t* import = ui::button(shell_, panel, "IMPORT AS BOOK", [this] { importBook(); }, 320, ui::kTouchMin + 8);
            lv_obj_set_pos(import, 240 + g, rowY);
        }

        // A text preview of the first 6 KB, or a note for files that aren't text.
        std::string text;
        if (image) text = "PICTURE FILE. VIEW IMAGE SHOWS IT ON THE SCREEN.";
        else if (epub) text = "EPUB BOOK. IMPORT AS BOOK MAKES IT A BOOK IN LIBRARY (THE IMPORT CAN TAKE A FEW SECONDS).";
        else text = looksLikeText(preview_) ? preview_ : "BINARY FILE: NO PREVIEW";
        if (!image && e.size > preview_.size()) text += "\n\n[PREVIEW: FIRST 6 KB]";
        lv_obj_t* body = ui::label(panel, text, fonts().small, t.ink);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(body, pw - 4 * g);
        lv_obj_set_pos(body, 0, rowY + ui::kTouchMin + 8 + 2 * g);
    }

    bool onBack() override {
        if (viewing_) {
            closeView();
            shell_.rebuild();
            return true;
        }
        if (rel_.empty()) return false;
        up();
        return true;
    }

    const char* hint() const override {
        if (viewing_) return "ESC CLOSES THE PICTURE";
        return "ENTER OPENS A FOLDER OR SELECTS A FILE  |  ESC UP  |  DELETE TWICE";
    }

    // Disarms a delete that was not confirmed in time; decodes a picture once its LOADING screen is up.
    void tick() override {
        if (viewLoading_) {
            if (viewLoadFrames_ < 1) viewLoadFrames_++;
            else decodeViewer();
        }
        if (armed_ && shell_.platform().uptimeMs() - armedAt_ > 5000) {
            armed_ = false;
            shell_.rebuild();
        }
    }

private:
    std::string root() const { return shell_.paths().folder(Folder::Files); }

    void refresh() {
        Paths p = shell_.paths();
        if (p.available()) makeDirs(root());
        entries_ = p.available() ? listDir(root() + rel_) : std::vector<DirEntry>();
        // Folders first (listDir), then Inbox and Screenshots at the very top of the top level.
        if (rel_.empty()) {
            for (const char* first : {"Inbox", "Screenshots"})
                for (size_t i = 0; i < entries_.size(); i++)
                    if (entries_[i].isDir && entries_[i].name == first) {
                        DirEntry e = entries_[i];
                        entries_.erase(entries_.begin() + i);
                        entries_.insert(entries_.begin(), e);
                        break;
                    }
        }
        selected_ = -1;
        armed_ = false;
        preview_.clear();
        nameDraft_.clear();
    }

    void up() {
        if (rel_.empty()) return;
        rel_.erase(rel_.find_last_of('/'));
        refresh();
        shell_.rebuild();
    }

    void loadPreview(const DirEntry& e) {
        std::string data;
        if (!isImageName(e.name) && readFile(root() + rel_ + "/" + e.name, data, 6000)) preview_ = data;
        else preview_ = " ";   // marks "loaded" for pictures and files that can't be read
        nameDraft_ = e.name;
    }

    void rename() {
        if (selected_ < 0 || selected_ >= (int)entries_.size()) return;
        std::string to = nameDraft_;
        size_t a = to.find_first_not_of(' '), b = to.find_last_not_of(' ');
        to = a == std::string::npos ? "" : to.substr(a, b - a + 1);
        const std::string from = entries_[selected_].name;
        if (to.empty() || to == from) { shell_.notify("NOTHING TO RENAME"); return; }
        if (to.find('/') != std::string::npos || to.find('\\') != std::string::npos || to[0] == '.') {
            shell_.notify("NAMES CAN'T CONTAIN / OR START WITH A DOT");
            return;
        }
        if (fileExists(root() + rel_ + "/" + to)) { shell_.notify("A FILE WITH THAT NAME ALREADY EXISTS"); return; }
        if (::rename((root() + rel_ + "/" + from).c_str(), (root() + rel_ + "/" + to).c_str()) != 0) {
            shell_.notify("COULD NOT RENAME THE FILE");
            return;
        }
        shell_.notify("RENAMED TO " + to);
        refresh();
        shell_.rebuild();
    }

    // Makes the selected EPUB into a book in MESHDECK/books, where LIBRARY finds it. The whole book is
    // converted before anything is written, so a long book takes a few seconds and the screen waits.
    void importBook() {
        if (selected_ < 0 || selected_ >= (int)entries_.size()) return;
        const std::string name = entries_[selected_].name;
        Paths p = shell_.paths();
        if (!p.available()) {
            shell_.notify("NO STORAGE: NO BOOK IMPORTED");
            return;
        }
        // Keep the file's name for the book, adding -2, -3... rather than replacing a book already there.
        makeDirs(p.folder(Folder::Books));
        std::string stem = name.substr(0, name.find_last_of('.'));
        std::string target = p.folder(Folder::Books) + "/" + stem + ".bk";
        for (int n = 2; fileExists(target) && n < 1000; n++)
            target = p.folder(Folder::Books) + "/" + stem + "-" + std::to_string(n) + ".bk";

        EpubResult result;
        std::string err;
        if (!importEpub(root() + rel_ + "/" + name, target, result, err)) {
            shell_.notify("NOT IMPORTED: " + err);
            return;
        }
        shell_.notify("IMPORTED " + result.title + ", " + std::to_string(result.sections) + " SECTIONS" +
                      (result.truncated ? " (CUT SHORT AT 4 MB)" : "") + ". SEE LIBRARY");
    }

    void copySelected() {
        if (selected_ < 0 || selected_ >= (int)entries_.size()) return;
        const DirEntry& e = entries_[selected_];
        std::string to = copyName(root() + rel_, e.name);
        if (copyFile(root() + rel_ + "/" + e.name, root() + rel_ + "/" + to)) {
            shell_.notify("COPIED TO " + to);
            refresh();
            shell_.rebuild();
        } else {
            shell_.notify("COPY FAILED (STORAGE FULL OR MISSING?)");
        }
    }

    void deletePressed() {
        if (selected_ < 0 || selected_ >= (int)entries_.size() || entries_[selected_].isDir) {
            shell_.notify("SELECT A FILE TO DELETE (FOLDERS CAN'T BE DELETED HERE)");
            return;
        }
        if (!armed_) {
            armed_ = true;
            armedAt_ = shell_.platform().uptimeMs();
            shell_.notify("PRESS CONFIRM DELETE WITHIN 5 SECONDS. THIS CAN'T BE UNDONE.");
            shell_.rebuild();
            return;
        }
        armed_ = false;
        const std::string name = entries_[selected_].name;
        bool ok = remove((root() + rel_ + "/" + name).c_str()) == 0;
        shell_.notify(ok ? "DELETED " + name : "COULD NOT DELETE THE FILE");
        refresh();
        shell_.rebuild();
    }

    // ---- viewer -----------------------------------------------------------------------------

    static constexpr uint64_t kMaxViewFileBytes = 16 * 1024 * 1024;

    // Opening a picture first draws a LOADING screen, then decodes on the next frame, so the
    // screen changes at once instead of after the decode.
    void openViewer() {
        if (selected_ < 0 || selected_ >= (int)entries_.size()) return;
        const DirEntry& e = entries_[selected_];
        if (e.size > kMaxViewFileBytes) {
            shell_.notify("THAT FILE IS TOO BIG TO VIEW (16 MB MAX)");
            return;
        }
        closeView();
        viewing_ = true;
        viewLoading_ = true;
        viewLoadFrames_ = 0;
        viewPath_ = root() + rel_ + "/" + e.name;
        viewName_ = e.name;
        viewInfo_ = "LOADING...";
        shell_.rebuild();
    }

    // Reads and decodes the picture, and scales it to fit the screen. On failure the viewer
    // closes and the reason is shown on the hint line.
    void decodeViewer() {
        viewLoading_ = false;
        std::string data;
        Image img;
        std::string error;
        bool ok = readFile(viewPath_, data, (size_t)kMaxViewFileBytes);
        if (!ok) error = "COULD NOT READ THAT FILE";
        else if (!imageKind((const uint8_t*)data.data(), data.size())[0]) {
            // Say what is really in the file, so a damaged or empty picture can be told apart.
            char head[80] = "";
            for (size_t i = 0; i < data.size() && i < 8; i++)
                snprintf(head + strlen(head), sizeof head - strlen(head), "%02X ", (unsigned char)data[i]);
            error = data.empty() ? "THE FILE IS EMPTY (0 BYTES): IT WAS NOT FULLY SAVED"
                                 : std::string("NOT A PNG, JPEG OR BMP. FILE STARTS: ") + head;
        } else if (!decodeImage((const uint8_t*)data.data(), data.size(), img, error)) {
            // error already set by the decoder
        }
        if (!error.empty()) {
            closeView();
            shell_.notify(error);
            shell_.rebuild();
            return;
        }
        // Scaled down (nearest pixel) to fit the body, keeping its shape.
        uint32_t dw = img.width, dh = img.height;
        if (dw > (uint32_t)viewMaxW_ || dh > (uint32_t)viewMaxH_) {
            double s = std::min((double)viewMaxW_ / dw, (double)viewMaxH_ / dh);
            dw = std::max<uint32_t>(1, (uint32_t)(dw * s));
            dh = std::max<uint32_t>(1, (uint32_t)(dh * s));
        }
        viewBuf_ = lv_draw_buf_create(dw, dh, LV_COLOR_FORMAT_RGB565, 0);
        if (!viewBuf_) {
            closeView();
            shell_.notify("NOT ENOUGH MEMORY FOR THAT PICTURE");
            shell_.rebuild();
            return;
        }
        for (uint32_t y = 0; y < dh; y++) {
            uint16_t* row = reinterpret_cast<uint16_t*>(viewBuf_->data + (size_t)y * viewBuf_->header.stride);
            uint32_t sy = (uint32_t)((uint64_t)y * img.height / dh);
            for (uint32_t x = 0; x < dw; x++) {
                uint32_t sx = (uint32_t)((uint64_t)x * img.width / dw);
                const uint8_t* p = &img.rgb[((size_t)sy * img.width + sx) * 3];
                row[x] = lv_color_to_u16(lv_color_make(p[0], p[1], p[2]));
            }
        }
        viewInfo_ = std::to_string(img.width) + " x " + std::to_string(img.height) + " PIXELS";
        shell_.rebuild();
    }
    void closeView() {
        if (viewBuf_) lv_draw_buf_destroy(viewBuf_);
        viewBuf_ = nullptr;
        viewing_ = false;
    }

    void buildViewer(Frame& frame) {
        const Theme& t = shell_.th();
        viewMaxW_ = frame.w;
        viewMaxH_ = frame.h - 40;
        lv_obj_t* head = ui::label(frame.body, viewName_ + "   " + viewInfo_, fonts().small, t.muted);
        lv_obj_set_pos(head, 0, 0);
        if (viewLoading_) {
            lv_obj_t* wait = ui::label(frame.body, "LOADING PICTURE...", fonts().body, t.muted);
            lv_obj_align(wait, LV_ALIGN_CENTER, 0, 0);
            return;
        }
        if (viewBuf_) {
            lv_obj_t* img = lv_image_create(frame.body);
            lv_image_set_src(img, viewBuf_);
            lv_obj_align(img, LV_ALIGN_CENTER, 0, 20);
        }
    }

    std::string rel_;
    std::vector<DirEntry> entries_;
    int selected_ = -1;
    std::string preview_, nameDraft_;
    bool armed_ = false;
    uint32_t armedAt_ = 0;
    lv_obj_t* nameField_ = nullptr;
    bool viewing_ = false, viewLoading_ = false;
    int viewLoadFrames_ = 0;
    std::string viewPath_;
    lv_draw_buf_t* viewBuf_ = nullptr;
    std::string viewName_, viewInfo_;
    int viewMaxW_ = 1200, viewMaxH_ = 600;
};

std::unique_ptr<App> makeFilesApp(Shell& shell) { return std::make_unique<FilesApp>(shell); }

} // namespace meshdeck
