// journal.h - an append-only log file that survives power loss.
//
// Logs and messages are journals. Each record is a fixed 28-byte
// header followed by its text, protected by a CRC-32 over both:
//
//   offset  size  field
//        0     2  magic 0x4A42 ("BJ")
//        2     1  type (Type below)
//        3     1  flags (0, reserved)
//        4     2  text length (at most kMaxPayload)
//        6     2  reserved (0)
//        8     4  sequence number (1, 2, 3...)
//       12     8  UTC seconds when written (0 = clock not set)
//       20     4  uptime in milliseconds when written
//       24     4  CRC-32 of bytes 0-23 followed by the text
//
// All numbers are little-endian. On open the whole file is scanned: the first record that is
// damaged or incomplete marks the end of good data, and new records are written from there. So a
// power cut while writing loses at most the record being written. Every append is flushed and
// synced to the card before it returns. Full format notes: docs/formats/journal.md.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>

namespace meshdeck {

class Journal {
public:
    enum Type : uint8_t { Boot = 1, Settings = 2, ClockSet = 3, Test = 4, Note = 5 };

    struct Record {
        uint32_t seq;
        uint8_t type;
        int64_t utc;        // 0 when the clock was not set
        uint32_t uptimeMs;
        std::string text;
    };

    static const size_t kHeader = 28;
    static const uint16_t kMagic = 0x4A42;
    static const uint16_t kMaxPayload = 4000;

    ~Journal() { close(); }

    // Opens (creating if needed) and scans the file. False only if it can't be opened at all.
    bool open(const std::string& path);
    void close();
    bool isOpen() const { return file_ != nullptr; }

    // Adds a record (text longer than kMaxPayload is cut). True once it is safely on the card.
    bool append(Type type, const std::string& text, int64_t utc, uint32_t uptimeMs);

    // Calls fn(record) for each good record, oldest first. Returns how many were visited.
    template <typename Fn> uint32_t forEach(Fn fn);

    uint32_t records() const { return records_; }
    uint32_t bytes() const { return validEnd_; }
    uint32_t discardedBytes() const { return discarded_; }   // damaged tail found when opened
    const std::string& path() const { return path_; }

    // CRC-32 (IEEE 802.3, the same as zip and PNG). Pass a previous result as crc to continue.
    static uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0);

private:
    bool readRecord(uint32_t offset, Record* out, uint32_t* next);

    FILE* file_ = nullptr;
    std::string path_;
    uint32_t validEnd_ = 0, discarded_ = 0, records_ = 0, nextSeq_ = 1;
};

template <typename Fn> uint32_t Journal::forEach(Fn fn) {
    uint32_t offset = 0, next = 0, n = 0;
    Record r;
    while (offset < validEnd_ && readRecord(offset, &r, &next)) {
        fn(r);
        n++;
        offset = next;
    }
    return n;
}

} // namespace meshdeck
