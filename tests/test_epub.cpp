// test_epub.cpp - the EPUB import: the text of a chapter, reading a zip archive (stored and deflated
// entries), and a whole EPUB turned into a book pack the reader opens in reading order.
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "doctest.h"
#include "formats/book_pack.h"
#include "formats/epub_import.h"
#include "formats/zip_archive.h"
#include "kernel/storage.h"
#include "test_util.h"

namespace {

void put16(std::string& s, uint16_t v) { s.append((const char*)&v, 2); }
void put32(std::string& s, uint32_t v) { s.append((const char*)&v, 4); }

struct ZipFile {
    std::string name, data;   // data is the packed bytes as stored in the archive
    uint16_t method;          // 0 stored, 8 deflated
    uint32_t size;            // the file's real size
};

// A deflate stream holding one stored (uncompressed) block: valid deflate, so it exercises the inflate path.
std::string storedDeflate(const std::string& raw) {
    std::string s;
    s += (char)0x01;                       // final block, stored
    put16(s, (uint16_t)raw.size());
    put16(s, (uint16_t)~raw.size());
    return s + raw;
}

// Builds a zip archive in memory: local headers and data, the central directory, then the end record.
std::string makeZip(const std::vector<ZipFile>& files) {
    std::string out, central;
    for (const ZipFile& f : files) {
        uint32_t offset = (uint32_t)out.size();
        put32(out, 0x04034b50); put16(out, 20); put16(out, 0); put16(out, f.method); put16(out, 0); put16(out, 0);
        put32(out, 0); put32(out, (uint32_t)f.data.size()); put32(out, f.size);
        put16(out, (uint16_t)f.name.size()); put16(out, 0);
        out += f.name;
        out += f.data;

        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, f.method);
        put16(central, 0); put16(central, 0); put32(central, 0);
        put32(central, (uint32_t)f.data.size()); put32(central, f.size);
        put16(central, (uint16_t)f.name.size()); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0); put32(central, 0); put32(central, offset);
        central += f.name;
    }
    uint32_t dirOffset = (uint32_t)out.size();
    out += central;
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0);
    put16(out, (uint16_t)files.size()); put16(out, (uint16_t)files.size());
    put32(out, (uint32_t)central.size()); put32(out, dirOffset); put16(out, 0);
    return out;
}

std::string writeFile(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    REQUIRE(f);
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return path;
}

} // namespace

TEST_CASE("epub: chapter text keeps paragraphs and headings, decodes entities, skips scripts") {
    std::string xhtml =
        "<?xml version=\"1.0\"?><html><head><title>Hidden</title></head><body>"
        "<script>var x = 1;</script><h1>Chapter One</h1>"
        "<p>Hello &amp; welcome,   to   <em>the</em> book.</p>"
        "<p>It&#8217;s &mdash; fine.</p><!-- a note --></body></html>";
    std::string text = meshdeck::xhtmlToText(xhtml);
    CHECK(text.find("Hidden") == std::string::npos);
    CHECK(text.find("var x") == std::string::npos);
    CHECK(text.find("Chapter One\n") == 0);
    CHECK(text.find("Hello & welcome, to the book.") != std::string::npos);
    CHECK(text.find("It\xE2\x80\x99s \xE2\x80\x94 fine.") != std::string::npos);   // ’ and — as UTF-8
    CHECK(text.find("a note") == std::string::npos);
}

TEST_CASE("epub: a zip archive lists its files and reads stored and deflated ones") {
    std::string dir = testutil::scratch("epub-zip");
    std::string raw = "the quick brown fox";
    std::string path = writeFile(dir + "/t.zip", makeZip({
        {"plain.txt", "stored text", 0, 11},
        {"packed.txt", storedDeflate(raw), 8, (uint32_t)raw.size()},
    }));

    meshdeck::ZipArchive zip;
    std::string err;
    REQUIRE(zip.open(path, err));
    REQUIRE(zip.entries().size() == 2);
    const meshdeck::ZipArchive::Entry* a = zip.find("plain.txt");
    const meshdeck::ZipArchive::Entry* b = zip.find("packed.txt");
    REQUIRE(a);
    REQUIRE(b);
    std::string out;
    REQUIRE(zip.read(*a, out, 1000, err));
    CHECK(out == "stored text");
    REQUIRE(zip.read(*b, out, 1000, err));
    CHECK(out == raw);
    CHECK_FALSE(zip.read(*b, out, 4, err));   // over the limit is refused
    CHECK(err == "FILE INSIDE THE ARCHIVE IS TOO BIG");
}

TEST_CASE("epub: a file that is not a zip archive is refused with a reason") {
    std::string dir = testutil::scratch("epub-not-zip");
    std::string path = writeFile(dir + "/plain.epub", "this is just text, not an archive at all, long enough to search");
    meshdeck::ZipArchive zip;
    std::string err;
    CHECK_FALSE(zip.open(path, err));
    CHECK(err == "NOT A ZIP ARCHIVE");
}

TEST_CASE("epub: an EPUB becomes a text book, chapters in reading order, that the reader opens") {
    std::string dir = testutil::scratch("epub-import");
    const std::string container =
        "<?xml version=\"1.0\"?><container><rootfiles>"
        "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
        "</rootfiles></container>";
    const std::string opf =
        "<?xml version=\"1.0\"?><package><metadata><dc:title>Test Book</dc:title>"
        "<dc:creator>A Writer</dc:creator></metadata>"
        "<manifest><item id=\"c1\" href=\"ch1.xhtml\"/><item id=\"c2\" href=\"text/ch%202.xhtml\"/></manifest>"
        "<spine><itemref idref=\"c2\"/><itemref idref=\"c1\"/></spine></package>";
    const std::string ch1 = "<html><body><h1>First Chapter</h1><p>Hello &amp; welcome.</p></body></html>";
    const std::string ch2 = "<html><body><h1>Second</h1><p>Wild&#8217;s text.</p></body></html>";
    std::string epub = writeFile(dir + "/book.epub", makeZip({
        {"META-INF/container.xml", container, 0, (uint32_t)container.size()},
        {"OEBPS/content.opf", opf, 0, (uint32_t)opf.size()},
        {"OEBPS/ch1.xhtml", ch1, 0, (uint32_t)ch1.size()},
        {"OEBPS/text/ch 2.xhtml", storedDeflate(ch2), 8, (uint32_t)ch2.size()},
    }));

    std::string pack = dir + "/book.bk";
    meshdeck::EpubResult result;
    std::string err;
    bool imported = meshdeck::importEpub(epub, pack, result, err);
    INFO("import error: " << err);
    REQUIRE(imported);
    CHECK(result.title == "Test Book");
    CHECK(result.sections == 2);
    CHECK_FALSE(result.truncated);

    meshdeck::BookPack book;
    REQUIRE(book.open(pack, err));
    CHECK(book.kind() == meshdeck::BookPack::Text);
    CHECK(book.title() == "Test Book");
    CHECK(book.author() == "A Writer");
    REQUIRE(book.count() == 2);
    std::string first, second;
    REQUIRE(book.text(0, first));
    REQUIRE(book.text(1, second));
    CHECK(first.find("Second") == 0);                           // the spine put chapter 2 first
    CHECK(second.find("First Chapter") == 0);
    CHECK(second.find("Hello & welcome.") != std::string::npos);
    CHECK(first.find("Wild\xE2\x80\x99s text.") != std::string::npos);
    REQUIRE(book.toc().size() == 2);
    CHECK(book.toc()[0].title == "Second");
    CHECK(book.toc()[1].title == "First Chapter");
    CHECK_FALSE(meshdeck::fileExists(pack + ".tmp"));            // the temporary file was renamed away
}
