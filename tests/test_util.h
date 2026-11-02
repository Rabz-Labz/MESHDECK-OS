// test_util.h - helpers shared by the tests: a fresh scratch folder per test case.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "kernel/storage.h"

namespace testutil {

// Deletes every file below dir, in all sub-folders (the empty folders stay).
inline void removeFiles(const std::string& dir) {
    for (const meshdeck::DirEntry& e : meshdeck::listDir(dir)) {
        std::string path = dir + "/" + e.name;
        if (e.isDir) removeFiles(path);
        else remove(path.c_str());
    }
}

// A new empty folder under the system temp folder, named after the test, e.g.
// /tmp/meshdeck-tests/journal-torn-tail. Files left from an earlier run are removed first.
inline std::string scratch(const std::string& name) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMPDIR");
    std::string dir = std::string(tmp ? tmp : "/tmp") + "/meshdeck-tests/" + name;
    meshdeck::makeDirs(dir);
    removeFiles(dir);
    return dir;
}

} // namespace testutil
