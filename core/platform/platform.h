// platform.h - everything the portable core needs from the hardware it runs on.
//
// The core never includes Arduino, ESP-IDF or SDL headers. Instead, each target (the Tab5
// firmware in platform/tab5, the desktop simulator in platform/sim, and the unit tests) provides
// one class derived from Platform. Every value can be "unknown"; the core must show that honestly
// rather than make something up.
//
// It grows milestone by milestone: V0 time, storage and power readings; V1 the backlight,
// keyboard, private records and random numbers. The display itself is
// handed to LVGL by each platform's main(), so it isn't part of this interface.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace meshdeck {

class Platform {
public:
    virtual ~Platform() = default;

    // A short name for logs and the About screen, e.g. "Tab5 (ESP32-P4)" or "Simulator".
    virtual const char* name() const = 0;

    // Milliseconds since start-up. Wraps after ~49 days: always compare with subtraction
    // (now - then), never with "<".
    virtual uint32_t uptimeMs() const = 0;

    // Free memory in bytes, for the Tab5 status panel. False when the platform doesn't report it.
    virtual bool freeHeapBytes(uint32_t& bytes) { (void)bytes; return false; }

    // Real-time clock in UTC seconds since 1970. rtcRead returns false when there is no clock
    // or it has lost its time (its backup supply ran out).
    virtual bool rtcRead(int64_t& utc) { (void)utc; return false; }
    virtual bool rtcWrite(int64_t utc) { (void)utc; return false; }

    // Where user data lives: the SD card if present, otherwise internal flash, otherwise ""
    // (nothing can be saved). Paths under it work with the C stdio functions.
    virtual std::string dataRoot() const { return ""; }

    // Battery readings for the power service. Each returns false when not measured.
    //  - millivolts: pack voltage.
    //  - milliamps: current out of the battery (positive = discharging, negative = charging).
    virtual bool batteryMillivolts(int& mv) { (void)mv; return false; }
    virtual bool batteryMilliamps(int& ma) { (void)ma; return false; }
    // True while external power is connected (USB-C), whether or not the battery is charging.
    virtual bool externalPower() { return false; }

    // Display backlight, 0-100 %. 0 turns the backlight off.
    virtual void setBacklight(int percent) { (void)percent; }
    // Whether a physical keyboard is attached (the status bar warns when it goes missing).
    virtual bool keyboardConnected() { return true; }
    // The shell calls this when a text box gains or loses focus. The Tab5 keyboard switches to its
    // text mode (it then handles Shift and symbols itself) and back to navigation mode.
    virtual void setTextInput(bool on) { (void)on; }

    // The mesh radio (Tab5: a Meshtastic node such as the Unit C6L on the USB-A port, spoken to
    // over its serial API). read never blocks and returns the bytes waiting (0 if none).
    virtual bool radioPresent() const { return false; }
    virtual int radioRead(uint8_t* buf, int max) { (void)buf; (void)max; return 0; }
    virtual bool radioWrite(const uint8_t* data, int len) { (void)data; (void)len; return false; }

    // Small private records kept away from the SD card (Tab5: internal flash), so taking the card
    // out can't remove the PIN. name is a short identifier such as "security".
    virtual bool loadSecret(const char* name, std::string& data) { (void)name; (void)data; return false; }
    virtual bool saveSecret(const char* name, const std::string& data) { (void)name; (void)data; return false; }
    // Random bytes from a source fit for salts (Tab5: the hardware random number generator).
    virtual void randomBytes(uint8_t* out, size_t len) = 0;

    // One line of text to the developer log (Tab5: USB serial; simulator: the console).
    virtual void log(const char* line) { (void)line; }
};

} // namespace meshdeck
