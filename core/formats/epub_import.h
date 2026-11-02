// epub_import.h - turns an EPUB into a text book pack for the LIBRARY reader (format notes:
// docs/formats/book-pack.md). The import runs on the Tab5 itself, one chapter at a time.
#pragma once
#include <stdint.h>
#include <string>

namespace meshdeck {

struct EpubResult {
    std::string title;
    uint32_t sections = 0;
    bool truncated = false;   // the book was longer than the import's limit and was cut short
};

// Converts epubPath into a Text pack at packPath (written as a temporary file, then renamed, so a
// power cut never leaves half a book). Returns false, with a reason in error, if the EPUB can't be read.
bool importEpub(const std::string& epubPath, const std::string& packPath, EpubResult& result, std::string& error);

// Plain text from one XHTML document: tags removed, paragraphs and headings kept as lines, entities
// decoded, runs of spaces collapsed. Scripts, styles and the head are skipped.
std::string xhtmlToText(const std::string& xhtml);

} // namespace meshdeck
