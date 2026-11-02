// test_books.cpp - the book pack reader, on a small text book built in the test.
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include "doctest.h"
#include "formats/book_pack.h"
#include "kernel/storage.h"
#include "test_util.h"

namespace {

void putU16Str(std::string& out, const std::string& s) {
    uint16_t n = (uint16_t)s.size();
    out.append((const char*)&n, 2);
    out += s;
}

void putU32(std::string& out, uint32_t v) { out.append((const char*)&v, 4); }

// A text book (kind 2) with three sections, a contents list with one entry, and an index.
std::string makeTextBook() {
    const std::vector<std::string> sections = {"The first section talks about water.",
                                               "The second section is about shelter and fire.",
                                               "Section three ends the book."};
    std::string head = "BBOK";
    head += char(2);   // version 2, little-endian
    head += char(0);
    head += char(2);   // kind: Text
    head += char(0);
    putU16Str(head, "Test Guide");
    putU16Str(head, "A. Author");
    putU16Str(head, "TEST CATEGORY");
    putU16Str(head, "test source");
    putU32(head, (uint32_t)sections.size());
    putU32(head, 1);                      // one contents entry
    putU32(head, 1);                      // it points at section 1
    head += char(1);                      // level
    putU16Str(head, "Shelter");
    // The index: four u32 per section (image offset, image length, text offset, text length).
    const size_t indexBytes = 16 * sections.size();
    uint32_t textAt = (uint32_t)(head.size() + indexBytes);
    std::string index, text;
    for (const std::string& s : sections) {
        putU32(index, 0);
        putU32(index, 0);
        putU32(index, textAt + (uint32_t)text.size());
        putU32(index, (uint32_t)s.size());
        text += s;
    }
    return head + index + text;
}

} // namespace

TEST_CASE("book pack: a text book opens, lists its contents and reads sections") {
    std::string path = testutil::scratch("bookpack") + "/guide.bk";
    REQUIRE(meshdeck::writeFileAtomic(path, makeTextBook()));
    meshdeck::BookPack book;
    std::string err;
    REQUIRE(book.open(path, err));
    CHECK(book.kind() == meshdeck::BookPack::Text);
    CHECK(book.title() == "Test Guide");
    CHECK(book.author() == "A. Author");
    CHECK(book.category() == "TEST CATEGORY");
    CHECK(book.count() == 3);
    REQUIRE(book.toc().size() == 1);
    CHECK(book.toc()[0].title == "Shelter");
    CHECK(book.toc()[0].index == 1);
    std::string text;
    REQUIRE(book.text(1, text));
    CHECK(text == "The second section is about shelter and fire.");
    CHECK_FALSE(book.text(7, text));
}

TEST_CASE("book pack: search finds the section and a snippet, case-insensitively") {
    std::string path = testutil::scratch("bookpack-search") + "/guide.bk";
    REQUIRE(meshdeck::writeFileAtomic(path, makeTextBook()));
    meshdeck::BookPack book;
    std::string err;
    REQUIRE(book.open(path, err));
    auto hits = book.search("FIRE");
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].index == 1);
    CHECK(hits[0].snippet.find("fire") != std::string::npos);
    CHECK(book.search("zebra").empty());
    CHECK(book.search("").empty());
}

TEST_CASE("book pack: headers only for the shelf, and bad files refused") {
    std::string dir = testutil::scratch("bookpack-bad");
    std::string path = dir + "/guide.bk";
    REQUIRE(meshdeck::writeFileAtomic(path, makeTextBook()));
    meshdeck::BookPack shelf;
    std::string err;
    REQUIRE(shelf.open(path, err, true));
    CHECK(shelf.count() == 3);
    CHECK(shelf.title() == "Test Guide");
    CHECK(shelf.toc().empty());   // not read in header-only mode
    REQUIRE(meshdeck::writeFileAtomic(dir + "/junk.bk", "NOT A BOOK AT ALL"));
    meshdeck::BookPack junk;
    CHECK_FALSE(junk.open(dir + "/junk.bk", err));
    CHECK(err == "NOT A BOOK PACK");
}
