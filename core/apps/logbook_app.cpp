// logbook_app.cpp - LOGBOOK: one hub for the field log, reports, inventory and notes.
//
// LOG ENTRIES is the append-only field log (MESHDECK/log/field.jnl, the journal format shared with
// the rest of MESHDECK): entries are added and never edited, so the record stays honest. REPORTS
// fills in and sends the standard forms, INVENTORY keeps the kit lists, and NOTES are plain files.
#include <stdio.h>
#include <ctype.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "apps/apps.h"
#include "apps/placeholder_app.h"
#include "formats/osgrid.h"
#include "formats/report.h"
#include "kernel/clock.h"
#include "kernel/journal.h"
#include "kernel/storage.h"
#include "services/inventory.h"
#include "services/mesh.h"
#include "services/position.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {
namespace {

const size_t kShownEntries = 200;     // newest entries listed; the journal keeps them all
const size_t kMaxEntryBytes = 4000;   // the journal's own limit per record
const size_t kMaxReportBox = 160;     // one box of a report form
const size_t kMaxInvText = 60;        // one item name, location name or date typed in INVENTORY
const int kExpiringDays = 30;         // "expiring" means within this many days
const size_t kShownNotes = 50;        // newest notes listed

// First line of a text, cut to a width, for list rows.
std::string firstLine(const std::string& text, size_t max) {
    std::string line = text.substr(0, text.find('\n'));
    if (line.size() > max) line = line.substr(0, max - 2) + "..";
    return line;
}

} // namespace

class LogbookApp : public App {
public:
    LogbookApp(Shell& shell, Services& services) : App(shell), position_(services.position) {}

    const char* name() const override { return "LOGBOOK"; }
    const char* subtitle() const override { return "Log, reports, inventory, notes"; }

    void onOpen() override {
        part_ = -1;
        form_ = -1;
        openLog();
    }

    void onClose() override { draft_.clear(); }

    void build(Frame& frame) override {
        if (part_ == 0) { buildLog(frame); return; }
        if (part_ == 1) { buildReports(frame); return; }
        if (part_ == 2) { buildInventory(frame); return; }
        if (part_ == 3) { buildNotes(frame); return; }
        if (part_ > 3) {
            PlaceholderApp::draw(shell_, frame.body, frame.w, frame.h, kParts[part_]);
            return;
        }
        std::vector<ui::HubItem> items;
        for (int i = 0; i < kPartCount; i++)
            items.push_back({kParts[i].name, kParts[i].subtitle, [this, i] { open(i); }});
        ui::hub(shell_, frame.body, frame.w, frame.h, items, 2);
        // Focus the first part rather than BACK, so ENTER opens it.
        if (lv_obj_get_child_count(frame.body) > 0) shell_.preferFocus(lv_obj_get_child(frame.body, 0));
    }

    // ESC from an open report form returns to the list of forms; from a part, to the hub; from the hub, to home.
    bool onBack() override {
        if (part_ == 1 && form_ >= 0) {
            form_ = -1;
            shell_.rebuild();
            return true;
        }
        if (part_ == 2 && !loc_.empty()) {
            leaveLocation();
            return true;
        }
        if (part_ == 3 && !selNote_.empty()) {
            closeNote();
            return true;
        }
        if (part_ < 0) return false;
        part_ = -1;
        shell_.rebuild();
        return true;
    }

    // 1-4 pick a part while on the hub (the shell's tile keys apply everywhere else).
    bool onKey(uint32_t key, uint8_t) override {
        if (part_ >= 0 || key < '1' || key >= '1' + kPartCount) return false;
        open((int)(key - '1'));
        return true;
    }

    const char* hint() const override {
        if (part_ == 0) return "TYPE AN ENTRY, THEN ADD  |  ENTRIES ARE NEVER EDITED";
        if (part_ == 1) {
            return form_ < 0 ? "PICK A REPORT"
                             : "TAP A BOX AND TYPE  |  SAVE KEEPS IT ON THE CARD";
        }
        if (part_ == 2) {
            return loc_.empty() ? "PICK A LOCATION, OR TYPE A NEW ONE"
                                : "TAP AN ITEM TO CHANGE IT  |  ESC BACK TO THE LOCATIONS";
        }
        if (part_ == 3) {
            return selNote_.empty() ? "TYPE A NOTE, THEN SAVE  |  TAP A NOTE TO READ IT"
                                    : "READ-ONLY: NEW NOTE TO WRITE ANOTHER";
        }
        return part_ < 0 ? "1-4 OPEN A PART  |  ARROWS + ENTER" : nullptr;
    }

private:
    static const int kPartCount = 4;
    static const PlannedApp kParts[kPartCount];

    void open(int part) {
        part_ = part;
        shell_.rebuild();
    }

    // Opens the journal once; a card that's missing or read-only shows as "LOG OFF".
    void openLog() {
        Paths p = shell_.paths();
        if (!p.available()) {
            error_ = "NO STORAGE";
            return;
        }
        if (!journal_.isOpen() || journal_.path() != p.file(Folder::Log, "field.jnl")) {
            makeDirs(p.folder(Folder::Log));
            if (!journal_.open(p.file(Folder::Log, "field.jnl"))) {
                error_ = "CAN'T OPEN THE LOG FILE";
                return;
            }
        }
        error_.clear();
        reload();
    }

    void reload() {
        std::deque<Journal::Record> last;
        journal_.forEach([&](const Journal::Record& r) {
            last.push_back(r);
            if (last.size() > kShownEntries) last.pop_front();
        });
        entries_.assign(last.begin(), last.end());
        if (selected_ < 0 || selected_ >= (int)entries_.size()) selected_ = (int)entries_.size() - 1;   // newest
    }

    void add() {
        size_t a = draft_.find_first_not_of(" \r\n\t"), b = draft_.find_last_not_of(" \r\n\t");
        if (a == std::string::npos) {
            shell_.notify("TYPE AN ENTRY FIRST");
            return;
        }
        if (!journal_.isOpen()) {
            shell_.notify("LOG OFF: " + error_);
            return;
        }
        std::string text = draft_.substr(a, b - a + 1);
        if (text.size() > kMaxEntryBytes) text.resize(kMaxEntryBytes);
        if (!journal_.append(Journal::Note, text, shell_.nowUtc(), shell_.platform().uptimeMs())) {
            shell_.notify("LOG WRITE FAILED. THE ENTRY IS STILL IN THE BOX.");
            return;
        }
        shell_.notify("LOG ENTRY #" + std::to_string(journal_.records()) + " SAVED");
        draft_.clear();
        reload();
        selected_ = (int)entries_.size() - 1;
        shell_.rebuild();
    }

    void buildLog(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap, listW = 520;

        // The entry box (several lines), then ADD.
        lv_obj_t* box = lv_textarea_create(frame.body);
        lv_obj_set_pos(box, 0, 0);
        lv_obj_set_size(box, listW, 150);
        lv_obj_set_style_text_font(box, fonts().small, 0);
        lv_obj_set_style_text_color(box, t.ink, 0);
        lv_obj_set_style_bg_color(box, t.surface, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(box, t.line, 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_border_color(box, t.ink, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(box, 4, LV_STATE_FOCUSED);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_set_style_pad_all(box, 12, 0);
        lv_textarea_set_max_length(box, (uint32_t)kMaxEntryBytes);
        lv_textarea_set_placeholder_text(box, "NEW ENTRY: WHAT, WHERE, WHO TOLD YOU");
        lv_textarea_set_text(box, draft_.c_str());
        ui::onEvent(box, LV_EVENT_VALUE_CHANGED, [this, box] { draft_ = lv_textarea_get_text(box); });
        shell_.focusable(box);
        shell_.preferFocus(box);

        lv_obj_t* addBtn = ui::button(shell_, frame.body, "ADD ENTRY", [this] { add(); }, 240, ui::kTouchMin + 8);
        lv_obj_set_pos(addBtn, 0, 150 + g);

        std::string status = error_.empty() ? std::to_string(journal_.records()) + " ENTRIES" : "LOG OFF: " + error_;
        lv_obj_t* info = ui::label(frame.body, status, fonts().small, error_.empty() ? t.muted : t.warn);
        lv_obj_set_pos(info, 260, 150 + g + 12);

        // The entries, newest first.
        std::vector<ui::Row> rows;
        for (int i = (int)entries_.size() - 1; i >= 0; i--) {
            const Journal::Record& r = entries_[i];
            char when[24] = "NO CLOCK";
            if (r.utc) {
                CivilTime c = civilFromUtc(localFromUtc(r.utc));
                snprintf(when, sizeof when, "%02d/%02d %02d:%02d", c.day, c.month, c.hour, c.minute);
            }
            rows.push_back({std::string(when) + "  " + firstLine(r.text, 40), "", [this, i] {
                                selected_ = i;
                                shell_.rebuild();
                            }});
        }
        if (rows.empty()) rows.push_back({"NO ENTRIES YET", "", nullptr});
        const int listY = 150 + g + ui::kTouchMin + 8 + g;   // below the ADD button
        ui::list(shell_, frame.body, 0, listY, listW, frame.h - listY, rows);

        // The chosen entry, on the right.
        int px = listW + 2 * g, pw = frame.w - px;
        lv_obj_t* panel = ui::panel(shell_, frame.body, px, 0, pw, frame.h);
        if (selected_ < 0 || selected_ >= (int)entries_.size()) {
            ui::label(panel, "SELECT AN ENTRY", fonts().body, t.muted);
            return;
        }
        const Journal::Record& r = entries_[selected_];
        char head[96], when[40];
        snprintf(head, sizeof head, "ENTRY #%u", (unsigned)r.seq);
        ui::label(panel, head, fonts().body, t.ink);
        if (r.utc) {
            formatLocal(r.utc, when, sizeof when, true);
            snprintf(head, sizeof head, "%s %s (RTC)", when, zoneName(r.utc));
        } else {
            snprintf(head, sizeof head, "CLOCK NOT SET, %u S AFTER BOOT", (unsigned)(r.uptimeMs / 1000));
        }
        lv_obj_t* stamp = ui::label(panel, head, fonts().small, r.utc ? t.muted : t.warn);
        lv_obj_set_pos(stamp, 0, 40);
        lv_obj_t* note = ui::label(panel, "UNVERIFIED OBSERVATION", fonts().small, t.muted);
        lv_obj_set_pos(note, 0, 70);
        lv_obj_t* text = ui::label(panel, r.text, fonts().small, t.ink);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(text, pw - 4 * g);
        lv_obj_set_pos(text, 0, 110);
    }

    // ---- REPORTS ---------------------------------------------------------------------------

    void buildReports(Frame& frame) {
        if (form_ < 0) buildReportPicker(frame);
        else buildReportForm(frame);
    }

    void buildReportPicker(Frame& frame) {
        std::vector<ui::Row> rows;
        const std::vector<ReportForm>& forms = reportForms();
        for (size_t i = 0; i < forms.size(); i++) {
            rows.push_back({forms[i].title, std::to_string(forms[i].fields.size()) + " BOXES", [this, i] { openForm((int)i); }});
        }
        lv_obj_t* list = ui::list(shell_, frame.body, 0, 0, frame.w, frame.h, rows);
        // Focus the first form rather than BACK (the title row's first button), so ENTER opens a form.
        if (lv_obj_get_child_count(list) > 0) shell_.preferFocus(lv_obj_get_child(list, 0));
    }

    // Opens a form with its time and location boxes already filled from the device, where it knows them.
    void openForm(int form) {
        const ReportForm& f = reportForms()[form];
        form_ = form;
        field_ = 0;
        values_.assign(f.fields.size(), "");
        for (size_t k = 0; k < f.fields.size(); k++) {
            if (f.fields[k].autofill == ReportField::Time) values_[k] = clockText();
            if (f.fields[k].autofill == ReportField::Location) values_[k] = ownGridRef();
        }
        shell_.rebuild();
    }

    void buildReportForm(Frame& frame) {
        const Theme& t = shell_.th();
        const ReportForm& form = reportForms()[form_];
        const int g = ui::kGap, listW = 520;

        // The boxes on the left: ">" marks the one being typed in, "+" a box that has something in it.
        std::vector<ui::Row> rows;
        for (size_t k = 0; k < form.fields.size(); k++) {
            std::string mark = (int)k == field_ ? "> " : values_[k].empty() ? "  " : "+ ";
            rows.push_back({mark + form.fields[k].label, "", [this, k] {
                                field_ = (int)k;
                                shell_.rebuild();
                            }});
        }
        ui::list(shell_, frame.body, 0, 0, listW, frame.h, rows);

        // The box being typed in, the buttons, a preview of the message, and the keyboard.
        int px = listW + 2 * g, pw = frame.w - px;
        const int k = field_;
        lv_obj_t* field = ui::textField(shell_, frame.body, pw, form.fields[k].hint, kMaxReportBox);
        lv_obj_set_pos(field, px, 0);
        lv_obj_set_height(field, ui::kTouchBig);
        lv_textarea_set_text(field, values_[k].c_str());
        ui::onEvent(field, LV_EVENT_VALUE_CHANGED, [this, field, k] { values_[k] = lv_textarea_get_text(field); });
        shell_.focusable(field);
        shell_.preferFocus(field);

        const int by = ui::kTouchBig + g, bw = 200;
        lv_obj_t* save = ui::button(shell_, frame.body, "SAVE", [this] { saveReport(); }, bw, ui::kTouchBig);
        lv_obj_set_pos(save, px, by);

        const int sy = by + ui::kTouchBig + g;
        lv_obj_t* preview = ui::label(frame.body, reportSummary(form, values_, clockText()), fonts().small, t.muted);
        lv_label_set_long_mode(preview, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(preview, pw);
        lv_obj_set_pos(preview, px, sy);

    }

    // HH:MM on the device's clock (UK time), or "" when the clock isn't set.
    std::string clockText() const {
        int64_t utc = shell_.nowUtc();
        if (!utc) return "";
        CivilTime c = civilFromUtc(localFromUtc(utc));
        char b[8];
        snprintf(b, sizeof b, "%02d:%02d", c.hour, c.minute);
        return b;
    }

    // Our own grid reference from the position service, or "" when there is no fix.
    std::string ownGridRef() {
        PositionService::Fix f = position_.own();
        double e = 0, n = 0;
        if (!f.valid || !osgrid::toGrid(f.lat, f.lon, e, n)) return "";
        return osgrid::format(e, n, 8);
    }

    // Writes the report as a text file in MESHDECK/reports. Returns its name, or "" when it failed.
    std::string saveReport() {
        const ReportForm& form = reportForms()[form_];
        std::string body = reportSummary(form, values_, clockText()) + "\n\n" + form.title + "\n";
        for (size_t k = 0; k < form.fields.size(); k++)
            body += std::string(form.fields[k].label) + ": " + values_[k] + "\n";

        char stamp[40];
        int64_t utc = shell_.nowUtc();
        if (utc) {
            CivilTime c = civilFromUtc(localFromUtc(utc));
            snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d", c.year, c.month, c.day, c.hour, c.minute, c.second);
        } else {
            snprintf(stamp, sizeof stamp, "NOCLOCK-%u", (unsigned)(shell_.platform().uptimeMs() / 1000));
        }
        std::string name = std::string(form.code) + "-" + stamp + ".txt";

        Paths p = shell_.paths();
        if (!p.available()) {
            shell_.notify("NO STORAGE: THE REPORT WAS NOT SAVED");
            return "";
        }
        makeDirs(p.folder(Folder::Reports));
        if (!writeFileAtomic(p.file(Folder::Reports, name), body)) {
            shell_.notify("REPORT NOT SAVED: THE CARD WRITE FAILED");
            return "";
        }
        return name;
    }


    // ---- INVENTORY -------------------------------------------------------------------------

    // Loads the stock file once. A missing card or an unreadable file shows as an error instead.
    void ensureInventory() {
        if (invLoaded_) return;
        Paths p = shell_.paths();
        if (!p.available()) {
            invError_ = "NO STORAGE: INVENTORY NEEDS THE SD CARD";
            return;
        }
        makeDirs(p.folder(Folder::Inventory));
        if (!inv_.load(p.file(Folder::Inventory, "inventory.txt"))) {
            invError_ = "CAN'T READ THE INVENTORY FILE";
            return;
        }
        invError_.clear();
        invLoaded_ = true;
    }

    void saveInventory() {
        if (!inv_.save()) shell_.notify("INVENTORY NOT SAVED: THE CARD WRITE FAILED");
    }

    void buildInventory(Frame& frame) {
        ensureInventory();
        if (!invError_.empty()) {
            ui::label(frame.body, invError_, fonts().body, shell_.th().warn);
            return;
        }
        if (loc_.empty()) buildLocations(frame);
        else buildLocation(frame);
    }

    // The locations, with a box to add one. Tapping a location opens its items.
    void buildLocations(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap, listW = 520;
        const int64_t now = shell_.nowUtc();

        std::vector<ui::Row> rows;
        for (const std::string& name : inv_.locations()) {
            int items = 0, expiring = 0;
            for (const Inventory::Item& it : inv_.items()) {
                if (it.location != name) continue;
                items++;
                int days = 0;
                if (Inventory::daysLeft(it.expiry, now, days) && days <= kExpiringDays) expiring++;
            }
            std::string detail = std::to_string(items) + " ITEMS";
            if (expiring) detail += ", " + std::to_string(expiring) + " EXPIRING";
            rows.push_back({name, detail, [this, name] {
                                loc_ = name;
                                selItem_ = -1;
                                pendingDelete_ = false;
                                invDraft_.clear();
                                shell_.rebuild();
                            }});
        }
        if (rows.empty()) rows.push_back({"NO LOCATIONS YET", "TYPE ONE ON THE RIGHT", nullptr});
        ui::list(shell_, frame.body, 0, 0, listW, frame.h, rows);

        int px = listW + 2 * g, pw = frame.w - px;
        int items = (int)inv_.items().size();
        std::string summary = std::to_string(items) + " ITEMS IN ALL, " + std::to_string(inv_.expiringCount(now, kExpiringDays)) +
                              " EXPIRING WITHIN " + std::to_string(kExpiringDays) + " DAYS";
        if (!now) summary += "  (CLOCK NOT SET: DATES NOT CHECKED)";
        lv_obj_t* head = ui::label(frame.body, summary, fonts().small, t.muted);
        lv_label_set_long_mode(head, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(head, pw);
        lv_obj_set_pos(head, px, 0);

        const int boxY = 56, addY = boxY + ui::kTouchMin + g;
        typeBox(frame, px, boxY, pw, "NEW LOCATION, E.G. CAR");
        lv_obj_t* addBtn = ui::button(shell_, frame.body, "ADD LOCATION", [this] { addLocation(); }, 280, ui::kTouchMin);
        lv_obj_set_pos(addBtn, px, addY);
    }

    // The items in one location, on the left; the chosen item and the typing box on the right.
    void buildLocation(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap, listW = 520, btnH = ui::kTouchMin;
        const int64_t now = shell_.nowUtc();
        std::vector<Inventory::Item>& items = inv_.items();

        std::vector<ui::Row> rows;
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].location != loc_) continue;
            std::string text = std::string((int)i == selItem_ ? "> " : "  ") + std::to_string(items[i].qty) + "  x  " + items[i].name;
            rows.push_back({text, expiryBadge(items[i].expiry, now), [this, i] {
                                selItem_ = (int)i;
                                pendingDelete_ = false;
                                shell_.rebuild();
                            }});
        }
        if (rows.empty()) rows.push_back({"NO ITEMS YET", "ADD ONE ON THE RIGHT", nullptr});
        ui::list(shell_, frame.body, 0, 0, listW, frame.h - btnH - g, rows);

        lv_obj_t* kit = ui::button(shell_, frame.body, "STARTER KIT", [this] { addStarterKit(); }, 250, btnH);
        lv_obj_set_pos(kit, 0, frame.h - btnH);
        lv_obj_t* checked = ui::button(shell_, frame.body, "CHECKED NOW", [this] { checkLocation(); }, 250, btnH);
        lv_obj_set_pos(checked, 250 + g, frame.h - btnH);

        int px = listW + 2 * g, pw = frame.w - px;
        Inventory::Location* where = inv_.location(loc_);
        std::string checkedText = "NOT CHECKED YET";
        if (where && where->checkedUtc) {
            char when[40];
            formatLocal(where->checkedUtc, when, sizeof when, false);
            checkedText = std::string("LAST CHECKED ") + when + " " + zoneName(where->checkedUtc);
        }
        lv_obj_t* head = ui::label(frame.body, loc_, fonts().body, t.ink);
        lv_obj_set_pos(head, px, 0);
        lv_obj_t* checkedLine = ui::label(frame.body, checkedText, fonts().small, t.muted);
        lv_obj_set_pos(checkedLine, px, 40);

        Inventory::Item* it = picked();
        if (it) {
            lv_obj_t* name = ui::label(frame.body, it->name, fonts().body, t.ink);
            lv_obj_set_pos(name, px, 80);
            std::string line = "QTY " + std::to_string(it->qty) + "   " + Inventory::expiryText(it->expiry, now);
            lv_obj_t* detail = ui::label(frame.body, line, fonts().small, t.muted);
            lv_obj_set_pos(detail, px, 120);
            lv_obj_t* minus = ui::button(shell_, frame.body, "-1", [this] { adjustQty(-1); }, 120, btnH);
            lv_obj_set_pos(minus, px, 160);
            lv_obj_t* plus = ui::button(shell_, frame.body, "+1", [this] { adjustQty(1); }, 120, btnH);
            lv_obj_set_pos(plus, px + 128, 160);
            lv_obj_t* del = ui::button(shell_, frame.body, pendingDelete_ ? "CONFIRM DELETE" : "DELETE", [this] { deleteItem(); }, 220, btnH);
            lv_obj_set_pos(del, px + 256, 160);
        } else {
            lv_obj_t* pick = ui::label(frame.body, "TAP AN ITEM ON THE LEFT TO CHANGE ITS COUNT, DATE OR NAME", fonts().small, t.muted);
            lv_label_set_long_mode(pick, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(pick, pw);
            lv_obj_set_pos(pick, px, 80);
        }

        const int boxY = 216, addY = boxY + btnH + g;
        typeBox(frame, px, boxY, pw, "NEW ITEM, OR A PICKED ITEM'S DATE AS YYYY-MM-DD");
        lv_obj_t* addItem = ui::button(shell_, frame.body, "ADD ITEM", [this] { addItemFromBox(); }, 200, btnH);
        lv_obj_set_pos(addItem, px, addY);
        lv_obj_t* expiry = ui::button(shell_, frame.body, "SET DATE", [this] { setExpiry(); }, 200, btnH);
        lv_obj_set_pos(expiry, px + 208, addY);
    }

    // A short word for a row's right side: EXPIRED, SOON (within kExpiringDays) or nothing.
    static std::string expiryBadge(const std::string& expiry, int64_t now) {
        int days = 0;
        if (!Inventory::daysLeft(expiry, now, days)) return "";
        if (days < 0) return "EXPIRED";
        if (days <= kExpiringDays) return "SOON";
        return "";
    }

    // A one-line box for typing on a USB keyboard. What is typed is kept in invDraft_.
    void typeBox(Frame& frame, int x, int y, int w, const char* placeholder) {
        lv_obj_t* field = ui::textField(shell_, frame.body, w, placeholder, kMaxInvText);
        lv_obj_set_pos(field, x, y);
        lv_obj_set_height(field, ui::kTouchMin);
        lv_textarea_set_text(field, invDraft_.c_str());
        ui::onEvent(field, LV_EVENT_VALUE_CHANGED, [this, field] { invDraft_ = lv_textarea_get_text(field); });
        shell_.focusable(field);
        shell_.preferFocus(field);
    }

    static std::string trimmed(const std::string& s) {
        size_t a = s.find_first_not_of(" \r\n\t"), b = s.find_last_not_of(" \r\n\t");
        return a == std::string::npos ? "" : s.substr(a, b - a + 1);
    }

    // The item chosen on the left, if it is in the open location.
    Inventory::Item* picked() {
        std::vector<Inventory::Item>& items = inv_.items();
        if (selItem_ < 0 || selItem_ >= (int)items.size() || items[selItem_].location != loc_) return nullptr;
        return &items[selItem_];
    }

    void leaveLocation() {
        loc_.clear();
        selItem_ = -1;
        pendingDelete_ = false;
        invDraft_.clear();
        shell_.rebuild();
    }

    void addLocation() {
        std::string name = trimmed(invDraft_);
        if (name.empty()) {
            shell_.notify("TYPE A LOCATION NAME FIRST");
            return;
        }
        for (const std::string& known : inv_.locations()) {
            if (known == name) {
                shell_.notify("THAT LOCATION ALREADY EXISTS");
                return;
            }
        }
        inv_.ensureLocation(name);
        saveInventory();
        loc_ = name;
        selItem_ = -1;
        invDraft_.clear();
        shell_.rebuild();
    }

    void addItemFromBox() {
        std::string name = trimmed(invDraft_);
        if (name.empty()) {
            shell_.notify("TYPE AN ITEM NAME FIRST");
            return;
        }
        for (const Inventory::Item& it : inv_.items()) {
            if (it.location == loc_ && it.name == name) {
                shell_.notify("ALREADY IN THIS LOCATION: CHANGE ITS COUNT INSTEAD");
                return;
            }
        }
        Inventory::Item item;
        item.name = name;
        item.location = loc_;
        inv_.items().push_back(item);
        selItem_ = (int)inv_.items().size() - 1;
        pendingDelete_ = false;
        invDraft_.clear();
        saveInventory();
        shell_.rebuild();
    }

    void setExpiry() {
        Inventory::Item* it = picked();
        if (!it) {
            shell_.notify("TAP AN ITEM ON THE LEFT FIRST");
            return;
        }
        std::string date = trimmed(invDraft_);
        if (!Inventory::validDate(date)) {
            shell_.notify("TYPE THE DATE AS YYYY-MM-DD, E.G. 2027-03-31");
            return;
        }
        it->expiry = date;
        invDraft_.clear();
        saveInventory();
        shell_.rebuild();
    }

    void adjustQty(int delta) {
        Inventory::Item* it = picked();
        if (!it) {
            shell_.notify("TAP AN ITEM ON THE LEFT FIRST");
            return;
        }
        it->qty = std::max(0, std::min(9999, it->qty + delta));
        pendingDelete_ = false;
        saveInventory();
        shell_.rebuild();
    }

    // Two presses, like the messages delete: the first asks, the second removes the item.
    void deleteItem() {
        Inventory::Item* it = picked();
        if (!it) {
            shell_.notify("TAP AN ITEM ON THE LEFT FIRST");
            return;
        }
        if (!pendingDelete_) {
            pendingDelete_ = true;
            shell_.notify("PRESS CONFIRM DELETE TO REMOVE " + it->name);
            shell_.rebuild();
            return;
        }
        pendingDelete_ = false;
        inv_.items().erase(inv_.items().begin() + selItem_);
        selItem_ = -1;
        saveInventory();
        shell_.rebuild();
    }

    void addStarterKit() {
        int added = inv_.addStarterKit(loc_);
        if (added) saveInventory();
        shell_.notify(added ? "ADDED " + std::to_string(added) + " STARTER KIT ITEMS AT 0: COUNT THEM"
                            : "THE STARTER KIT IS ALREADY IN THIS LOCATION");
        shell_.rebuild();
    }

    void checkLocation() {
        int64_t now = shell_.nowUtc();
        if (!now) {
            shell_.notify("THE CLOCK ISN'T SET, SO THE CHECK CAN'T BE DATED");
            return;
        }
        inv_.ensureLocation(loc_).checkedUtc = now;
        saveInventory();
        shell_.notify("LOCATION CHECKED");
        shell_.rebuild();
    }

    // ---- NOTES -----------------------------------------------------------------------------

    // Plain-text notes: one file each in MESHDECK/notes, named by when they were saved.
    struct NoteFile { std::string name, when, first; };

    // The newest notes first, with their date and first line.
    std::vector<NoteFile> listNotes() {
        std::vector<NoteFile> out;
        Paths p = shell_.paths();
        if (!p.available()) return out;
        std::vector<std::string> names;
        for (const DirEntry& e : listDir(p.folder(Folder::Notes), 500)) {
            if (!e.isDir && e.name.size() > 4 && e.name.compare(e.name.size() - 4, 4, ".txt") == 0) names.push_back(e.name);
        }
        std::sort(names.rbegin(), names.rend());   // the names start with the date, so this is newest first
        for (size_t i = 0; i < names.size() && i < kShownNotes; i++) {
            NoteFile n;
            n.name = names[i];
            n.when = noteWhen(n.name);
            std::string text;
            readFile(p.file(Folder::Notes, n.name), text, kMaxEntryBytes);
            n.first = firstLine(text, 40);
            out.push_back(n);
        }
        return out;
    }

    // "01/11 18:59" from "note-20261101-185900.txt"; anything else (no clock when saved) is shown as it is.
    static std::string noteWhen(const std::string& name) {
        if (name.size() < 21 || name.compare(0, 5, "note-") != 0 || !isdigit((unsigned char)name[5])) return name;
        return name.substr(10, 2) + "/" + name.substr(8, 2) + " " + name.substr(14, 2) + ":" + name.substr(16, 2);
    }

    // YYYYMMDD-HHMMSS from the clock, or NOCLOCK-seconds when the clock isn't set.
    std::string fileStamp() {
        int64_t utc = shell_.nowUtc();
        char stamp[40];
        if (utc) {
            CivilTime c = civilFromUtc(localFromUtc(utc));
            snprintf(stamp, sizeof stamp, "%04d%02d%02d-%02d%02d%02d", c.year, c.month, c.day, c.hour, c.minute, c.second);
        } else {
            snprintf(stamp, sizeof stamp, "NOCLOCK-%u", (unsigned)(shell_.platform().uptimeMs() / 1000));
        }
        return stamp;
    }

    void saveNote() {
        std::string text = trimmed(noteDraft_);
        if (text.empty()) {
            shell_.notify("TYPE A NOTE FIRST");
            return;
        }
        if (text.size() > kMaxEntryBytes) text.resize(kMaxEntryBytes);
        Paths p = shell_.paths();
        if (!p.available()) {
            shell_.notify("NO STORAGE: THE NOTE WAS NOT SAVED");
            return;
        }
        makeDirs(p.folder(Folder::Notes));
        std::string stamp = fileStamp();
        std::string path = p.file(Folder::Notes, "note-" + stamp + ".txt");
        for (int n = 2; fileExists(path); n++) path = p.file(Folder::Notes, "note-" + stamp + "-" + std::to_string(n) + ".txt");
        if (!writeFileAtomic(path, text + "\n")) {
            shell_.notify("NOTE NOT SAVED: THE CARD WRITE FAILED");
            return;
        }
        noteDraft_.clear();
        shell_.notify("NOTE SAVED");
        shell_.rebuild();
    }

    void openNote(const std::string& name) {
        selNote_ = name;
        noteText_.clear();
        readFile(shell_.paths().file(Folder::Notes, name), noteText_, kMaxEntryBytes);
        pendingNoteDelete_ = false;
        shell_.rebuild();
    }

    void closeNote() {
        selNote_.clear();
        noteText_.clear();
        pendingNoteDelete_ = false;
        shell_.rebuild();
    }

    // Two presses, like the other deletes: the first asks, the second removes the note.
    void deleteNote() {
        if (!pendingNoteDelete_) {
            pendingNoteDelete_ = true;
            shell_.notify("PRESS CONFIRM DELETE TO REMOVE THIS NOTE");
            shell_.rebuild();
            return;
        }
        bool ok = remove(shell_.paths().file(Folder::Notes, selNote_).c_str()) == 0;
        shell_.notify(ok ? "NOTE DELETED" : "COULD NOT DELETE THE NOTE");
        if (ok) closeNote();
        else {
            pendingNoteDelete_ = false;
            shell_.rebuild();
        }
    }

    // The note list on the left. On the right, either the note being read, or the box for a new one.
    void buildNotes(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap, listW = 520, btnH = ui::kTouchMin;

        std::vector<ui::Row> rows;
        for (const NoteFile& n : listNotes()) {
            std::string text = n.when + "  " + n.first;
            rows.push_back({text, "", [this, name = n.name] { openNote(name); }});
        }
        if (rows.empty()) rows.push_back({"NO NOTES YET", "TYPE ONE ON THE RIGHT", nullptr});
        ui::list(shell_, frame.body, 0, 0, listW, frame.h, rows);

        int px = listW + 2 * g, pw = frame.w - px;
        if (!selNote_.empty()) {
            lv_obj_t* head = ui::label(frame.body, noteWhen(selNote_), fonts().body, t.ink);
            lv_obj_set_pos(head, px, 0);
            lv_obj_t* panel = ui::panel(shell_, frame.body, px, 56, pw, frame.h - 56 - btnH - g);
            lv_obj_t* text = ui::label(panel, noteText_, fonts().small, t.ink);
            lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(text, pw - 4 * g);
            lv_obj_set_pos(text, 0, 0);

            lv_obj_t* newBtn = ui::button(shell_, frame.body, "NEW NOTE", [this] { closeNote(); }, 220, btnH);
            lv_obj_set_pos(newBtn, px, frame.h - btnH);
            lv_obj_t* del = ui::button(shell_, frame.body, pendingNoteDelete_ ? "CONFIRM DELETE" : "DELETE",
                                       [this] { deleteNote(); }, 240, btnH);
            lv_obj_set_pos(del, px + 228, frame.h - btnH);
            return;
        }

        lv_obj_t* box = lv_textarea_create(frame.body);
        lv_obj_set_pos(box, px, 0);
        lv_obj_set_size(box, pw, 200);
        lv_obj_set_style_text_font(box, fonts().small, 0);
        lv_obj_set_style_text_color(box, t.ink, 0);
        lv_obj_set_style_bg_color(box, t.surface, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(box, t.line, 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_border_color(box, t.ink, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(box, 4, LV_STATE_FOCUSED);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_set_style_pad_all(box, 12, 0);
        lv_textarea_set_max_length(box, (uint32_t)kMaxEntryBytes);
        lv_textarea_set_placeholder_text(box, "NEW NOTE: WHAT, WHERE, WHEN");
        lv_textarea_set_text(box, noteDraft_.c_str());
        ui::onEvent(box, LV_EVENT_VALUE_CHANGED, [this, box] { noteDraft_ = lv_textarea_get_text(box); });
        shell_.focusable(box);
        shell_.preferFocus(box);

        const int by = 200 + g;
        lv_obj_t* save = ui::button(shell_, frame.body, "SAVE NOTE", [this] { saveNote(); }, 220, btnH);
        lv_obj_set_pos(save, px, by);
        lv_obj_t* clear = ui::button(shell_, frame.body, "CLEAR", [this] { noteDraft_.clear(); shell_.rebuild(); }, 160, btnH);
        lv_obj_set_pos(clear, px + 228, by);

    }

    PositionService& position_;
    Inventory inv_;
    bool invLoaded_ = false;
    std::string invError_, loc_, invDraft_;   // INVENTORY: the error, the open location, what is typed
    int selItem_ = -1;                        // INVENTORY: the chosen item (an index into inv_.items())
    bool pendingDelete_ = false;              // INVENTORY: the first press of DELETE has been made

    std::string noteDraft_;                   // NOTES: the note being typed
    std::string selNote_, noteText_;          // NOTES: the file name and text of the note being read
    bool pendingNoteDelete_ = false;          // NOTES: the first press of DELETE has been made

    int form_ = -1;                    // REPORTS: the open form (-1 = the list of forms)
    int field_ = 0;                    // REPORTS: the box being typed in
    std::vector<std::string> values_;  // REPORTS: what is in each box of the open form

    Journal journal_;
    std::vector<Journal::Record> entries_;
    std::string draft_, error_;
    int selected_ = -1;
    int part_ = -1;   // -1 = the hub
};

const PlannedApp LogbookApp::kParts[LogbookApp::kPartCount] = {
    {"LOG ENTRIES", "The append-only field log", "V6", "Field log entries, safe against power loss."},
    {"REPORTS", "SITREP, METHANE, contact reports", "V6",
     "Structured reports filled in on the deck, saved, and sent over the mesh."},
    {"INVENTORY", "Kit, food, water, medical", "V6", "Stock lists with quantities and expiry dates."},
    {"NOTES", "Plain-text notes", "V6", "Quick notes, saved as text files on the card."},
};

std::unique_ptr<App> makeLogbookApp(Shell& shell, Services& services) {
    return std::make_unique<LogbookApp>(shell, services);
}

} // namespace meshdeck
