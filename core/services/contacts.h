// contacts.h - nicknames for mesh nodes, and which messages have been read.
//
// Contacts: MESHDECK/msg/contacts.txt, one line per contact, readable on any PC:
//     !1a01a2b3|Dad|Cardputer, usually at home
// (node number in hex, nickname, note). The nickname replaces the node's own short name
// everywhere in MESHDECK.
//
// Read marks: MESHDECK/msg/read.txt, one "threadkey=seq" line per thread: the newest message seen
// in that thread. Messages after it count as unread. Message sequence numbers come from the
// journal's order, so they are the same after every restart.
//
// Both files are small and rewritten whole with writeFileAtomic.
#pragma once
#include <stdint.h>
#include <map>
#include <string>

namespace meshdeck {

class Contacts {
public:
    struct Contact {
        std::string nickname, note;
    };
    static constexpr size_t kMaxNickname = 20, kMaxNote = 60;

    bool load(const std::string& path);    // missing file = no contacts (not an error)
    bool save();
    const Contact* find(uint32_t node) const;
    // Adds or updates. Text is trimmed, '|' and line breaks removed, lengths capped. An empty
    // nickname removes the contact.
    bool set(uint32_t node, const std::string& nickname, const std::string& note);
    bool remove(uint32_t node);
    const std::map<uint32_t, Contact>& all() const { return contacts_; }

private:
    static std::string clean(const std::string& s, size_t max);
    std::string path_;
    std::map<uint32_t, Contact> contacts_;
};

class ReadMarks {
public:
    bool load(const std::string& path);
    uint32_t lastRead(const std::string& key) const;
    // Marks a thread read up to seq; saves only if it moved forward.
    void markRead(const std::string& key, uint32_t seq);

private:
    bool save();
    std::string path_;
    std::map<std::string, uint32_t> marks_;
};

} // namespace meshdeck
