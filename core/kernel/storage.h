// storage.h - file helpers and the one place that knows where MESHDECK keeps its data.
//
// All user data lives under <dataRoot>/MESHDECK/ (dataRoot = the SD card, or internal flash
// without a card). Apps ask Paths for their folder instead of building paths themselves, so the
// layout is documented here and can't drift. File writes that replace a whole file go through
// writeFileAtomic, so a power cut leaves either the old file or the new one, never half of each.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace meshdeck {

// The folders under <dataRoot>/MESHDECK. Each has one owner; see docs/architecture.md.
enum class Folder {
    Root,          // MESHDECK/            settings and security
    Log,           // MESHDECK/log/        system journal and field log
    Messages,      // MESHDECK/msg/        message journal, contacts
    Notes,         // MESHDECK/notes/      plain-text notes
    Reports,       // MESHDECK/reports/    saved reports
    Inventory,     // MESHDECK/inventory/
    Maps,          // MESHDECK/maps/       map packs (*.map)
    Books,         // MESHDECK/books/      book packs (*.bk) and reading positions
    Games,         // MESHDECK/games/      best scores (scores.txt)
    Files,         // MESHDECK/Files/      the user's own files (visible in FILES)
    Screenshots,   // MESHDECK/Files/Screenshots/
    Inbox,         // MESHDECK/Files/Inbox/  books and files to import
    Update,        // MESHDECK/update/     signed firmware updates
};

class Paths {
public:
    explicit Paths(const std::string& dataRoot) : root_(dataRoot) {}
    bool available() const { return !root_.empty(); }
    // Full path of a folder (no trailing slash), or "" when there is no storage.
    std::string folder(Folder f) const;
    // Full path of a file in a folder.
    std::string file(Folder f, const std::string& name) const { return available() ? folder(f) + "/" + name : ""; }
    // Creates every folder above, so apps never have to. Returns false if any can't be made.
    bool createAll() const;

private:
    std::string root_;
};

struct DirEntry {
    std::string name;
    bool isDir;
    uint64_t size;
};

// Reads at most maxBytes of a file into out. False if it can't be opened.
bool readFile(const std::string& path, std::string& out, size_t maxBytes = 64 * 1024);
// Writes path.tmp, flushes it to the card, then replaces path with it.
bool writeFileAtomic(const std::string& path, const std::string& data);
bool fileExists(const std::string& path);
// Makes a folder and any missing parents (like mkdir -p).
bool makeDirs(const std::string& path);
// Lists a folder: sub-folders first, then files, each alphabetical. Hidden (".x") and
// temporary ("x.tmp") files are left out.
std::vector<DirEntry> listDir(const std::string& path, size_t limit = 1000);
// Flushes a file's data to the storage device (fsync), so it survives a power cut.
bool syncFile(void* stdioFile);

} // namespace meshdeck
