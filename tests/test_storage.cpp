// test_storage.cpp - paths, atomic writes and folder listing.
#include "doctest.h"
#include "kernel/storage.h"
#include "test_util.h"

using namespace meshdeck;

TEST_CASE("Paths: one documented layout, all folders created") {
    std::string root = testutil::scratch("storage-paths");
    Paths p(root);
    CHECK(p.folder(Folder::Screenshots) == root + "/MESHDECK/Files/Screenshots");
    CHECK(p.file(Folder::Books, "positions.txt") == root + "/MESHDECK/books/positions.txt");
    REQUIRE(p.createAll());
    CHECK(fileExists(root + "/MESHDECK/Files/Inbox"));
    CHECK(fileExists(root + "/MESHDECK/msg"));
    Paths none("");
    CHECK_FALSE(none.available());
    CHECK(none.folder(Folder::Notes).empty());
}

TEST_CASE("writeFileAtomic replaces the whole file and leaves no temporary behind") {
    std::string dir = testutil::scratch("storage-atomic");
    std::string path = dir + "/note.txt";
    REQUIRE(writeFileAtomic(path, "old content that is longer"));
    REQUIRE(writeFileAtomic(path, "new"));
    std::string got;
    REQUIRE(readFile(path, got));
    CHECK(got == "new");
    CHECK_FALSE(fileExists(path + ".tmp"));
}

TEST_CASE("listDir: folders first, alphabetical, hidden and .tmp files left out") {
    std::string dir = testutil::scratch("storage-list");
    makeDirs(dir + "/zeta");
    makeDirs(dir + "/alpha");
    writeFileAtomic(dir + "/b.txt", "1");
    writeFileAtomic(dir + "/a.txt", "22");
    writeFileAtomic(dir + "/.hidden", "x");
    writeFileAtomic(dir + "/half.tmp", "x");
    auto e = listDir(dir);
    REQUIRE(e.size() == 4);
    CHECK(e[0].name == "alpha");
    CHECK(e[0].isDir);
    CHECK(e[1].name == "zeta");
    CHECK(e[2].name == "a.txt");
    CHECK(e[2].size == 2);
    CHECK(e[3].name == "b.txt");
}

TEST_CASE("readFile respects its size limit") {
    std::string path = testutil::scratch("storage-limit") + "/big.txt";
    writeFileAtomic(path, std::string(10000, 'x'));
    std::string got;
    REQUIRE(readFile(path, got, 100));
    CHECK(got.size() == 100);
    CHECK_FALSE(readFile(path + ".missing", got));
}
