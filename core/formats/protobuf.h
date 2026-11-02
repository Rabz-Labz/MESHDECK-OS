// protobuf.h - a minimal protobuf wire-format writer and reader: just what the Meshtastic serial
// API needs (varints, fixed32, length-delimited bytes and nested messages). Ported from 1.x.
// Wire format: https://protobuf.dev/programming-guides/encoding/
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>

namespace meshdeck {
namespace pb {

enum Wire { Varint = 0, Fixed64 = 1, Bytes = 2, Fixed32 = 5 };

class Writer {
public:
    std::string buf;
    void varint(uint64_t v) {
        do { uint8_t b = v & 0x7F; v >>= 7; buf += (char)(b | (v ? 0x80 : 0)); } while (v);
    }
    void tag(int field, Wire wire) { varint((uint64_t)field << 3 | wire); }
    void uint(int field, uint64_t v) { if (v) { tag(field, Varint); varint(v); } }   // proto3: zero omitted
    void sint32(int field, int32_t v) { if (v) { tag(field, Varint); varint((uint64_t)(int64_t)v); } }
    void boolean(int field, bool v) { if (v) { tag(field, Varint); varint(1); } }
    void fixed32(int field, uint32_t v) {
        tag(field, Fixed32);
        for (int i = 0; i < 4; i++) buf += (char)(v >> (8 * i));
    }
    void floatv(int field, float f) { uint32_t v; memcpy(&v, &f, 4); fixed32(field, v); }
    void bytes(int field, const std::string& s) { tag(field, Bytes); varint(s.size()); buf += s; }
    void message(int field, const Writer& w) { bytes(field, w.buf); }
    void emptyMessage(int field) { tag(field, Bytes); varint(0); }
};

struct Field {
    int number = 0;
    Wire wire = Varint;
    uint64_t varint = 0;     // Varint
    uint32_t fixed32 = 0;    // Fixed32
    const uint8_t* data = nullptr;
    size_t len = 0;          // Bytes
    int32_t asInt32() const { return (int32_t)(int64_t)varint; }
    float asFloat() const { float f; memcpy(&f, &fixed32, 4); return f; }
    std::string asString() const { return std::string((const char*)data, len); }
};

class Reader {
public:
    Reader(const uint8_t* data, size_t len) : p_(data), end_(data + len) {}
    explicit Reader(const Field& f) : p_(f.data), end_(f.data + f.len) {}
    explicit Reader(const std::string& s) : p_((const uint8_t*)s.data()), end_((const uint8_t*)s.data() + s.size()) {}

    // Returns false at the end of the message or on malformed input.
    bool next(Field& f) {
        if (p_ >= end_) return false;
        uint64_t key;
        if (!readVarint(key)) return false;
        f.number = (int)(key >> 3);
        f.wire = (Wire)(key & 7);
        switch (f.wire) {
        case Varint: return readVarint(f.varint);
        case Fixed32:
            if (end_ - p_ < 4) return false;
            f.fixed32 = p_[0] | p_[1] << 8 | p_[2] << 16 | (uint32_t)p_[3] << 24;
            p_ += 4;
            return true;
        case Fixed64:
            if (end_ - p_ < 8) return false;
            p_ += 8;
            return true;
        case Bytes: {
            uint64_t n;
            if (!readVarint(n) || (uint64_t)(end_ - p_) < n) return false;
            f.data = p_;
            f.len = (size_t)n;
            p_ += n;
            return true;
        }
        default: return false;
        }
    }

private:
    bool readVarint(uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 64 && p_ < end_; shift += 7) {
            uint8_t b = *p_++;
            v |= (uint64_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    const uint8_t* p_;
    const uint8_t* end_;
};

} // namespace pb
} // namespace meshdeck
