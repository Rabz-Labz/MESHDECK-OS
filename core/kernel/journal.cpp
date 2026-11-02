// journal.cpp - see journal.h for the record layout and the power-loss guarantee.
#include "journal.h"
#include <string.h>
#include <vector>
#include "storage.h"

namespace meshdeck {

// Little-endian field helpers (the file format is the same on every CPU).
static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

uint32_t Journal::crc32(const uint8_t* data, size_t len, uint32_t crc) {
    // CRC-32 with the reflected polynomial 0xEDB88320, one byte at a time through a 256-entry table
    // (built on first use). Screenshots run a few megabytes through this, so the table matters.
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
            table[n] = c;
        }
        built = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool Journal::open(const std::string& path) {
    close();
    path_ = path;
    // "r+b" keeps the existing content; "w+b" creates a new empty file.
    file_ = fopen(path.c_str(), "r+b");
    if (!file_) file_ = fopen(path.c_str(), "w+b");
    if (!file_) return false;

    // Scan from the start until a record fails its checks; that point is the end of good data.
    fseek(file_, 0, SEEK_END);
    long size = ftell(file_);
    validEnd_ = 0;
    records_ = 0;
    nextSeq_ = 1;
    uint32_t next = 0;
    Record r;
    while (validEnd_ < (uint32_t)size && readRecord(validEnd_, &r, &next)) {
        validEnd_ = next;
        records_++;
        nextSeq_ = r.seq + 1;
    }
    discarded_ = (uint32_t)size - validEnd_;
    return true;
}

void Journal::close() {
    if (file_) fclose(file_);
    file_ = nullptr;
}

bool Journal::readRecord(uint32_t offset, Record* out, uint32_t* next) {
    // Step 1: read and sanity-check the header.
    uint8_t h[kHeader];
    if (fseek(file_, (long)offset, SEEK_SET) != 0 || fread(h, 1, kHeader, file_) != kHeader) return false;
    uint16_t len = get16(h + 4);
    if (get16(h) != kMagic || len > kMaxPayload) return false;
    // Step 2: read the text and check the CRC over header + text.
    std::vector<uint8_t> payload(len);
    if (len && fread(payload.data(), 1, len, file_) != len) return false;
    uint32_t crc = crc32(payload.data(), len, crc32(h, 24));
    if (crc != get32(h + 24)) return false;
    // Step 3: unpack the fields.
    out->type = h[2];
    out->seq = get32(h + 8);
    out->utc = (int64_t)((uint64_t)get32(h + 12) | ((uint64_t)get32(h + 16) << 32));
    out->uptimeMs = get32(h + 20);
    out->text.assign((const char*)payload.data(), len);
    *next = offset + (uint32_t)kHeader + len;
    return true;
}

bool Journal::append(Type type, const std::string& text, int64_t utc, uint32_t uptimeMs) {
    if (!file_) return false;
    // Step 1: build the header (see journal.h) and its CRC.
    uint16_t len = (uint16_t)(text.size() > kMaxPayload ? kMaxPayload : text.size());
    uint8_t h[kHeader] = {0};
    put16(h, kMagic);
    h[2] = type;
    put16(h + 4, len);
    put32(h + 8, nextSeq_);
    put32(h + 12, (uint32_t)((uint64_t)utc & 0xFFFFFFFFu));
    put32(h + 16, (uint32_t)((uint64_t)utc >> 32));
    put32(h + 20, uptimeMs);
    put32(h + 24, crc32((const uint8_t*)text.data(), len, crc32(h, 24)));

    // Step 2: write it at the end of good data (overwriting any damaged tail), then flush and
    // sync so it is on the card before we report success.
    if (fseek(file_, (long)validEnd_, SEEK_SET) != 0) return false;
    bool ok = fwrite(h, 1, kHeader, file_) == kHeader && (len == 0 || fwrite(text.data(), 1, len, file_) == len) &&
              fflush(file_) == 0 && syncFile(file_);
    if (!ok) return false;
    validEnd_ += (uint32_t)kHeader + len;
    records_++;
    nextSeq_++;
    return true;
}

} // namespace meshdeck
