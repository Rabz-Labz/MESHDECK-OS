// fake_radio.h - a pretend Meshtastic node for the simulator and the tests.
//
// It speaks the real serial API (0x94 0xC3 framed protobufs): it answers want_config with its
// node number, three nodes (itself, a "test handheld" and a test relay), the LoRa region and
// two channels (PRIMARY and FIELD-A), queues and acknowledges each message sent (1.2 s later; a
// message containing FAILTEST is refused with "no ack after retries"), and now and then the
// Cardputer sends a check-in message and a position of its own. Ported from MESHDECK 1.x.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

class FakeRadio {
public:
    bool present = true;
    bool chatter = true;   // the Cardputer's periodic check-ins (off for exact tests)
    uint32_t selfNum = 0x1D01, pocketNum = 0x1A01, relayNum = 0x1A02;
    std::vector<std::string> sentTexts;   // every text handed to this radio, for tests

    void tick(uint32_t nowMs);
    int read(uint8_t* buf, int max);
    bool write(const uint8_t* data, int len);
    // Makes the Cardputer send a text now (to = 0xFFFFFFFF for its channel, or selfNum).
    void receiveText(const std::string& text, uint32_t to = 0xFFFFFFFF, int channel = 1);

private:
    struct Pending {
        uint32_t atMs;
        std::string fromRadio;
    };
    void handleToRadio(const std::string& msg, uint32_t nowMs);
    void emit(const std::string& fromRadio);
    std::string textPacket(uint32_t from, uint32_t to, int channel, const std::string& text);

    std::string out_, in_;
    std::vector<Pending> pending_;
    uint32_t now_ = 0, nextChat_ = 3000, nextPosition_ = 2000, packetId_ = 1000;
    bool configured_ = false;
};
