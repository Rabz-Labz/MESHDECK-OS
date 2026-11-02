// usb_radio.h - a minimal USB host serial (CDC-ACM) driver for a Meshtastic radio on the Tab5's
// USB-A port. The Unit C6L's ESP32-C6 shows up as a USB serial device.
//
// ESP-IDF's USB host stack is in this framework build but its CDC-ACM class driver isn't, so this
// does the small part needed: find the CDC data interface's bulk endpoints, set DTR (never RTS,
// which resets an ESP32 radio), keep one IN transfer running into a ring buffer, and send writes
// one at a time. Ported from MESHDECK 1.x, where it was tested with a Unit C6L; the power-off
// feature was left out (MESHDECK 2 never powers hardware down).
#pragma once
#include <stdint.h>
#include <string>

class UsbRadio {
public:
    bool begin();                      // powers the USB-A port and starts the host stack
    bool present() const { return ready_; }
    int read(uint8_t* buf, int max);   // never blocks
    bool write(const uint8_t* data, int len);
    std::string describe() const;      // e.g. "303A:1001 CDC" or "NONE"
    std::string diagnostics() const;   // byte counters and the first bytes received

    // Internal (called from the USB tasks).
    void onClientEvent(int event, uint8_t address);
    void onInDone(void* transfer);
    void onOutDone(void* transfer);
    void clientLoop();

private:
    void openDevice(uint8_t address);
    void closeDevice();

    volatile bool ready_ = false;
    volatile uint8_t pendingAddress_ = 0;
    volatile bool gone_ = false;
    void* client_ = nullptr;
    void* device_ = nullptr;
    void* inXfer_ = nullptr;
    void* outXfer_ = nullptr;
    void* outDone_ = nullptr;          // semaphore
    uint8_t epIn_ = 0, epOut_ = 0, dataIntf_ = 0, commIntf_ = 0;
    uint16_t mpsIn_ = 64, vid_ = 0, pid_ = 0;

    volatile uint32_t rxBytes_ = 0, txOk_ = 0, txFail_ = 0, inErrors_ = 0;
    volatile int lastInStatus_ = -1, inSubmit_ = -1, ctrlResult_ = -1;
    uint8_t firstBytes_[24] = {0};
    volatile int firstCount_ = 0;

    static constexpr int kRing = 8192;
    uint8_t ring_[kRing];
    volatile int head_ = 0, tail_ = 0;
};
