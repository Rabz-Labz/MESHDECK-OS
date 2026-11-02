// messages_app.cpp - MESSAGES: chat with anyone on the mesh.
//
// Screens:
//   CHATS    every channel and every conversation with one node, newest first, with unread counts
//   THREAD   the messages, each sent one with its honest state (QUEUED / SENT / DELIVERED /
//            FAILED and why), and a box to type in (on the physical keyboard).
//   NODES    every node the radio has heard: tap one to start a conversation with it
//   CONTACT  give a node a nickname and a note (shown everywhere instead of its short name)
// Deleting a conversation asks for a second press to confirm.
//
// The mesh itself (services/mesh.h) keeps running in the background whatever is on screen; this
// app only shows it. A message that arrives while you are elsewhere puts a notice on the hint
// line and an unread count on the home tile.
#include <stdio.h>
#include <algorithm>
#include <memory>
#include "kernel/clock.h"
#include "services/contacts.h"
#include "services/mesh.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {

class MessagesApp : public App {
public:
    using Thread = MeshService::Thread;
    using Message = MeshService::Message;

    MessagesApp(Shell& shell, MeshService& mesh, Contacts& contacts) : App(shell), mesh_(mesh), contacts_(contacts) {
        reads_.load(shell.paths().file(Folder::Messages, "read.txt"));
        mesh_.onReceive = [this](const Message& m) { incoming(m); };
    }

    const char* name() const override { return "MESSAGES"; }
    const char* subtitle() const override { return "Mesh chat with anyone"; }

    std::string tileNote() const override {
        int n = 0;
        for (const Message& m : mesh_.messages())
            if (!m.outgoing && m.seq > reads_.lastRead(mesh_.threadOf(m).key())) n++;
        return n ? std::to_string(n) + " UNREAD" : "";
    }

    void onOpen() override {
        view_ = View::Chats;
    }

    void build(Frame& frame) override {
        seenVersion_ = mesh_.version();
        field_ = nullptr;
        msgBox_ = nullptr;
        switch (view_) {
        case View::Chats: buildChats(frame); break;
        case View::Thread: buildThread(frame); break;
        case View::Nodes: buildNodes(frame); break;
        case View::Contact: buildContact(frame); break;
        }
    }

    // Redraw when the mesh changes (at most twice a second), keeping what was being typed.
    void tick() override {
        uint32_t now = shell_.platform().uptimeMs();
        if (mesh_.version() != seenVersion_ && now - lastRedraw_ > 500 && view_ != View::Contact) {
            lastRedraw_ = now;
            shell_.rebuild();
        }
        if (confirmDelete_ && now - confirmAt_ > 5000) {
            confirmDelete_ = false;
            shell_.rebuild();
        }
    }

    bool onBack() override {
        switch (view_) {
        case View::Chats: return false;
        case View::Contact: view_ = View::Thread; break;
        default: view_ = View::Chats; break;
        }
        shell_.rebuild();
        return true;
    }

    // N opens NEW CHAT from the chat list; UP/DOWN scroll the message list while it has focus.
    bool onKey(uint32_t key, uint8_t) override {
        if (view_ == View::Chats && (key == 'n' || key == 'N')) { go(View::Nodes); return true; }
        if (!msgBox_ || lv_group_get_focused(lv_obj_get_group(msgBox_)) != msgBox_) return false;
        if (key == LV_KEY_UP) { lv_obj_scroll_by_bounded(msgBox_, 0, 120, LV_ANIM_OFF); return true; }
        if (key == LV_KEY_DOWN) { lv_obj_scroll_by_bounded(msgBox_, 0, -120, LV_ANIM_OFF); return true; }
        return false;
    }

    const char* hint() const override {
        switch (view_) {
        case View::Thread: return "TYPE, THEN ENTER TO SEND  |  UP/DOWN TO REACH THE LIST";
        case View::Contact: return "TYPE A NICKNAME AND A NOTE, THEN SAVE";
        case View::Chats: return "N NEW CHAT  |  ARROWS + ENTER  |  CTRL+P SCREENSHOT";
        default: return "ARROWS + ENTER  |  CTRL+P SCREENSHOT";
        }
    }

private:
    enum class View { Chats, Thread, Nodes, Contact };

    // ---- Names ---------------------------------------------------------------------------

    // A node's name as shown everywhere: its nickname if it's a contact, else its short name.
    std::string nodeLabel(uint32_t node) const {
        if (const Contacts::Contact* c = contacts_.find(node)) return c->nickname;
        return mesh_.nodeName(node);
    }

    std::string threadLabel(const Thread& t) const {
        return t.direct ? nodeLabel(t.node) : "# " + mesh_.channelName(t.channel);
    }

    static std::string timeText(int64_t utc) {
        if (!utc) return "--:--";
        CivilTime c = civilFromUtc(localFromUtc(utc));
        char buf[8];
        snprintf(buf, sizeof buf, "%02d:%02d", c.hour, c.minute);
        return buf;
    }

    std::string linkLine() const {
        if (mesh_.link() == MeshService::Link::None) return "NO RADIO: CONNECT THE MESHTASTIC RADIO (UNIT C6L) TO THE USB-A PORT.";
        if (mesh_.link() == MeshService::Link::Connecting) return "RADIO FOUND, CONNECTING...";
        return "RADIO READY   " + mesh_.region() + "   " + std::to_string(mesh_.nodes().size()) + " NODES KNOWN";
    }

    int unread(const Thread& t) const {
        std::string key = t.key();
        int n = 0;
        for (const Message& m : mesh_.messages())
            if (!m.outgoing && m.seq > reads_.lastRead(key) && mesh_.threadOf(m).key() == key) n++;
        return n;
    }

    // ---- CHATS -----------------------------------------------------------------------------

    void buildChats(Frame& frame) {
        const Theme& t = shell_.th();
        frame.action("NEW CHAT", [this] { go(View::Nodes); });
        lv_obj_t* link = ui::label(frame.body, linkLine(), fonts().body, mesh_.link() == MeshService::Link::Ready ? t.muted : t.warn);
        lv_obj_set_pos(link, 0, 0);

        // Every channel the radio has, then every conversation, newest activity first.
        struct Entry { Thread thread; uint32_t lastSeq; const Message* last; };
        std::vector<Entry> entries;
        auto find = [&](const Thread& th) -> Entry* {
            for (Entry& e : entries)
                if (e.thread == th) return &e;
            return nullptr;
        };
        for (const MeshService::Channel& c : mesh_.channels()) {
            Thread th;
            th.channel = c.index;
            entries.push_back({th, 0, nullptr});
        }
        for (const Message& m : mesh_.messages()) {
            Thread th = mesh_.threadOf(m);
            Entry* e = find(th);
            if (!e) {
                entries.push_back({th, 0, nullptr});
                e = &entries.back();
            }
            e->lastSeq = m.seq;
            e->last = &m;
        }
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.lastSeq > b.lastSeq; });

        std::vector<ui::Row> rows;
        for (const Entry& e : entries) {
            int n = unread(e.thread);
            std::string detail = n ? std::to_string(n) + " NEW" : e.last ? timeText(e.last->utc) : "";
            std::string text = threadLabel(e.thread);
            if (e.last) text += ":  " + e.last->text;
            Thread th = e.thread;
            rows.push_back({text, detail, [this, th] { openThread(th); }});
        }
        if (rows.empty()) rows.push_back({"NO CONVERSATIONS YET. NEW CHAT LISTS EVERY NODE HEARD.", "", nullptr});
        ui::list(shell_, frame.body, 0, 48, frame.w, frame.h - 48, rows);
    }

    // ---- THREAD ----------------------------------------------------------------------------

    void buildThread(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap;
        if (thread_.direct) frame.action("CONTACT", [this] { openContact(); });
        frame.action(confirmDelete_ ? "CONFIRM DELETE" : "DELETE", [this] { deletePressed(); });

        // Who this is.
        std::string head = threadLabel(thread_);
        if (thread_.direct) {
            auto it = mesh_.nodes().find(thread_.node);
            if (it != mesh_.nodes().end()) {
                const MeshService::Node& n = it->second;
                char buf[96];
                snprintf(buf, sizeof buf, "   %s   SNR %.1f   %s", n.longName.c_str(), n.snr,
                         n.hops < 0 ? "HOPS ?" : (std::to_string(n.hops) + (n.hops == 1 ? " HOP" : " HOPS")).c_str());
                head += buf;
            }
        } else {
            head += "   EVERYONE ON THIS CHANNEL";
        }
        lv_obj_t* who = ui::label(frame.body, head, fonts().body, t.muted);
        lv_obj_set_pos(who, 0, 0);

        // The compose row at the bottom.
        int composeH = ui::kTouchMin + 8;
        int composeY = frame.h - composeH;
        int boxY = 44, boxH = composeY - g - boxY;

        msgBox_ = ui::box(frame.body, 0, boxY, frame.w, boxH);
        lv_obj_add_flag(msgBox_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_flex_flow(msgBox_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(msgBox_, 10, 0);
        lv_obj_set_style_pad_right(msgBox_, 12, 0);
        lv_obj_set_style_border_color(msgBox_, t.line, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(msgBox_, 2, LV_STATE_FOCUSED);
        shell_.focusable(msgBox_);
        uint32_t newest = 0;
        int shown = 0;
        for (const Message& m : mesh_.messages()) {
            if (!(mesh_.threadOf(m) == thread_)) continue;
            addMessage(m, frame.w - 24);
            newest = m.seq;
            shown++;
        }
        if (!shown) {
            lv_obj_t* none = ui::label(msgBox_, "NO MESSAGES YET. TYPE BELOW TO START.", fonts().body, t.muted);
            (void)none;
        }
        lv_obj_update_layout(msgBox_);
        lv_obj_scroll_to_y(msgBox_, LV_COORD_MAX, LV_ANIM_OFF);   // newest at the bottom
        if (newest) reads_.markRead(thread_.key(), newest);

        const int sendW = 180;
        field_ = ui::textField(shell_, frame.body, frame.w - sendW - g, "TYPE A MESSAGE", MeshService::kMaxText);
        lv_obj_set_pos(field_, 0, composeY);
        lv_obj_set_height(field_, composeH);
        lv_textarea_set_text(field_, draft_.c_str());
        ui::onEvent(field_, LV_EVENT_VALUE_CHANGED, [this] { draft_ = lv_textarea_get_text(field_); });
        ui::onEvent(field_, LV_EVENT_READY, [this] { sendDraft(); });
        lv_obj_t* send = ui::button(shell_, frame.body, "SEND", [this] { sendDraft(); }, sendW, composeH);
        lv_obj_set_pos(send, frame.w - sendW, composeY);
        shell_.preferFocus(field_);
    }

    // One message: a small header line (who, when, and for ours the state) and the text.
    void addMessage(const Message& m, int w) {
        const Theme& t = shell_.th();
        std::string head = timeText(m.utc) + "  " + (m.outgoing ? std::string("YOU") : nodeLabel(m.from));
        if (m.outgoing) {
            head += "  -  " + std::string(MeshService::stateName(m.state));
            if (!m.note.empty()) head += ": " + m.note;
        } else if (m.snr != 0) {
            char snr[24];
            snprintf(snr, sizeof snr, "  (SNR %.1f)", m.snr);
            head += snr;
        }
        lv_obj_t* h = ui::label(msgBox_, head, fonts().small,
                                m.outgoing && m.state == MeshService::State::Failed ? t.warn : t.muted);
        lv_obj_set_width(h, w);
        lv_obj_t* body = ui::label(msgBox_, m.text, fonts().body, t.ink);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(body, w);
        if (m.outgoing) {   // ours on the right, like most chat apps
            lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_RIGHT, 0);
        }
    }

    void sendDraft() {
        std::string error;
        if (!mesh_.send(draft_, thread_, error)) {
            shell_.notify(error);
            return;
        }
        draft_.clear();
        shell_.rebuild();
    }

    void deletePressed() {
        if (!confirmDelete_) {
            confirmDelete_ = true;
            confirmAt_ = shell_.platform().uptimeMs();
            shell_.notify("PRESS CONFIRM DELETE WITHIN 5 SECONDS TO DELETE THIS CONVERSATION");
            shell_.rebuild();
            return;
        }
        confirmDelete_ = false;
        int n = mesh_.deleteThread(thread_);
        shell_.notify("CONVERSATION DELETED (" + std::to_string(n) + " MESSAGES)");
        go(View::Chats);
    }

    // ---- NODES -----------------------------------------------------------------------------

    void buildNodes(Frame& frame) {
        const Theme& t = shell_.th();
        lv_obj_t* link = ui::label(frame.body, "EVERY NODE THE RADIO HAS HEARD. CHOOSE ONE TO MESSAGE IT DIRECTLY.",
                                   fonts().body, t.muted);
        lv_obj_set_pos(link, 0, 0);
        std::vector<const MeshService::Node*> nodes;
        for (const auto& [num, n] : mesh_.nodes())
            if (num != mesh_.myNum()) nodes.push_back(&n);
        // Heard this session first (most recent first), then the radio's list by its last-heard time.
        std::sort(nodes.begin(), nodes.end(), [](const MeshService::Node* a, const MeshService::Node* b) {
            if ((a->heardMs != 0) != (b->heardMs != 0)) return a->heardMs != 0;
            if (a->heardMs != b->heardMs) return a->heardMs > b->heardMs;
            return a->lastHeardUtc > b->lastHeardUtc;
        });
        std::vector<ui::Row> rows;
        for (const MeshService::Node* n : nodes) {
            std::string text = nodeLabel(n->num);
            if (contacts_.find(n->num)) text += " (" + mesh_.nodeName(n->num) + ")";
            if (!n->longName.empty()) text += "  " + n->longName;
            char detail[48];
            snprintf(detail, sizeof detail, "SNR %.1f  %s", n->snr,
                     n->hops < 0 ? "" : (std::to_string(n->hops) + (n->hops == 1 ? " HOP" : " HOPS")).c_str());
            uint32_t num = n->num;
            rows.push_back({text, detail, [this, num] {
                                Thread th;
                                th.direct = true;
                                th.node = num;
                                openThread(th);
                            }});
        }
        if (rows.empty()) rows.push_back({mesh_.link() == MeshService::Link::Ready ? "NO OTHER NODES HEARD YET." : linkLine(), "", nullptr});
        ui::list(shell_, frame.body, 0, 48, frame.w, frame.h - 48, rows);
    }

    // ---- CONTACT ---------------------------------------------------------------------------

    void buildContact(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap;
        std::string who = mesh_.nodeName(thread_.node);
        auto it = mesh_.nodes().find(thread_.node);
        if (it != mesh_.nodes().end() && !it->second.longName.empty()) who += "  " + it->second.longName;
        char id[16];
        snprintf(id, sizeof id, "  !%08x", (unsigned)thread_.node);
        lv_obj_t* l = ui::label(frame.body, "NODE  " + who + id, fonts().body, t.muted);
        lv_obj_set_pos(l, 0, 0);

        int fieldW = frame.w - 220 - g;
        lv_obj_t* nl = ui::label(frame.body, "NICKNAME", fonts().small, t.muted);
        lv_obj_set_pos(nl, 0, 52);
        nickField_ = ui::textField(shell_, frame.body, fieldW, "E.G. DAD", Contacts::kMaxNickname);
        lv_obj_set_pos(nickField_, 0, 80);
        lv_textarea_set_text(nickField_, nickDraft_.c_str());
        ui::onEvent(nickField_, LV_EVENT_VALUE_CHANGED, [this] { nickDraft_ = lv_textarea_get_text(nickField_); });
        lv_obj_t* tl = ui::label(frame.body, "NOTE", fonts().small, t.muted);
        lv_obj_set_pos(tl, 0, 160);
        noteField_ = ui::textField(shell_, frame.body, fieldW, "E.G. CARDPUTER, USUALLY AT HOME", Contacts::kMaxNote);
        lv_obj_set_pos(noteField_, 0, 188);
        lv_textarea_set_text(noteField_, noteDraft_.c_str());
        ui::onEvent(noteField_, LV_EVENT_VALUE_CHANGED, [this] { noteDraft_ = lv_textarea_get_text(noteField_); });

        lv_obj_t* save = ui::button(shell_, frame.body, "SAVE", [this] { saveContact(); }, 200, ui::kTouchMin + 8);
        lv_obj_set_pos(save, frame.w - 200, 80);
        lv_obj_t* remove = ui::button(shell_, frame.body, "REMOVE", [this] {
            contacts_.remove(thread_.node);
            nickDraft_.clear();
            noteDraft_.clear();
            shell_.notify("CONTACT REMOVED");
            go(View::Thread);
        }, 200, ui::kTouchMin + 8);
        lv_obj_set_pos(remove, frame.w - 200, 188);

        ui::onEvent(nickField_, LV_EVENT_READY, [this] { saveContact(); });
        ui::onEvent(noteField_, LV_EVENT_READY, [this] { saveContact(); });
        shell_.preferFocus(nickField_);
    }

    void openContact() {
        const Contacts::Contact* c = contacts_.find(thread_.node);
        nickDraft_ = c ? c->nickname : "";
        noteDraft_ = c ? c->note : "";
        go(View::Contact);
    }

    void saveContact() {
        bool ok = contacts_.set(thread_.node, nickDraft_, noteDraft_);
        shell_.notify(!ok ? "CONTACT NOT SAVED: NO STORAGE" : nickDraft_.empty() ? "CONTACT REMOVED" : "CONTACT SAVED");
        go(View::Thread);
    }

    // ---- Navigation and notices ------------------------------------------------------------

    void go(View v) {
        view_ = v;
        confirmDelete_ = false;
        shell_.rebuild();
    }

    void openThread(const Thread& t) {
        if (!(t == thread_)) draft_.clear();
        thread_ = t;
        go(View::Thread);
    }

    // A message arrived: tell the user unless they are looking at that conversation.
    void incoming(const Message& m) {
        bool watching = shell_.currentApp() >= 0 && &shell_.app(shell_.currentApp()) == this &&
                        view_ == View::Thread && mesh_.threadOf(m) == thread_;
        if (watching) return;
        std::string text = m.text.size() > 50 ? m.text.substr(0, 50) + "..." : m.text;
        shell_.notify("MESSAGE FROM " + nodeLabel(m.from) + ": " + text);
    }

    MeshService& mesh_;
    Contacts& contacts_;
    ReadMarks reads_;
    View view_ = View::Chats;
    Thread thread_;
    std::string draft_, nickDraft_, noteDraft_;
    bool confirmDelete_ = false;
    uint32_t confirmAt_ = 0, seenVersion_ = 0, lastRedraw_ = 0;
    lv_obj_t* field_ = nullptr;
    lv_obj_t* msgBox_ = nullptr;
    lv_obj_t* nickField_ = nullptr;
    lv_obj_t* noteField_ = nullptr;
};

std::unique_ptr<App> makeMessagesApp(Shell& shell, MeshService& mesh, Contacts& contacts) {
    return std::make_unique<MessagesApp>(shell, mesh, contacts);
}

} // namespace meshdeck
