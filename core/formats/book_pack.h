// book_pack.h - reads MESHDECK book packs (.bk, made by tools/build_books.py from PDF and EPUB
// files). Ported from MESHDECK 1.x. Format notes: docs/formats/book-pack.md.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace meshdeck {

// A book made by tools/build_books.py: kind Pages (a PDF: one 16-grey PNG per page, inverted so
// ink is bright, plus each page's text for searching) or kind Text (an EPUB cut into sections of
// about 6,000 characters). Only the header, contents and index are held in memory; pages and
// text are read from the card when needed.
class BookPack {
public:
    enum Kind { Pages = 1, Text = 2 };
    struct TocEntry { uint32_t index; uint8_t level; std::string title; };
    struct Hit { uint32_t index; std::string snippet; };

    ~BookPack() { close(); }
    // headerOnly: just title, author, category, kind and count (for listing the shelf quickly).
    bool open(const std::string& path, std::string& error, bool headerOnly = false);
    void close();
    bool isOpen() const { return file_ != nullptr; }

    Kind kind() const { return kind_; }
    const std::string& title() const { return title_; }
    const std::string& author() const { return author_; }
    const std::string& category() const { return category_; }
    uint32_t count() const { return (uint32_t)index_.size(); }
    const std::vector<TocEntry>& toc() const { return toc_; }

    // Page image as 8-bit grey (0 = background, 255 = ink). Pages kind only.
    bool pageImage(uint32_t i, std::vector<uint8_t>& px, uint32_t& w, uint32_t& h, std::string& error);
    bool text(uint32_t i, std::string& out);
    // Case-insensitive search through every page/section, in order; at most maxHits results.
    std::vector<Hit> search(const std::string& query, size_t maxHits = 60);

private:
    struct Entry { uint32_t imgOff, imgLen, txtOff, txtLen; };
    bool readAt(uint32_t off, void* buf, size_t len);
    FILE* file_ = nullptr;
    Kind kind_ = Text;
    std::string title_, author_, category_, source_;
    std::vector<TocEntry> toc_;
    std::vector<Entry> index_;
};

} // namespace meshdeck
