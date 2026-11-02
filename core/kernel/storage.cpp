// storage.cpp - see storage.h.
#include "storage.h"
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <algorithm>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace meshdeck {

std::string Paths::folder(Folder f) const {
    if (!available()) return "";
    const std::string b = root_ + "/MESHDECK";
    switch (f) {
    case Folder::Root: return b;
    case Folder::Log: return b + "/log";
    case Folder::Messages: return b + "/msg";
    case Folder::Notes: return b + "/notes";
    case Folder::Reports: return b + "/reports";
    case Folder::Inventory: return b + "/inventory";
    case Folder::Maps: return b + "/maps";
    case Folder::Books: return b + "/books";
    case Folder::Games: return b + "/games";
    case Folder::Files: return b + "/Files";
    case Folder::Screenshots: return b + "/Files/Screenshots";
    case Folder::Inbox: return b + "/Files/Inbox";
    case Folder::Update: return b + "/update";
    }
    return b;
}

bool Paths::createAll() const {
    if (!available()) return false;
    bool ok = true;
    for (int f = (int)Folder::Root; f <= (int)Folder::Update; f++) ok = makeDirs(folder((Folder)f)) && ok;
    return ok;
}

bool syncFile(void* stdioFile) {
    FILE* f = static_cast<FILE*>(stdioFile);
#ifdef _WIN32
    return _commit(_fileno(f)) == 0;
#else
    return fsync(fileno(f)) == 0;
#endif
}

bool readFile(const std::string& path, std::string& out, size_t maxBytes) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[4096];
    size_t n;
    while (out.size() < maxBytes && (n = fread(buf, 1, std::min(sizeof buf, maxBytes - out.size()), f)) > 0)
        out.append(buf, n);
    fclose(f);
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    // Step 1: write the whole new content to a temporary file next to the target.
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size() && fflush(f) == 0;
    // Step 2: make sure it has reached the card before touching the old file.
    if (ok) ok = syncFile(f);
    fclose(f);
    if (!ok) {
        remove(tmp.c_str());
        return false;
    }
    // Step 3: replace. FAT (the SD card) can't rename over an existing file, so the old one is
    // removed first; a power cut in this gap leaves path.tmp, which listDir hides and the next
    // write replaces.
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static bool makeOneDir(const std::string& p) {
#ifdef _WIN32
    return _mkdir(p.c_str()) == 0 || fileExists(p);
#else
    return mkdir(p.c_str(), 0775) == 0 || fileExists(p);
#endif
}

bool makeDirs(const std::string& path) {
    // Create each level in turn: "a", "a/b", "a/b/c". Windows drive letters ("C:") and a leading
    // "/" are skipped as levels of their own.
    bool ok = true;
    for (size_t i = 1; i <= path.size(); i++) {
        if (i != path.size() && path[i] != '/' && path[i] != '\\') continue;
        std::string level = path.substr(0, i);
        if (level.size() == 2 && level[1] == ':') continue;
        ok = makeOneDir(level);
    }
    return ok;
}

std::vector<DirEntry> listDir(const std::string& path, size_t limit) {
    std::vector<DirEntry> out;
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.empty() || name[0] == '.' || (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0)) continue;
        DirEntry entry{name, false, 0};
        struct stat st;
        if (stat((path + "/" + name).c_str(), &st) == 0) {
            entry.isDir = S_ISDIR(st.st_mode);
            entry.size = (uint64_t)st.st_size;
        }
        out.push_back(entry);
        if (out.size() >= limit) break;
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        return a.isDir != b.isDir ? a.isDir : a.name < b.name;
    });
    return out;
}

} // namespace meshdeck
