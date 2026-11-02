// zip_archive.cpp - see zip_archive.h. The layout used: the end-of-central-directory record at the
// end of the file points to the central directory (one record per file), and each record points to
// the file's local header and its data.
#include "formats/zip_archive.h"
#include <string.h>
#include "lvgl.h"
#define LODEPNG_NO_COMPILE_CPP   // only the C API (its C++ wrappers clash inside LVGL's extern "C")
#include "src/libs/lodepng/lodepng.h"

namespace meshdeck {
namespace {

const uint32_t kEndSig = 0x06054b50, kCentralSig = 0x02014b50, kLocalSig = 0x04034b50;
const size_t kEndRecord = 22, kMaxComment = 65535;   // the end record is 22 bytes plus a comment of up to 64 KB
const uint32_t kMaxDirectory = 1024 * 1024;          // a central directory bigger than this isn't an EPUB

uint16_t u16(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return v; }
uint32_t u32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

} // namespace

bool ZipArchive::open(const std::string& path, std::string& error) {
    close();
    file_ = fopen(path.c_str(), "rb");
    if (!file_) { error = "CAN'T OPEN THE FILE"; return false; }

    // Step 1: find the end record. It sits at the very end, after an optional comment, so search the
    // last 22 + 64 KB bytes for its signature.
    fseek(file_, 0, SEEK_END);
    long size = ftell(file_);
    size_t tailLen = size < (long)(kEndRecord + kMaxComment) ? (size_t)size : kEndRecord + kMaxComment;
    std::vector<uint8_t> tail(tailLen);
    if (tailLen < kEndRecord || fseek(file_, size - (long)tailLen, SEEK_SET) != 0 ||
        fread(tail.data(), 1, tailLen, file_) != tailLen) {
        error = "NOT A ZIP ARCHIVE (TOO SMALL)";
        close();
        return false;
    }
    size_t end = tailLen;
    for (size_t i = tailLen - kEndRecord + 1; i-- > 0;) {
        if (u32(&tail[i]) == kEndSig) { end = i; break; }
    }
    if (end == tailLen) { error = "NOT A ZIP ARCHIVE"; close(); return false; }

    // Step 2: the end record says how many files there are and where the central directory starts.
    uint16_t count = u16(&tail[end + 10]);
    uint32_t dirSize = u32(&tail[end + 12]), dirOffset = u32(&tail[end + 16]);
    if (count == 0xFFFF || dirOffset == 0xFFFFFFFFu) { error = "ZIP64 ARCHIVES ARE NOT SUPPORTED"; close(); return false; }
    if (dirSize > kMaxDirectory || (long)dirOffset + (long)dirSize > size) { error = "DAMAGED ARCHIVE (DIRECTORY)"; close(); return false; }

    // Step 3: read the central directory and list the files.
    std::vector<uint8_t> dir(dirSize);
    if (fseek(file_, (long)dirOffset, SEEK_SET) != 0 || (dirSize && fread(dir.data(), 1, dirSize, file_) != dirSize)) {
        error = "DAMAGED ARCHIVE (DIRECTORY)";
        close();
        return false;
    }
    size_t at = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (at + 46 > dir.size() || u32(&dir[at]) != kCentralSig) { error = "DAMAGED ARCHIVE (LIST)"; close(); return false; }
        Entry e;
        e.method = u16(&dir[at + 10]);
        e.compSize = u32(&dir[at + 20]);
        e.size = u32(&dir[at + 24]);
        uint16_t nameLen = u16(&dir[at + 28]), extraLen = u16(&dir[at + 30]), commentLen = u16(&dir[at + 32]);
        e.localOffset = u32(&dir[at + 42]);
        if (at + 46 + nameLen > dir.size()) { error = "DAMAGED ARCHIVE (LIST)"; close(); return false; }
        e.name.assign((const char*)&dir[at + 46], nameLen);
        entries_.push_back(e);
        at += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

void ZipArchive::close() {
    if (file_) fclose(file_);
    file_ = nullptr;
    entries_.clear();
}

const ZipArchive::Entry* ZipArchive::find(const std::string& name) const {
    for (const Entry& e : entries_) if (e.name == name) return &e;
    return nullptr;
}

bool ZipArchive::read(const Entry& e, std::string& out, size_t maxBytes, std::string& error) {
    out.clear();
    if (!file_) { error = "NO ARCHIVE OPEN"; return false; }
    if (e.size > maxBytes) { error = "FILE INSIDE THE ARCHIVE IS TOO BIG"; return false; }
    if (e.method != 0 && e.method != 8) { error = "UNSUPPORTED COMPRESSION IN THE ARCHIVE"; return false; }

    // Step 1: the local header repeats the name and has its own extra field, so the data starts after both.
    uint8_t local[30];
    if (fseek(file_, (long)e.localOffset, SEEK_SET) != 0 || fread(local, 1, 30, file_) != 30 || u32(local) != kLocalSig) {
        error = "DAMAGED ARCHIVE (FILE HEADER)";
        return false;
    }
    long dataAt = (long)e.localOffset + 30 + u16(local + 26) + u16(local + 28);

    // Step 2: read the packed bytes.
    std::vector<uint8_t> packed(e.compSize);
    if (fseek(file_, dataAt, SEEK_SET) != 0 || (e.compSize && fread(packed.data(), 1, e.compSize, file_) != e.compSize)) {
        error = "DAMAGED ARCHIVE (FILE DATA)";
        return false;
    }

    // Step 3: stored files are copied as they are; deflated files are inflated to their known size.
    if (e.method == 0) {
        if (e.compSize != e.size) { error = "DAMAGED ARCHIVE (SIZE)"; return false; }
        out.assign((const char*)packed.data(), packed.size());
        return true;
    }
    unsigned char* buf = nullptr;
    size_t bufSize = 0;
    unsigned err = lodepng_inflate(&buf, &bufSize, packed.data(), packed.size(), &lodepng_default_decompress_settings);
    if (err || !buf || bufSize != e.size) {
        if (buf) lv_free(buf);
        error = "DAMAGED ARCHIVE (INFLATE " + std::to_string(err) + ")";
        return false;
    }
    out.assign((const char*)buf, bufSize);
    lv_free(buf);
    return true;
}

} // namespace meshdeck
