// tab5_platform.h - the Tab5's implementation of meshdeck::Platform (core/platform/platform.h).
#pragma once
#include "platform/platform.h"
#include "tab5_keyboard.h"
#include "usb_radio.h"

namespace tab5 {

class Tab5Platform : public meshdeck::Platform {
public:
    Tab5Platform(Tab5Keyboard& keyboard, UsbRadio& radio) : keyboard_(keyboard), radio_(radio) {}
    bool radioPresent() const override { return radio_.present(); }
    int radioRead(uint8_t* buf, int max) override { return radio_.read(buf, max); }
    bool radioWrite(const uint8_t* data, int len) override { return radio_.write(data, len); }
    const char* name() const override { return "Tab5 (ESP32-P4)"; }
    uint32_t uptimeMs() const override;
    bool freeHeapBytes(uint32_t& bytes) override;
    bool rtcRead(int64_t& utc) override;
    bool rtcWrite(int64_t utc) override;
    std::string dataRoot() const override;
    bool batteryMillivolts(int& mv) override;
    bool batteryMilliamps(int& ma) override;
    bool externalPower() override;
    void setBacklight(int percent) override;
    bool keyboardConnected() override { return keyboard_.connected(); }
    void setTextInput(bool on) override { keyboard_.setTextMode(on); }
    bool loadSecret(const char* name, std::string& data) override;
    bool saveSecret(const char* name, const std::string& data) override;
    void randomBytes(uint8_t* out, size_t len) override;
    void log(const char* line) override;

    void setSdReady(bool ready) { sdReady_ = ready; }

private:
    Tab5Keyboard& keyboard_;
    UsbRadio& radio_;
    bool sdReady_ = false;
};

} // namespace tab5
