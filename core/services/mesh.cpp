// mesh.cpp - see mesh.h. Protocol logic ported from MESHDECK 1.x, where it was tested against
// Meshtastic 2.7 firmware on a Unit C6L and a Cardputer.
#include "services/mesh.h"
#include <stdio.h>
#include <algorithm>
#include "formats/protobuf.h"

namespace meshdeck {

// Meshtastic protobuf field numbers (meshtastic/protobufs: mesh.proto, config.proto,
// channel.proto, portnums.proto). Field numbers never change once published.
namespace mt {
enum ToRadio { TR_Packet = 1, TR_WantConfigId = 3, TR_Heartbeat = 7 };
enum FromRadio {
    FR_Packet = 2, FR_MyInfo = 3, FR_NodeInfo = 4, FR_Config = 5, FR_ConfigCompleteId = 7, FR_Rebooted = 8,
    FR_Channel = 10, FR_QueueStatus = 11,
};
enum MeshPacket { MP_From = 1, MP_To = 2, MP_Channel = 3, MP_Decoded = 4, MP_Encrypted = 5, MP_Id = 6, MP_RxSnr = 8,
                  MP_HopLimit = 9, MP_WantAck = 10, MP_HopStart = 15 };
enum Data { D_Portnum = 1, D_Payload = 2, D_RequestId = 6 };
enum Port { PortText = 1, PortPosition = 3, PortNodeInfo = 4, PortRouting = 5, PortAdmin = 6 };
enum NodeInfo { NI_Num = 1, NI_User = 2, NI_Position = 3, NI_Snr = 4, NI_LastHeard = 5, NI_HopsAway = 9 };
enum User { U_LongName = 2, U_ShortName = 3 };
enum Position { P_Lat = 1, P_Lon = 2 };
enum Channel { C_Index = 1, C_Settings = 2, C_Role = 3 };
enum ChannelSettings { CS_Name = 3 };
enum Config { CF_Lora = 6 };
enum Lora { L_Region = 7, L_HopLimit = 8, L_TxEnabled = 9 };
enum Admin { A_SetConfig = 34 };
enum Routing { R_ErrorReason = 3 };
enum QueueStatus { Q_Res = 1, Q_PacketId = 4 };
} // namespace mt

static const char* regionName(int r) {
    static const char* names[] = {"UNSET", "US", "EU_433", "EU_868", "CN", "JP", "ANZ", "KR", "TW", "RU", "IN",
                                  "NZ_865", "TH", "LORA_24", "UA_433", "UA_868", "MY_433", "MY_919", "SG_923"};
    return r >= 0 && r < (int)(sizeof names / sizeof names[0]) ? names[r] : "OTHER";
}

// Routing.Error values, in words.
static const char* routingError(int e) {
    switch (e) {
    case 1: return "NO ROUTE";
    case 2: return "REFUSED (NAK)";
    case 3: return "TIMED OUT";
    case 4: return "NO INTERFACE";
    case 5: return "NO ACK AFTER RETRIES";
    case 6: return "NO SUCH CHANNEL";
    case 7: return "TOO LARGE";
    case 8: return "NO RESPONSE";
    case 9: return "DUTY-CYCLE LIMIT REACHED";
    case 32: return "BAD REQUEST";
    case 33: return "NOT AUTHORISED";
    case 34: return "ENCRYPTION FAILED";
    case 35: return "NO KEY FOR THAT NODE YET";
    case 38: return "RATE LIMITED";
    default: return "RADIO ERROR";
    }
}

const char* MeshService::stateName(State s) {
    switch (s) {
    case State::Queued: return "QUEUED";
    case State::Sent: return "SENT";
    case State::Delivered: return "DELIVERED";
    case State::Failed: return "FAILED";
    case State::Received: return "RECEIVED";
    }
    return "";
}

std::string MeshService::Thread::key() const {
    char buf[16];
    if (direct) snprintf(buf, sizeof buf, "D%x", (unsigned)node);
    else snprintf(buf, sizeof buf, "C%d", channel);
    return buf;
}

MeshService::MeshService(Platform& platform, std::function<int64_t()> clock)
    : platform_(platform), clock_(std::move(clock)) {}

const char* MeshService::linkText() const {
    if (emcon_ == Emcon::RadioOff) return "RADIO OFF (EMCON)";
    switch (link_) {
    case Link::None: return "NO RADIO";
    case Link::Connecting: return "RADIO CONNECTING";
    case Link::Ready: return "RADIO READY";
    }
    return "";
}

std::string MeshService::nodeName(uint32_t num) const {
    auto it = nodes_.find(num);
    if (it != nodes_.end() && !it->second.shortName.empty()) return it->second.shortName;
    if (num == myNum_ && myNum_) return "YOU";
    char buf[16];
    snprintf(buf, sizeof buf, "!%08x", (unsigned)num);
    return buf;
}

std::string MeshService::channelName(int index) const {
    for (const Channel& c : channels_)
        if (c.index == index && !c.name.empty()) return c.name;
    return index == 0 ? "PRIMARY" : "CHANNEL " + std::to_string(index);
}

MeshService::Thread MeshService::threadOf(const Message& m) const {
    Thread t;
    if (m.to == kBroadcast) {
        t.channel = m.channel;
    } else {
        t.direct = true;
        t.node = m.outgoing ? m.to : m.from;
    }
    return t;
}

uint32_t MeshService::random() {
    do {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
    } while (!rng_);
    return rng_;
}

// ---------------------------------------------------------------------------------------------
// History

// Journal records (text), oldest first:
//   OUT|id|to|channel|text           a message we sent
//   IN|id|from|to|channel|snr|text   a message we received
//   ST|id|SNT/DLV/FAI|note           a sent message changed state
//   DEL|threadkey                    a thread was deleted (hides everything before it)
void MeshService::begin(const std::string& journalPath) {
    uint8_t seed[4];
    platform_.randomBytes(seed, 4);
    rng_ ^= seed[0] | seed[1] << 8 | seed[2] << 16 | (uint32_t)seed[3] << 24;
    if (journalPath.empty() || !journal_.open(journalPath)) return;

    journal_.forEach([&](const Journal::Record& r) {
        const std::string& t = r.text;
        unsigned id = 0, a = 0, b = 0;
        int ch = 0, used = 0;
        float snr = 0;
        char code[4] = {0};
        if (t.compare(0, 4, "OUT|") == 0 && sscanf(t.c_str(), "OUT|%u|%u|%d|%n", &id, &a, &ch, &used) == 3 && used) {
            Message m;
            m.id = id;
            m.to = a;
            m.channel = ch;
            m.text = t.substr(used);
            m.outgoing = true;
            m.utc = r.utc;
            add(m);
        } else if (t.compare(0, 3, "IN|") == 0 &&
                   sscanf(t.c_str(), "IN|%u|%u|%u|%d|%f|%n", &id, &a, &b, &ch, &snr, &used) == 5 && used) {
            Message m;
            m.id = id;
            m.from = a;
            m.to = b;
            m.channel = ch;
            m.snr = snr;
            m.text = t.substr(used);
            m.utc = r.utc;
            m.state = State::Received;
            add(m);
        } else if (t.compare(0, 3, "ST|") == 0 && sscanf(t.c_str(), "ST|%u|%3s|%n", &id, code, &used) == 2) {
            if (Message* m = findOutgoing(id)) {
                std::string k = code;
                m->state = k == "SNT" ? State::Sent : k == "DLV" ? State::Delivered : k == "FAI" ? State::Failed : m->state;
                m->note = used ? t.substr(used) : "";
            }
        } else if (t.compare(0, 4, "DEL|") == 0) {
            std::string key = t.substr(4);
            messages_.erase(std::remove_if(messages_.begin(), messages_.end(),
                                           [&](const Message& m) { return threadOf(m).key() == key; }),
                            messages_.end());
        }
    });
    // A message that was SENT when the deck switched off can't be followed any more.
    for (Message& m : messages_)
        if (m.outgoing && m.state == State::Sent) m.note = "ACK NOT TRACKED AFTER RESTART";
    // Queued ones from before the restart are sent again when the radio is ready.
    touch();
}

void MeshService::add(Message m) {
    m.seq = nextSeq_++;
    messages_.push_back(m);
    while (messages_.size() > kKeepMessages) messages_.pop_front();
}

void MeshService::persist(const std::string& record) {
    if (journal_.isOpen()) journal_.append(Journal::Note, record, clock_(), platform_.uptimeMs());
}

MeshService::Message* MeshService::findOutgoing(uint32_t id) {
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it)
        if (it->outgoing && it->id == id) return &*it;
    return nullptr;
}

void MeshService::setState(Message& m, State s, const std::string& note) {
    m.state = s;
    m.note = note;
    const char* code = s == State::Sent ? "SNT" : s == State::Delivered ? "DLV" : s == State::Failed ? "FAI" : "QUE";
    persist("ST|" + std::to_string(m.id) + "|" + code + "|" + note);
    touch();
}

int MeshService::deleteThread(const Thread& thread) {
    std::string key = thread.key();
    size_t before = messages_.size();
    messages_.erase(std::remove_if(messages_.begin(), messages_.end(),
                                   [&](const Message& m) { return threadOf(m).key() == key; }),
                    messages_.end());
    persist("DEL|" + key);
    touch();
    return (int)(before - messages_.size());
}

// ---------------------------------------------------------------------------------------------
// Sending

bool MeshService::send(const std::string& text, const Thread& thread, std::string& error) {
    size_t a = text.find_first_not_of(" \r\n\t"), b = text.find_last_not_of(" \r\n\t");
    if (a == std::string::npos) { error = "MESSAGE IS EMPTY"; return false; }
    std::string body = text.substr(a, b - a + 1);
    if (body.size() > kMaxText) { error = "TOO LONG (MAX 200 CHARACTERS)"; return false; }
    Message m;
    m.id = random();
    m.to = thread.direct ? thread.node : kBroadcast;
    m.channel = thread.direct ? 0 : thread.channel;
    m.text = body;
    m.outgoing = true;
    m.from = myNum_;
    m.utc = clock_();
    persist("OUT|" + std::to_string(m.id) + "|" + std::to_string(m.to) + "|" + std::to_string(m.channel) + "|" + body);
    add(m);
    touch();
    return true;   // tick() hands it to the radio when the link is ready
}

void MeshService::setEmcon(Emcon level) {
    if (level == emcon_) return;
    emcon_ = level;
    txSetMs_ = 0;   // a change to the radio's transmit switch may go out at once
    touch();
}

// Sends the radio an admin message that sets its LoRa tx_enabled, addressed to its own node, as the
// Meshtastic clients do. The Config.lora bytes are the radio's own with tx_enabled appended: a
// protobuf field given twice takes its last value, so the region and the other settings stay as they were.
bool MeshService::setRadioTx(bool on) {
    std::string lora = loraRaw_ + std::string(1, '\x48') + std::string(1, on ? '\x01' : '\x00');   // 0x48 = field 9, varint
    pb::Writer config, admin, data, packet, toRadio;
    config.bytes(mt::CF_Lora, lora);
    admin.bytes(mt::A_SetConfig, config.buf);
    data.uint(mt::D_Portnum, mt::PortAdmin);
    data.bytes(mt::D_Payload, admin.buf);
    packet.fixed32(mt::MP_To, myNum_);
    packet.message(mt::MP_Decoded, data);
    packet.fixed32(mt::MP_Id, random());
    packet.boolean(mt::MP_WantAck, true);
    toRadio.message(mt::TR_Packet, packet);
    return writeToRadio(toRadio.buf);
}

bool MeshService::writeToRadio(const std::string& toRadio) {
    if (toRadio.size() > 512) return false;
    std::string frame;
    frame += (char)0x94;
    frame += (char)0xC3;
    frame += (char)(toRadio.size() >> 8);
    frame += (char)(toRadio.size() & 0xFF);
    frame += toRadio;
    return platform_.radioWrite((const uint8_t*)frame.data(), (int)frame.size());
}

void MeshService::requestConfig() {
    configId_ = random();
    // Like the official clients, send 32 START2 bytes first to wake the radio's serial API.
    std::string wake(32, (char)0xC3);
    platform_.radioWrite((const uint8_t*)wake.data(), (int)wake.size());
    pb::Writer w;
    w.uint(mt::TR_WantConfigId, configId_);
    writeToRadio(w.buf);
    configSentMs_ = platform_.uptimeMs();
    if (link_ != Link::Connecting) {
        link_ = Link::Connecting;
        touch();
    }
}

void MeshService::transmit(Message& m) {
    pb::Writer data, packet, toRadio;
    data.uint(mt::D_Portnum, mt::PortText);
    data.bytes(mt::D_Payload, m.text);
    packet.fixed32(mt::MP_To, m.to);
    packet.uint(mt::MP_Channel, (uint32_t)m.channel);
    packet.message(mt::MP_Decoded, data);
    packet.fixed32(mt::MP_Id, m.id);
    // Without a hop limit the packet goes out as 0 hops: no node relays it, and a direct message to
    // anyone not in direct range fails with NO ACK AFTER RETRIES. The official clients set it too.
    packet.uint(mt::MP_HopLimit, hopLimit_);
    packet.boolean(mt::MP_WantAck, true);
    toRadio.message(mt::TR_Packet, packet);
    lastSendMs_ = platform_.uptimeMs();
    if (writeToRadio(toRadio.buf)) {
        m.sentMs = lastSendMs_;
        setState(m, State::Sent, "WAITING FOR ACK");
    }
}

// ---------------------------------------------------------------------------------------------
// Main loop

void MeshService::tick() {
    // RADIO OFF: MESHDECK neither reads nor writes the radio. Its transmitter is switched off once,
    // and the queued messages say why they wait. Leaving RADIO OFF asks the radio for its
    // configuration again, which brings the link back.
    if (emcon_ == Emcon::RadioOff) {
        if (loraTx_ == 1 && !loraRaw_.empty()) {
            setRadioTx(false);
            loraTx_ = 0;
            touch();
        }
        if (link_ != Link::None) {
            link_ = Link::None;
            rx_.clear();
            touch();
        }
        for (Message& m : messages_)
            if (m.outgoing && m.state == State::Queued && m.note.empty()) { m.note = "HELD: EMCON"; touch(); }
        return;
    }

    if (!platform_.radioPresent()) {
        if (link_ != Link::None) {
            link_ = Link::None;
            rx_.clear();
            touch();
        }
        return;
    }

    // Step 1: collect bytes and cut out whole frames as each chunk arrives. Anything between
    // frames is the radio's own log text and is skipped. Frames are parsed chunk by chunk (not
    // after reading everything) because the radio sends its whole configuration in one burst at
    // start-up, often over 16 KB, and the first frames (node number, region, channels) matter most.
    uint8_t buf[512];
    int n;
    while ((n = platform_.radioRead(buf, sizeof buf)) > 0) {
        rx_.append((const char*)buf, n);
        for (;;) {
            size_t start = rx_.find('\x94');
            if (start == std::string::npos) { rx_.clear(); break; }
            if (start) rx_.erase(0, start);
            if (rx_.size() < 4) break;
            if ((uint8_t)rx_[1] != 0xC3) { rx_.erase(0, 1); continue; }
            size_t len = (uint8_t)rx_[2] << 8 | (uint8_t)rx_[3];
            if (len > 512) { rx_.erase(0, 1); continue; }
            if (rx_.size() < 4 + len) break;
            handleFrame((const uint8_t*)rx_.data() + 4, len);
            stats_.frames++;
            rx_.erase(0, 4 + len);
        }
    }

    // Step 2: the link. Ask for the configuration, again every 10 s until it arrives (the radio
    // may still be starting up).
    uint32_t now = platform_.uptimeMs();
    if (link_ == Link::None) requestConfig();
    else if (link_ == Link::Connecting && now - configSentMs_ > 10000) requestConfig();
    if (link_ != Link::Ready) return;

    // Step 2b: the radio's transmit switch follows RX ONLY. The change goes to the radio as an admin
    // message, then its configuration is asked for again, which confirms the change. A change waits
    // 15 s before another is sent, as in 1.x.
    const bool wantTx = emcon_ != Emcon::RxOnly;
    if (loraTx_ >= 0 && (loraTx_ == 1) != wantTx && (!txSetMs_ || now - txSetMs_ > 15000)) {
        txSetMs_ = now;
        setRadioTx(wantTx);
        requestConfig();
        return;
    }

    // Step 3: a heartbeat every minute keeps the serial API awake.
    if (now - lastHeartbeatMs_ > 60000) {
        pb::Writer w;
        w.emptyMessage(mt::TR_Heartbeat);
        writeToRadio(w.buf);
        lastHeartbeatMs_ = now;
    }
    // Step 4: hand the oldest queued message to the radio, at most one every 1.5 s. Under RX ONLY
    // nothing is handed over; queued messages say why they wait.
    if (emcon_ == Emcon::RxOnly) {
        for (Message& m : messages_)
            if (m.outgoing && m.state == State::Queued && m.note.empty()) { m.note = "HELD: EMCON"; touch(); }
    } else if (!lastSendMs_ || now - lastSendMs_ > 1500) {
        for (Message& m : messages_)
            if (m.outgoing && m.state == State::Queued) { transmit(m); break; }
    }
    // Step 5: no acknowledgement within 2 minutes is a failure. (The clock is read again because
    // transmit() may just have stamped a later time; now - sentMs would otherwise wrap.)
    now = platform_.uptimeMs();
    for (Message& m : messages_)
        if (m.outgoing && m.state == State::Sent && m.sentMs && now - m.sentMs > 120000)
            setState(m, State::Failed, "NO ACK WITHIN 2 MINUTES");
}

void MeshService::handleFrame(const uint8_t* data, size_t len) {
    pb::Reader r(data, len);
    pb::Field f;
    while (r.next(f)) {
        switch (f.number) {
        case mt::FR_Packet: handlePacket(f.data, f.len); break;
        case mt::FR_MyInfo: {
            pb::Reader m(f);
            pb::Field g;
            while (m.next(g))
                if (g.number == 1) myNum_ = (uint32_t)g.varint;
            touch();
            break;
        }
        case mt::FR_NodeInfo: parseNodeInfo(f.data, f.len); break;
        case mt::FR_Config: {
            pb::Reader c(f);
            pb::Field g;
            while (c.next(g)) {
                if (g.number != mt::CF_Lora) continue;
                loraRaw_.assign((const char*)g.data, g.len);
                loraTx_ = 1;   // a radio that doesn't report tx_enabled is transmitting
                pb::Reader l(g);
                pb::Field h;
                while (l.next(h)) {
                    if (h.number == mt::L_Region) region_ = regionName((int)h.varint);
                    else if (h.number == mt::L_HopLimit) hopLimit_ = h.varint >= 1 && h.varint <= 7 ? (uint32_t)h.varint : 3;
                    else if (h.number == mt::L_TxEnabled) loraTx_ = h.varint ? 1 : 0;
                }
                if (region_.empty()) region_ = "UNSET";
            }
            touch();
            break;
        }
        case mt::FR_Channel: {
            pb::Reader c(f);
            pb::Field g;
            Channel ch;
            while (c.next(g)) {
                if (g.number == mt::C_Index) ch.index = g.asInt32();
                else if (g.number == mt::C_Role) ch.role = (int)g.varint;
                else if (g.number == mt::C_Settings) {
                    pb::Reader s(g);
                    pb::Field h;
                    while (s.next(h))
                        if (h.number == mt::CS_Name) ch.name = h.asString();
                }
            }
            channels_.erase(std::remove_if(channels_.begin(), channels_.end(),
                                           [&](const Channel& c2) { return c2.index == ch.index; }),
                            channels_.end());
            if (ch.role != 0) {   // role 0 = disabled slot
                channels_.push_back(ch);
                std::sort(channels_.begin(), channels_.end(),
                          [](const Channel& a, const Channel& b) { return a.index < b.index; });
            }
            touch();
            break;
        }
        case mt::FR_ConfigCompleteId:
            if ((uint32_t)f.varint == configId_) {
                link_ = Link::Ready;
                lastHeartbeatMs_ = platform_.uptimeMs();
                touch();
            }
            break;
        case mt::FR_Rebooted:
            link_ = Link::None;
            touch();
            break;
        case mt::FR_QueueStatus: {
            // The radio says whether it accepted a packet into its transmit queue.
            pb::Reader q(f);
            pb::Field g;
            int32_t res = 0;
            uint32_t id = 0;
            while (q.next(g)) {
                if (g.number == mt::Q_Res) res = g.asInt32();
                else if (g.number == mt::Q_PacketId) id = (uint32_t)g.varint;
            }
            if (Message* m = findOutgoing(id))
                if (res != 0 && m->state == State::Sent)
                    setState(*m, State::Failed, std::string("RADIO REFUSED IT: ") + routingError(res));
            break;
        }
        default: break;
        }
    }
}

void MeshService::parseUser(Node& node, const uint8_t* data, size_t len) {
    pb::Reader u(data, len);
    pb::Field g;
    while (u.next(g)) {
        if (g.number == mt::U_LongName) node.longName = g.asString();
        else if (g.number == mt::U_ShortName) node.shortName = g.asString();
    }
}

// Positions are degrees x 1e7 (sfixed32 in current firmware, varint in older).
void MeshService::parsePosition(Node& node, const uint8_t* data, size_t len) {
    pb::Reader p(data, len);
    pb::Field g;
    int32_t lat = 0, lon = 0;
    while (p.next(g)) {
        if (g.number == mt::P_Lat) lat = g.wire == pb::Fixed32 ? (int32_t)g.fixed32 : g.asInt32();
        else if (g.number == mt::P_Lon) lon = g.wire == pb::Fixed32 ? (int32_t)g.fixed32 : g.asInt32();
    }
    if (!lat && !lon) return;
    node.hasPosition = true;
    node.lat = lat * 1e-7;
    node.lon = lon * 1e-7;
}

void MeshService::parseNodeInfo(const uint8_t* data, size_t len) {
    pb::Reader r(data, len);
    pb::Field f;
    Node n;
    const uint8_t *user = nullptr, *pos = nullptr;
    size_t userLen = 0, posLen = 0;
    while (r.next(f)) {
        switch (f.number) {
        case mt::NI_Num: n.num = (uint32_t)f.varint; break;
        case mt::NI_User: user = f.data; userLen = f.len; break;
        case mt::NI_Position: pos = f.data; posLen = f.len; break;
        case mt::NI_Snr: n.snr = f.asFloat(); break;
        case mt::NI_LastHeard: n.lastHeardUtc = f.fixed32; break;
        case mt::NI_HopsAway: n.hops = (int)f.varint; break;
        }
    }
    if (!n.num) return;
    Node& node = nodes_[n.num];
    node.num = n.num;
    node.snr = n.snr;
    node.lastHeardUtc = n.lastHeardUtc;
    if (n.hops >= 0) node.hops = n.hops;
    if (user) parseUser(node, user, userLen);
    if (pos) parsePosition(node, pos, posLen);
    touch();
}

void MeshService::handlePacket(const uint8_t* data, size_t len) {
    pb::Reader r(data, len);
    pb::Field f;
    uint32_t from = 0, to = kBroadcast, id = 0, requestId = 0, port = 0;
    int channel = 0, hopLimit = -1, hopStart = -1;
    float snr = 0;
    bool encrypted = false;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    while (r.next(f)) {
        switch (f.number) {
        case mt::MP_Encrypted: encrypted = true; break;   // the radio couldn't decrypt it
        case mt::MP_From: from = f.fixed32; break;
        case mt::MP_To: to = f.fixed32; break;
        case mt::MP_Channel: channel = (int)f.varint; break;
        case mt::MP_Id: id = f.fixed32; break;
        case mt::MP_RxSnr: snr = f.asFloat(); break;
        case mt::MP_HopLimit: hopLimit = (int)f.varint; break;
        case mt::MP_HopStart: hopStart = (int)f.varint; break;
        case mt::MP_Decoded: {
            pb::Reader d(f);
            pb::Field g;
            while (d.next(g)) {
                if (g.number == mt::D_Portnum) port = (uint32_t)g.varint;
                else if (g.number == mt::D_Payload) { payload = g.data; payloadLen = g.len; }
                else if (g.number == mt::D_RequestId) requestId = g.fixed32;
            }
            break;
        }
        }
    }
    stats_.packets++;
    if (encrypted) stats_.encrypted++;
    if (port == mt::PortText) stats_.texts++;
    stats_.lastFrom = from;
    stats_.lastChannel = channel;
    stats_.lastPort = port;
    stats_.lastEncrypted = encrypted;

    // Anything heard from another node updates when we last heard it, and how well.
    if (from && from != myNum_) {
        Node& node = nodes_[from];
        node.num = from;
        node.heardMs = platform_.uptimeMs();
        if (snr != 0) node.snr = snr;
        if (hopStart >= 0 && hopLimit >= 0) node.hops = hopStart - hopLimit;
    }

    switch (port) {
    case mt::PortText: {
        if (from == myNum_ || !payload) break;
        Message m;
        m.id = id;
        m.from = from;
        m.to = to;
        m.channel = channel;
        m.snr = snr;
        m.text.assign((const char*)payload, payloadLen);
        m.utc = clock_();
        m.state = State::Received;
        char snrText[16];
        snprintf(snrText, sizeof snrText, "%.1f", snr);
        persist("IN|" + std::to_string(id) + "|" + std::to_string(from) + "|" + std::to_string(to) + "|" +
                std::to_string(channel) + "|" + snrText + "|" + m.text);
        add(m);
        if (onReceive) onReceive(messages_.back());
        break;
    }
    case mt::PortRouting: {
        // An acknowledgement (or refusal) for something we sent.
        Message* m = findOutgoing(requestId);
        if (!m || m->state == State::Delivered || m->state == State::Failed || !payload) break;
        int error = 0;
        pb::Reader rt(payload, payloadLen);
        pb::Field g;
        while (rt.next(g))
            if (g.number == mt::R_ErrorReason) error = (int)g.varint;
        if (error) setState(*m, State::Failed, routingError(error));
        else if (m->to == kBroadcast) setState(*m, State::Delivered, "HEARD BY THE MESH (NOT PROOF EVERYONE GOT IT)");
        else if (from == m->to) setState(*m, State::Delivered, "ACK FROM " + nodeName(from));
        else {
            m->note = "RELAYED, WAITING FOR ACK FROM " + nodeName(m->to);
            touch();
        }
        break;
    }
    case mt::PortPosition:
        if (from && payload) {
            Node& node = nodes_[from];
            parsePosition(node, payload, payloadLen);
            if (node.hasPosition) node.positionMs = platform_.uptimeMs();
        }
        break;
    case mt::PortNodeInfo:
        if (from && payload) parseUser(nodes_[from], payload, payloadLen);
        break;
    default: break;
    }
    touch();
}

} // namespace meshdeck
