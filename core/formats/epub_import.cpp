// epub_import.cpp - see epub_import.h. An EPUB is a zip archive. Its META-INF/container.xml names the
// package file (the OPF). The package lists the content files (manifest) and their reading order
// (spine). Each content file is XHTML: we keep its text and cut the text into sections.
#include "formats/epub_import.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "formats/zip_archive.h"
#include "kernel/storage.h"

namespace meshdeck {
namespace {

const size_t kMaxXml = 2 * 1024 * 1024;        // container and package files
const size_t kMaxChapter = 4 * 1024 * 1024;    // one content file
const size_t kMaxBook = 4 * 1024 * 1024;       // all the text kept from one book
const size_t kSectionChars = 6000;             // the reader's section size (see book-pack.md)
const size_t kMaxTitle = 40;                   // a contents title is at most this many characters
const size_t kMaxString = 1000;                // the pack format's limit for one string

// ---- small helpers for the markup -------------------------------------------------------------

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// The value of attribute `name` inside one tag's text, e.g. href in <item href="ch1.xhtml" ...>.
// Matches only whole attribute names (so "id" never matches "idref"). Empty if absent.
std::string attr(const std::string& tag, const char* name) {
    size_t n = strlen(name);
    for (size_t i = 1; i + n < tag.size(); i++) {
        if (!isspace((unsigned char)tag[i - 1]) || tag.compare(i, n, name) != 0) continue;
        size_t q = i + n;
        while (q < tag.size() && isspace((unsigned char)tag[q])) q++;
        if (q >= tag.size() || tag[q] != '=') continue;
        q++;
        while (q < tag.size() && isspace((unsigned char)tag[q])) q++;
        if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\'')) continue;
        char quote = tag[q++];
        size_t close = tag.find(quote, q);
        return close == std::string::npos ? "" : tag.substr(q, close - q);
    }
    return "";
}

// The text between <tag ...> and </tag>, with markup removed; "" if there is no such element.
std::string elementText(const std::string& xml, const char* tag) {
    std::string open = std::string("<") + tag, close = std::string("</") + tag + ">";
    size_t at = xml.find(open);
    while (at != std::string::npos && at + open.size() < xml.size() && !isspace((unsigned char)xml[at + open.size()]) &&
           xml[at + open.size()] != '>')
        at = xml.find(open, at + 1);
    if (at == std::string::npos) return "";
    size_t start = xml.find('>', at);
    size_t end = xml.find(close, start);
    if (start == std::string::npos || end == std::string::npos) return "";
    return xhtmlToText(xml.substr(start + 1, end - start - 1));
}

// Decodes %20-style escapes in a file name inside the EPUB.
std::string percentDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Appends a code point as UTF-8.
void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

// Writes a string as the pack format has it: u16 length, then the bytes (cut to kMaxString).
void putString(std::string& out, std::string s) {
    if (s.size() > kMaxString) s.resize(kMaxString);
    uint16_t n = (uint16_t)s.size();
    out.append((const char*)&n, 2);
    out += s;
}

void putU32(std::string& out, uint32_t v) { out.append((const char*)&v, 4); }

// Cuts text into pieces of at most `max` bytes, at line breaks where possible and never inside a
// UTF-8 character.
std::vector<std::string> cutSections(const std::string& text, size_t max) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = pos + max;
        if (end >= text.size()) {
            end = text.size();
        } else {
            size_t nl = text.rfind('\n', end);
            if (nl != std::string::npos && nl > pos + max / 2) end = nl;
            while (end > pos && ((unsigned char)text[end] & 0xC0) == 0x80) end--;   // back to a character start
        }
        out.push_back(text.substr(pos, end - pos));
        pos = end;
        while (pos < text.size() && (text[pos] == '\n' || text[pos] == ' ')) pos++;
    }
    return out;
}

// The first line of a section, as a contents title.
std::string titleOf(const std::string& text) {
    std::string line = text.substr(0, text.find('\n'));
    if (line.size() > kMaxTitle) {
        line.resize(kMaxTitle);
        while (!line.empty() && ((unsigned char)line.back() & 0xC0) == 0x80) line.pop_back();
        line += "...";
    }
    return line;
}

// Folder part of a path inside the archive ("OEBPS/content.opf" -> "OEBPS/"), "" at the top.
std::string folderOf(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

} // namespace

std::string xhtmlToText(const std::string& xhtml) {
    std::string out;
    size_t i = 0;
    while (i < xhtml.size()) {
        char c = xhtml[i];
        if (c == '<') {
            // Comments and declarations are skipped whole.
            if (xhtml.compare(i, 4, "<!--") == 0) {
                size_t end = xhtml.find("-->", i + 4);
                i = end == std::string::npos ? xhtml.size() : end + 3;
                continue;
            }
            size_t end = xhtml.find('>', i);
            if (end == std::string::npos) break;
            std::string tag = xhtml.substr(i + 1, end - i - 1);
            bool closing = !tag.empty() && tag[0] == '/';
            size_t nameStart = closing ? 1 : 0, nameEnd = nameStart;
            while (nameEnd < tag.size() && (isalnum((unsigned char)tag[nameEnd]) || tag[nameEnd] == ':')) nameEnd++;
            std::string name = lower(tag.substr(nameStart, nameEnd - nameStart));
            i = end + 1;

            // Scripts, styles and the head hold no reading text: skip up to their closing tag.
            if (!closing && (name == "script" || name == "style" || name == "head")) {
                size_t stop = xhtml.find("</" + name, i);
                i = stop == std::string::npos ? xhtml.size() : xhtml.find('>', stop) + 1;
                continue;
            }
            // Block-level tags start a new line; headings and paragraphs also leave a blank line after.
            bool block = name == "p" || name == "div" || name == "br" || name == "li" || name == "tr" ||
                         name == "blockquote" || name == "section" || name == "table" ||
                         (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6');
            if (block) {
                if (!out.empty() && out.back() != '\n') out += '\n';
                bool gap = closing && name != "br";
                if (gap && !out.empty() && out.back() == '\n' && (out.size() < 2 || out[out.size() - 2] != '\n')) out += '\n';
            }
            continue;
        }
        if (c == '&') {
            // Entities: the common named ones, and numeric references such as &#8217; or &#x2019;.
            size_t semi = xhtml.find(';', i);
            if (semi != std::string::npos && semi - i <= 10) {
                std::string ent = xhtml.substr(i + 1, semi - i - 1);
                bool done = true;
                if (ent == "amp") out += '&';
                else if (ent == "lt") out += '<';
                else if (ent == "gt") out += '>';
                else if (ent == "quot") out += '"';
                else if (ent == "apos") out += '\'';
                else if (ent == "nbsp") out += ' ';
                else if (ent == "mdash") appendUtf8(out, 0x2014);
                else if (ent == "ndash") appendUtf8(out, 0x2013);
                else if (ent == "hellip") appendUtf8(out, 0x2026);
                else if (ent == "lsquo" || ent == "rsquo") appendUtf8(out, ent == "lsquo" ? 0x2018 : 0x2019);
                else if (ent == "ldquo" || ent == "rdquo") appendUtf8(out, ent == "ldquo" ? 0x201C : 0x201D);
                else if (ent.size() > 1 && ent[0] == '#') {
                    unsigned long cp = ent[1] == 'x' || ent[1] == 'X' ? strtoul(ent.c_str() + 2, nullptr, 16)
                                                                      : strtoul(ent.c_str() + 1, nullptr, 10);
                    if (cp > 0 && cp < 0x110000) appendUtf8(out, (uint32_t)cp);
                } else {
                    done = false;
                }
                if (done) { i = semi + 1; continue; }
            }
            out += c;
            i++;
            continue;
        }
        // Whitespace in the source is only a space in the text (HTML collapses runs of it).
        if (isspace((unsigned char)c)) {
            if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
        } else {
            out += c;
        }
        i++;
    }
    // Tidy: no spaces at line ends, no runs of blank lines.
    std::string tidy;
    for (char c : out) {
        if (c == '\n' && !tidy.empty() && tidy.back() == ' ') tidy.pop_back();
        if (c == '\n' && !tidy.empty() && tidy.back() == '\n') continue;
        tidy += c;
    }
    while (!tidy.empty() && (tidy.back() == ' ' || tidy.back() == '\n')) tidy.pop_back();
    while (!tidy.empty() && (tidy[0] == ' ' || tidy[0] == '\n')) tidy.erase(0, 1);
    return tidy;
}

bool importEpub(const std::string& epubPath, const std::string& packPath, EpubResult& result, std::string& error) {
    result = EpubResult();
    ZipArchive zip;
    if (!zip.open(epubPath, error)) return false;

    // Step 1: container.xml names the package file.
    std::string container;
    const ZipArchive::Entry* containerEntry = zip.find("META-INF/container.xml");
    if (!containerEntry) { error = "NOT AN EPUB (NO CONTAINER)"; return false; }
    if (!zip.read(*containerEntry, container, kMaxXml, error)) return false;
    size_t rootAt = container.find("<rootfile ");   // with the space: "<rootfiles>" is the wrapper, not the entry
    std::string opfPath = rootAt == std::string::npos ? "" : attr(container.substr(rootAt, container.find('>', rootAt) - rootAt), "full-path");
    const ZipArchive::Entry* opfEntry = opfPath.empty() ? nullptr : zip.find(opfPath);
    if (!opfEntry) { error = "NOT AN EPUB (NO PACKAGE FILE)"; return false; }

    // Step 2: the package file gives the title and the manifest (file names by id), and the spine
    // gives the reading order as ids.
    std::string opf;
    if (!zip.read(*opfEntry, opf, kMaxXml, error)) return false;
    result.title = elementText(opf, "dc:title");
    std::string author = elementText(opf, "dc:creator");
    std::vector<std::pair<std::string, std::string>> manifest;   // id, href
    for (size_t at = opf.find("<item"); at != std::string::npos; at = opf.find("<item", at + 1)) {
        char after = at + 5 < opf.size() ? opf[at + 5] : 0;
        if (!isspace((unsigned char)after) && after != '/' && after != '>') continue;
        std::string tag = opf.substr(at, opf.find('>', at) - at);
        manifest.push_back({attr(tag, "id"), percentDecode(attr(tag, "href"))});
    }
    std::vector<std::string> spine;
    for (size_t at = opf.find("<itemref"); at != std::string::npos; at = opf.find("<itemref", at + 1)) {
        std::string tag = opf.substr(at, opf.find('>', at) - at);
        std::string idref = attr(tag, "idref");
        for (const auto& item : manifest) {
            if (item.first == idref) { spine.push_back(item.second); break; }
        }
    }
    if (spine.empty()) { error = "NOT AN EPUB (NO READING ORDER)"; return false; }

    // Step 3: read each chapter in reading order and cut its text into sections. Each chapter's first
    // section gets a contents entry.
    std::string base = folderOf(opfPath);
    struct Section { std::string title, text; bool starts; };
    std::vector<Section> sections;
    size_t kept = 0;
    for (const std::string& href : spine) {
        const ZipArchive::Entry* e = zip.find(base + href);
        if (!e) continue;
        std::string xhtml, readError;
        if (!zip.read(*e, xhtml, kMaxChapter, readError)) continue;   // a damaged chapter is skipped, not fatal
        std::string text = xhtmlToText(xhtml);
        if (text.empty()) continue;
        if (kept + text.size() > kMaxBook) {
            text.resize(kMaxBook - kept);
            result.truncated = true;
        }
        kept += text.size();
        bool first = true;
        for (const std::string& piece : cutSections(text, kSectionChars)) {
            sections.push_back({first ? titleOf(piece) : std::string(), piece, first});
            first = false;
        }
        if (result.truncated) break;
    }
    if (sections.empty()) { error = "NO READABLE TEXT IN THIS EPUB"; return false; }
    if (result.title.empty()) result.title = "UNTITLED";
    result.sections = (uint32_t)sections.size();

    // Step 4: write the pack. The header and contents come first, then the index (one entry per
    // section, with placeholder offsets), then the section texts. The offsets are worked out before
    // anything is written.
    std::string head = "BBOK";
    uint16_t version = 2;
    head.append((const char*)&version, 2);
    head += (char)2;   // kind: Text
    head += (char)0;   // reserved: the reader takes an 8-byte block here
    putString(head, result.title);
    putString(head, author);
    putString(head, "IMPORTED EPUB");
    putString(head, epubPath.substr(epubPath.find_last_of('/') + 1));
    putU32(head, (uint32_t)sections.size());
    std::vector<uint32_t> tocIndex;
    for (uint32_t i = 0; i < sections.size(); i++) if (sections[i].starts) tocIndex.push_back(i);
    putU32(head, (uint32_t)tocIndex.size());
    for (uint32_t i : tocIndex) {
        putU32(head, i);
        head += (char)0;   // level: chapters are flat
        putString(head, sections[i].title.empty() ? "SECTION" : sections[i].title);
    }
    size_t blobStart = head.size() + sections.size() * 16;
    std::string index, blobs;
    for (const Section& s : sections) {
        putU32(index, 0);                                  // imgOff: no page images in a text book
        putU32(index, 0);                                  // imgLen
        putU32(index, (uint32_t)(blobStart + blobs.size()));   // txtOff
        putU32(index, (uint32_t)s.text.size());            // txtLen
        blobs += s.text;
    }
    std::string file = head + index + blobs;
    if (!writeFileAtomic(packPath, file)) { error = "CAN'T WRITE THE BOOK (CARD FULL OR MISSING?)"; return false; }
    return true;
}

} // namespace meshdeck
