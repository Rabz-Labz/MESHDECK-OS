// tab5_keyboard.cpp - see tab5_keyboard.h. Register map from M5Stack's unit_Tab5Keyboard.hpp
// (m5stack/M5Unit-KEYBOARD) and testing on MESHDECK 1.x.
#include "tab5_keyboard.h"
#include <Arduino.h>
#include <Wire.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "lvgl.h"
#include "ui/keys.h"

using meshdeck::kModAlt;
using meshdeck::kModCtrl;
using meshdeck::kModShift;

static const uint8_t kAddr = 0x6D;
static const uint8_t kRegCount = 0x02, kRegMode = 0x10, kRegRgbMode = 0x11, kRegHid = 0x30;
static const uint8_t kRegTextLen = 0x40, kRegText = 0x50, kRegRgb1 = 0x60, kRegRgb2 = 0x64;

// The keyboard's modifier byte uses the USB HID layout: bit 0/4 Ctrl, 1/5 Shift, 2/6 Alt (left/right).
static uint8_t modsFromHid(uint8_t m) {
    return ((m & 0x11) ? kModCtrl : 0) | ((m & 0x22) ? kModShift : 0) | ((m & 0x44) ? kModAlt : 0);
}

bool Tab5Keyboard::begin() {
    Wire.begin(0, 1, 100000);
    Wire.setTimeOut(10);
    connected_ = writeMode(wanted_);
    return connected_;
}

bool Tab5Keyboard::readReg(uint8_t reg, uint8_t* data, uint8_t len) {
    Wire.beginTransmission(kAddr);
    Wire.write(reg);
    if (Wire.endTransmission(false)) return false;
    if (Wire.requestFrom(kAddr, len) != len) return false;
    for (int i = 0; i < len; i++) data[i] = Wire.read();
    return true;
}

bool Tab5Keyboard::writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(kAddr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

// LEDs: RGB mode 1 = set by us; each LED's registers are in B, G, R order.
bool Tab5Keyboard::applyLeds() {
    uint8_t r = ledRgb_ >> 16, g = ledRgb_ >> 8, b = ledRgb_;
    bool ok = writeReg(kRegRgbMode, 1);
    for (uint8_t base : {kRegRgb1, kRegRgb2}) ok = ok && writeReg(base, b) && writeReg(base + 1, g) && writeReg(base + 2, r);
    if (ok) ledDirty_ = false;
    return ok;
}

bool Tab5Keyboard::writeMode(int mode) {
    bool ok = writeReg(kRegMode, (uint8_t)mode);
    if (ok) {
        mode_ = mode;
        lastHid_ = 0;
    }
    return ok;
}

void Tab5Keyboard::push(uint32_t key, uint8_t mods) {
    uint8_t next = (head_ + 1) % 32;
    if (next == tail_) return;   // full: drop rather than block the UI
    queue_[head_] = {key, mods};
    head_ = next;
}

bool Tab5Keyboard::next(Event& ev) {
    if (tail_ == head_) return false;
    ev = queue_[tail_];
    tail_ = (tail_ + 1) % 32;
    return true;
}

void Tab5Keyboard::poll() {
    if (millis() - lastPoll_ < 20) return;
    lastPoll_ = millis();
    if (mode_ != wanted_) {
        connected_ = writeMode(wanted_);
        return;
    }
    uint8_t count = 0;
    bool ok = readReg(kRegCount, &count, 1);
    if (ok && !connected_) {   // plugged back in: it may have reset its mode and LEDs
        connected_ = writeMode(wanted_);
        ledDirty_ = true;
        return;
    }
    connected_ = ok;
    if (ok && ledDirty_) applyLeds();
    if (!ok || !count) return;
    if (count > 16) count = 16;
    if (mode_ == kNav) readNav(count);
    else readText(count);
}

// Navigation mode: HID usage codes -> MESHDECK keys. A held key repeats its code, so only changes
// count as presses.
void Tab5Keyboard::readNav(uint8_t count) {
    for (int i = 0; i < count; i++) {
        uint8_t data[2];
        if (!readReg(kRegHid, data, 2)) break;
        uint8_t mods = modsFromHid(data[0]), code = data[1];
        if (code == 255) break;
        if (code && code != lastHid_) {
            uint32_t key = 0;
            if (code >= 4 && code <= 29) key = ((mods & kModShift) && !(mods & kModCtrl) ? 'A' : 'a') + (code - 4);
            else if (code >= 30 && code <= 38) key = '1' + (code - 30);
            else switch (code) {
                case 39: key = '0'; break;
                case 40: key = LV_KEY_ENTER; break;
                case 41: key = LV_KEY_ESC; break;
                case 42: key = LV_KEY_BACKSPACE; break;
                case 43: key = (mods & kModShift) ? LV_KEY_PREV : LV_KEY_NEXT; break;
                case 44: key = ' '; break;
                case 45: key = '-'; break;
                case 46: key = '+'; break;   // the "=/+" key: zoom in
                case 74: key = LV_KEY_HOME; break;
                case 76: key = LV_KEY_DEL; break;
                case 77: key = LV_KEY_END; break;
                case 79: key = LV_KEY_RIGHT; break;
                case 80: key = LV_KEY_LEFT; break;
                case 81: key = LV_KEY_DOWN; break;
                case 82: key = LV_KEY_UP; break;
                default: break;
                }
            if (key) push(key, mods);
        }
        lastHid_ = code;
    }
}

// Text mode: printable keys arrive as one character; special keys arrive by name ("enter",
// "delete"...). The length byte may count a trailing NUL, so the text itself is measured.
static uint32_t namedKey(const char* name) {
    static const struct { const char* name; uint32_t key; } kNames[] = {
        {"enter", LV_KEY_ENTER}, {"return", LV_KEY_ENTER}, {"ret", LV_KEY_ENTER}, {"delete", LV_KEY_BACKSPACE},
        {"del", LV_KEY_BACKSPACE}, {"backspace", LV_KEY_BACKSPACE}, {"bksp", LV_KEY_BACKSPACE},
        {"esc", LV_KEY_ESC}, {"escape", LV_KEY_ESC}, {"tab", LV_KEY_NEXT}, {"space", ' '}, {"up", LV_KEY_UP},
        {"down", LV_KEY_DOWN}, {"left", LV_KEY_LEFT}, {"right", LV_KEY_RIGHT}, {"home", LV_KEY_HOME},
        {"end", LV_KEY_END},
    };
    for (const auto& n : kNames)
        if (strcasecmp(name, n.name) == 0) return n.key;
    return 0;
}

void Tab5Keyboard::readText(uint8_t count) {
    for (int i = 0; i < count; i++) {
        uint8_t len = 0, data[17] = {0};
        if (!readReg(kRegTextLen, &len, 1) || len == 0 || len > 15) break;
        if (!readReg(kRegText, data, len + 1)) break;
        uint8_t mods = modsFromHid(data[0]);
        const char* name = (const char*)data + 1;
        if (strnlen(name, len) > 1) {
            if (uint32_t key = namedKey(name)) push(key, mods);
            else Serial.printf("KEYBOARD unmapped key name \"%s\"\n", name);
            continue;
        }
        uint8_t c = data[1];
        if (c == 27) push(LV_KEY_ESC, mods);
        else if (c == 8 || c == 127) push(LV_KEY_BACKSPACE, mods);
        else if (c == '\r' || c == '\n') push(LV_KEY_ENTER, mods);
        else if (c >= 32 && c <= 126) push((mods & kModCtrl) ? (uint32_t)tolower(c) : c, mods);
    }
}
