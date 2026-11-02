// zip_archive.h - reads the files inside a zip archive, which is what an EPUB is. Only what EPUB
// needs: stored and deflated entries. Encrypted archives and zip64 (over 4 GB) are refused with a
// reason. Deflate uses LVGL's bundled lodepng inflate, so this file belongs in the UI library.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace meshdeck {

class ZipArchive {
public:
    struct Entry {
        std::string name;
        uint16_t method = 0;          // 0 = stored, 8 = deflated
        uint32_t compSize = 0, size = 0, localOffset = 0;
    };

    ~ZipArchive() { close(); }
    // Reads the archive's list of files (its central directory). False, with a reason, if the file
    // isn't a zip archive or uses a feature this reader doesn't support.
    bool open(const std::string& path, std::string& error);
    void close();
    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(const std::string& name) const;
    // Reads one file. False, with a reason, if it is too big (over maxBytes) or damaged.
    bool read(const Entry& e, std::string& out, size_t maxBytes, std::string& error);

private:
    FILE* file_ = nullptr;
    std::vector<Entry> entries_;
};

} // namespace meshdeck
