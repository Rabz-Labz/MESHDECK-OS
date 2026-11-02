// fake_radio.cpp - see fake_radio.h.
#include "fake_radio.h"
#include <string.h>
#include <initializer_list>
#include "formats/protobuf.h"

using meshdeck::pb::Field;
using meshdeck::pb::Reader;
using meshdeck::pb::Writer;

// Frames a FromRadio message, with a line of log text in front as a real radio prints.
void FakeRadio::emit(const std::string& fromRadio) {
    out_ += "[radio log] tx\r\n";
    out_ += (char)0x94;
    out_ += (char)0xC3;
    out_ += (char)(fromRadio.size() >> 8);
    out_ += (char)(fromRadio.size() & 0xFF);
    out_ += fromRadio;
}

int FakeRadio::read(uint8_t* buf, int max) {
    if (!present || out_.empty()) return 0;
    int n = (int)out_.size() < max ? (int)out_.size() : max;
    memcpy(buf, out_.data(), n);
    out_.erase(0, n);
    return n;
}

bool FakeRadio::write(const uint8_t* data, int len) {
    if (!present) return false;
    in_.append((const char*)data, len);
    while (in_.size() >= 4) {
        if ((uint8_t)in_[0] != 0x94 || (uint8_t)in_[1] != 0xC3) { in_.erase(0, 1); continue; }
        size_t n = (uint8_t)in_[2] << 8 | (uint8_t)in_[3];
        if (in_.size() < 4 + n) break;
        handleToRadio(in_.substr(4, n), now_);
        in_.erase(0, 4 + n);
    }
    return true;
}

std::string FakeRadio::textPacket(uint32_t from, uint32_t to, int channel, const std::string& text) {
    Writer data, packet, fr;
    data.uint(1, 1);          // port: text
    data.bytes(2, text);
    packet.fixed32(1, from);
    packet.fixed32(2, to);
    packet.uint(3, channel);
    packet.message(4, data);
    packet.fixed32(6, ++packetId_);
    packet.floatv(8, 6.5f);   // SNR
    packet.uint(9, 2);        // hop limit left
    packet.uint(15, 3);       // hop start: so 1 hop away
    fr.message(2, packet);
    return fr.buf;
}

void FakeRadio::receiveText(const std::string& text, uint32_t to, int channel) {
    emit(textPacket(pocketNum, to, to == 0xFFFFFFFF ? channel : 0, text));
}

void FakeRadio::handleToRadio(const std::string& msg, uint32_t nowMs) {
    Reader r(msg);
    Field f;
    while (r.next(f)) {
        if (f.number == 3) {   // want_config_id: send our whole configuration, then the id back
            uint32_t id = (uint32_t)f.varint;
            Writer my, fr;
            my.uint(1, selfNum);
            fr.message(3, my);
            emit(fr.buf);
            struct N { uint32_t num; const char* s; const char* l; bool pos; };
            for (const N& n : {N{selfNum, "TD01", "TEST DECK", false}, N{pocketNum, "TS01", "TEST HANDHELD", true},
                               N{relayNum, "TR01", "TEST RELAY", false}}) {
                Writer user, ni, pos, out;
                user.bytes(2, n.l);
                user.bytes(3, n.s);
                ni.uint(1, n.num);
                ni.message(2, user);
                if (n.pos) {
                    pos.fixed32(1, (uint32_t)(int32_t)510000000);   // 51.0 N, 1.0 W: a generic test point
                    pos.fixed32(2, (uint32_t)(int32_t)-10000000);
                    ni.message(3, pos);
                }
                ni.floatv(4, 5.0f);
                ni.uint(9, n.num == relayNum ? 1 : 0);
                out.message(4, ni);
                emit(out.buf);
            }
            Writer lora, cfg, out;
            lora.boolean(1, true);   // use_preset
            lora.uint(7, 3);         // region EU_868
            lora.uint(8, 3);         // hop_limit
            lora.boolean(9, true);   // tx_enabled
            cfg.message(6, lora);
            out.message(5, cfg);
            emit(out.buf);
            for (int i = 0; i < 2; i++) {
                Writer settings, ch, o;
                if (i == 1) settings.bytes(3, "FIELD-A");
                ch.uint(1, i);
                ch.message(2, settings);
                ch.uint(3, i == 0 ? 1 : 2);
                o.message(10, ch);
                emit(o.buf);
            }
            Writer done;
            done.uint(7, id);
            emit(done.buf);
            configured_ = true;
        } else if (f.number == 1) {   // a packet to send: queue it, then acknowledge it later
            Reader p(f);
            Field g;
            uint32_t to = 0xFFFFFFFF, id = 0, port = 0;
            std::string text;
            while (p.next(g)) {
                if (g.number == 2) to = g.fixed32;
                else if (g.number == 6) id = g.fixed32;
                else if (g.number == 4) {
                    Reader d(g);
                    Field h;
                    while (d.next(h)) {
                        if (h.number == 1) port = (uint32_t)h.varint;
                        else if (h.number == 2) text = h.asString();
                    }
                }
            }
            if (port != 1) return;
            sentTexts.push_back(text);
            Writer q, qo;   // QueueStatus: accepted
            q.uint(2, 15);
            q.uint(3, 16);
            q.uint(4, id);
            qo.message(11, q);
            emit(qo.buf);
            Writer routing, data, packet, out;
            routing.uint(3, text.find("FAILTEST") != std::string::npos ? 5 : 0);
            data.uint(1, 5);         // port: routing
            data.bytes(2, routing.buf);
            data.fixed32(6, id);     // request_id: which message this answers
            packet.fixed32(1, to == 0xFFFFFFFF ? selfNum : to);
            packet.fixed32(2, selfNum);
            packet.message(4, data);
            packet.fixed32(6, ++packetId_);
            out.message(2, packet);
            pending_.push_back({nowMs + 1200, out.buf});
        }
    }
}

void FakeRadio::tick(uint32_t nowMs) {
    now_ = nowMs;
    if (!present) return;
    for (size_t i = 0; i < pending_.size();) {
        if ((int32_t)(nowMs - pending_[i].atMs) >= 0) {
            emit(pending_[i].fromRadio);
            pending_.erase(pending_.begin() + i);
        } else {
            i++;
        }
    }
    if (!configured_ || !chatter) return;
    if (nowMs >= nextPosition_) {
        Writer pos, data, packet, out;
        pos.fixed32(1, (uint32_t)(int32_t)510010000);   // the same generic point, moved a little
        pos.fixed32(2, (uint32_t)(int32_t)-10010000);
        data.uint(1, 3);
        data.bytes(2, pos.buf);
        packet.fixed32(1, pocketNum);
        packet.fixed32(2, 0xFFFFFFFF);
        packet.message(4, data);
        packet.fixed32(6, ++packetId_);
        out.message(2, packet);
        emit(out.buf);
        nextPosition_ = nowMs + 30000;
    }
    if (nowMs >= nextChat_) {
        emit(textPacket(pocketNum, 0xFFFFFFFF, 1, "CHECK-IN: ALL WELL HERE"));
        nextChat_ = nowMs + 25000;
    }
}
