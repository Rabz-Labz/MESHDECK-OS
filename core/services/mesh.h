// mesh.h - messaging over a Meshtastic radio.
//
// The radio (e.g. the Unit C6L on the Tab5's USB-A port) does all the LoRa work. MESHDECK talks to
// it over the Meshtastic serial API: protobuf messages framed as
//     0x94 0xC3 <length high> <length low> <ToRadio or FromRadio bytes>
// (https://meshtastic.org/docs/development/device/client-api/). This service:
//   1. asks the radio for its configuration (want_config) and learns its node number, region,
//      channels and the nodes it has heard;
//   2. sends text messages to a channel or to one node, and follows each one honestly:
//        QUEUED -> SENT (handed to the radio) -> DELIVERED (acknowledged) or FAILED (with why);
//      "sent" is never shown as "delivered";
//   3. receives text, positions and node names from other nodes;
//   4. keeps every message in a journal (MESHDECK/msg/messages.jnl, the 1.x format, so 1.x history
//      opens unchanged), including thread deletions.
//
// A thread is either a channel (everyone on it) or a direct conversation with one node; see
// threadOf(). Deleting a thread hides its messages now and after a restart; the journal itself is
// append-only, so the text stays on the card until the journal is next tidied or wiped.
#pragma once
#include <stdint.h>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "kernel/journal.h"
#include "platform/platform.h"

namespace meshdeck {

class MeshService {
public:
    static constexpr uint32_t kBroadcast = 0xFFFFFFFF;
    static constexpr size_t kMaxText = 200;      // bytes; Meshtastic's payload limit is ~230
    static constexpr size_t kKeepMessages = 300; // in memory; the journal keeps everything

    enum class Link { None, Connecting, Ready };
    enum class State { Queued, Sent, Delivered, Failed, Received };
    // EMCON, as in 1.x. NORMAL sends and receives. RX ONLY sends nothing: queued messages wait as
    // HELD, and the radio's own transmitter is switched off, so it doesn't relay either. RADIO OFF
    // does the same and also stops MESHDECK reading or writing the radio. The radio keeps its power
    // (MESHDECK never powers hardware down).
    enum class Emcon { Normal = 0, RxOnly = 1, RadioOff = 2 };

    struct Node {
        uint32_t num = 0;
        std::string shortName, longName;
        float snr = 0;
        int hops = -1;                 // -1 unknown
        uint32_t heardMs = 0;          // our uptime when last heard (0 = only from the radio's list)
        uint32_t lastHeardUtc = 0;     // as reported by the radio
        bool hasPosition = false;
        double lat = 0, lon = 0;
        uint32_t positionMs = 0;       // our uptime when its position last arrived (0 = from the radio's list)
    };
    struct Channel {
        int index = 0;
        std::string name;
        int role = 0;                  // 1 primary, 2 secondary
    };
    struct Message {
        uint32_t seq = 0;              // local order, increasing (for unread counts)
        uint32_t id = 0, from = 0, to = kBroadcast;
        int channel = 0;
        std::string text, note;        // note: why it's in its state, e.g. "ACK FROM TS01"
        int64_t utc = 0;               // 0 when the clock wasn't set
        uint32_t sentMs = 0;
        State state = State::Queued;
        float snr = 0;
        bool outgoing = false;
    };
    // Where a message belongs: channel N ("C1") or a conversation with one node ("D1a01").
    struct Thread {
        bool direct = false;
        int channel = 0;               // for channel threads
        uint32_t node = 0;             // for direct threads
        std::string key() const;
        bool operator==(const Thread& o) const { return key() == o.key(); }
    };

    // clock() returns UTC seconds, or 0 when the clock isn't set.
    MeshService(Platform& platform, std::function<int64_t()> clock);

    // Opens the message journal at path and replays it ("" = keep nothing).
    void begin(const std::string& journalPath);
    // Reads the radio and moves messages along. Call every main-loop pass.
    void tick();

    Link link() const { return link_; }
    const char* linkText() const;
    const std::string& region() const { return region_; }
    uint32_t myNum() const { return myNum_; }
    std::string nodeName(uint32_t num) const;   // short name, or !hex id
    const std::map<uint32_t, Node>& nodes() const { return nodes_; }
    const std::vector<Channel>& channels() const { return channels_; }
    std::string channelName(int index) const;
    const std::deque<Message>& messages() const { return messages_; }
    uint32_t version() const { return version_; }   // goes up on every visible change

    // Sets the EMCON level. It takes effect on the next tick, and MESHDECK keeps the level it was
    // given (the Settings store keeps it across restarts).
    void setEmcon(Emcon level);
    Emcon emcon() const { return emcon_; }

    Thread threadOf(const Message& m) const;
    // Queues a text message for a thread. False (with the reason in error) if it can't be.
    bool send(const std::string& text, const Thread& thread, std::string& error);
    // Removes a thread's messages, now and after restarts. Returns how many were removed.
    int deleteThread(const Thread& thread);

    std::function<void(const Message&)> onReceive;   // a text message arrived

    static const char* stateName(State s);

    // Diagnostics (serial MESH command): packets the radio passed on, how many it couldn't
    // decrypt (a channel key we don't share), and the last one's sender, channel and port.
    struct Stats {
        uint32_t frames = 0, packets = 0, encrypted = 0, texts = 0;
        uint32_t lastFrom = 0, lastPort = 0;
        int lastChannel = -1;
        bool lastEncrypted = false;
    };
    const Stats& stats() const { return stats_; }
    uint32_t framesIn() const { return stats_.frames; }

private:
    void handleFrame(const uint8_t* data, size_t len);
    void handlePacket(const uint8_t* data, size_t len);
    void parseNodeInfo(const uint8_t* data, size_t len);
    void parseUser(Node& node, const uint8_t* data, size_t len);
    void parsePosition(Node& node, const uint8_t* data, size_t len);
    bool writeToRadio(const std::string& toRadio);
    bool setRadioTx(bool on);
    void requestConfig();
    void transmit(Message& m);
    void setState(Message& m, State s, const std::string& note);
    Message* findOutgoing(uint32_t id);
    void add(Message m);
    void persist(const std::string& record);
    uint32_t random();
    void touch() { version_++; }

    Platform& platform_;
    std::function<int64_t()> clock_;
    Link link_ = Link::None;
    uint32_t configId_ = 0, configSentMs_ = 0, lastHeartbeatMs_ = 0, lastSendMs_ = 0;
    uint32_t myNum_ = 0;
    Emcon emcon_ = Emcon::Normal;
    int loraTx_ = -1;              // the radio's tx_enabled: -1 not reported yet, 0 off, 1 on
    std::string loraRaw_;          // the radio's LoRa settings as sent, so only tx_enabled changes
    uint32_t txSetMs_ = 0;         // when the last tx_enabled change was sent
    uint32_t hopLimit_ = 3;        // the radio's configured hop limit, put on every packet we send
    std::string region_;
    std::map<uint32_t, Node> nodes_;
    std::vector<Channel> channels_;
    std::deque<Message> messages_;
    uint32_t nextSeq_ = 1;
    uint32_t version_ = 1;
    uint32_t rng_ = 0x9E3779B9;
    std::string rx_;               // bytes from the radio not yet parsed
    Stats stats_;
    Journal journal_;
};

} // namespace meshdeck
