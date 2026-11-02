// contacts.cpp - see contacts.h.
#include "services/contacts.h"
#include <stdio.h>
#include <stdlib.h>
#include "kernel/storage.h"

namespace meshdeck {

// Splits text into lines and calls fn(line) for each non-empty one.
template <typename Fn> static void eachLine(const std::string& text, Fn fn) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) fn(line);
        start = end + 1;
    }
}

// ---------------------------------------------------------------------------------------------
// Contacts

std::string Contacts::clean(const std::string& s, size_t max) {
    std::string out;
    for (char c : s)
        if (c != '|' && c != '\n' && c != '\r') out += c;
    size_t a = out.find_first_not_of(' '), b = out.find_last_not_of(' ');
    out = a == std::string::npos ? "" : out.substr(a, b - a + 1);
    if (out.size() > max) out.resize(max);
    return out;
}

bool Contacts::load(const std::string& path) {
    path_ = path;
    contacts_.clear();
    std::string text;
    if (path.empty() || !readFile(path, text)) return false;
    eachLine(text, [&](const std::string& line) {
        // !hex|nickname|note
        size_t p1 = line.find('|'), p2 = p1 == std::string::npos ? p1 : line.find('|', p1 + 1);
        if (line[0] != '!' || p1 == std::string::npos) return;
        uint32_t node = (uint32_t)strtoul(line.substr(1, p1 - 1).c_str(), nullptr, 16);
        std::string nick = line.substr(p1 + 1, p2 == std::string::npos ? std::string::npos : p2 - p1 - 1);
        std::string note = p2 == std::string::npos ? "" : line.substr(p2 + 1);
        if (node && !nick.empty()) contacts_[node] = {clean(nick, kMaxNickname), clean(note, kMaxNote)};
    });
    return true;
}

bool Contacts::save() {
    if (path_.empty()) return false;
    std::string out;
    char id[16];
    for (const auto& [node, c] : contacts_) {
        snprintf(id, sizeof id, "!%08x", (unsigned)node);
        out += std::string(id) + "|" + c.nickname + "|" + c.note + "\n";
    }
    return writeFileAtomic(path_, out);
}

const Contacts::Contact* Contacts::find(uint32_t node) const {
    auto it = contacts_.find(node);
    return it == contacts_.end() ? nullptr : &it->second;
}

bool Contacts::set(uint32_t node, const std::string& nickname, const std::string& note) {
    std::string nick = clean(nickname, kMaxNickname);
    if (nick.empty()) return remove(node);
    contacts_[node] = {nick, clean(note, kMaxNote)};
    return save();
}

bool Contacts::remove(uint32_t node) {
    contacts_.erase(node);
    return save();
}

// ---------------------------------------------------------------------------------------------
// Read marks

bool ReadMarks::load(const std::string& path) {
    path_ = path;
    marks_.clear();
    std::string text;
    if (path.empty() || !readFile(path, text)) return false;
    eachLine(text, [&](const std::string& line) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) marks_[line.substr(0, eq)] = (uint32_t)strtoul(line.c_str() + eq + 1, nullptr, 10);
    });
    return true;
}

uint32_t ReadMarks::lastRead(const std::string& key) const {
    auto it = marks_.find(key);
    return it == marks_.end() ? 0 : it->second;
}

void ReadMarks::markRead(const std::string& key, uint32_t seq) {
    if (seq <= lastRead(key)) return;
    marks_[key] = seq;
    save();
}

bool ReadMarks::save() {
    if (path_.empty()) return false;
    std::string out;
    for (const auto& [key, seq] : marks_) out += key + "=" + std::to_string(seq) + "\n";
    return writeFileAtomic(path_, out);
}

} // namespace meshdeck
