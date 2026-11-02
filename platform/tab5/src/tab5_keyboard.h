// tab5_keyboard.h - driver for the official Tab5 keyboard (I2C address 0x6D on GPIO 0/1).
//
// The keyboard has two modes:
//   navigation  reports raw HID key codes plus a modifier byte; used normally, so every key
//               (arrows, digits, letters, Ctrl combinations) reaches the shell;
//   text        the keyboard's firmware turns keys into characters itself (Shift, symbols); used
//               while a text box has focus.
// Either way, each press comes out as a key code (LVGL's LV_KEY_* or ASCII) and meshdeck::KeyMod
// bits, the same as the simulator's PC keyboard.
#pragma once
#include <stdint.h>

class Tab5Keyboard {
public:
    struct Event {
        uint32_t key;
        uint8_t mods;   // meshdeck::KeyMod bits
    };

    bool begin();
    void poll();                                   // call every main-loop pass (reads every 20 ms)
    bool next(Event& ev);                          // takes one queued key press
    void inject(uint32_t key, uint8_t mods = 0) { push(key, mods); }   // serial KEY command
    bool connected() const { return connected_; }
    void setTextMode(bool text) { wanted_ = text ? kText : kNav; }
    void setLeds(uint32_t rgb) { ledRgb_ = rgb; ledDirty_ = true; }    // both LEDs; 0 = off

private:
    static constexpr int kNav = 1, kText = 2;
    bool readReg(uint8_t reg, uint8_t* data, uint8_t len);
    bool writeReg(uint8_t reg, uint8_t value);
    bool writeMode(int mode);
    bool applyLeds();
    void push(uint32_t key, uint8_t mods);
    void readNav(uint8_t count);
    void readText(uint8_t count);

    bool connected_ = false;
    int mode_ = 0, wanted_ = kNav;
    uint8_t lastHid_ = 0;
    uint32_t lastPoll_ = 0;
    uint32_t ledRgb_ = 0;
    bool ledDirty_ = true;
    Event queue_[32];
    uint8_t head_ = 0, tail_ = 0;
};
