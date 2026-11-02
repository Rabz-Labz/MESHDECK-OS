// main.cpp - MESHDECK 2 firmware entry point for the M5Stack Tab5.
//
// Start-up steps:
//   1. M5Unified brings up the display, touch, power chip and RTC; the keyboard is started.
//   2. The SD card is mounted and MESHDECK's folders are created.
//   3. Settings load and a boot record goes into the system journal.
//   4. LVGL gets the display (two partial buffers in PSRAM), touch, and the shell's keypad.
//   5. The shell builds the home screen with every built-in app.
// The loop then feeds keys to the shell, runs LVGL and the shell, and answers serial commands.
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include "apps/apps.h"
#include "kernel/clock.h"
#include "kernel/event_bus.h"
#include "kernel/journal.h"
#include "kernel/settings.h"
#include "kernel/storage.h"
#include "lvgl.h"
#include "display.h"
#include "sd_card.h"
#include "services/contacts.h"
#include "services/mesh.h"
#include "services/position.h"
#include "tab5_keyboard.h"
#include "tab5_platform.h"
#include "ui/shell.h"
#include "usb_radio.h"
#include "version.h"

namespace {

Tab5Keyboard keyboard;
UsbRadio usbRadio;
tab5::Tab5Platform platform(keyboard, usbRadio);
meshdeck::EventBus events;
meshdeck::Settings settings;
meshdeck::Journal journal;
meshdeck::Shell shell(platform, events, settings);
meshdeck::MeshService mesh(platform, [] { return shell.nowUtc(); });
meshdeck::Contacts contacts;
meshdeck::PositionService position(platform, mesh, [] { return shell.nowUtc(); });
meshdeck::Services services{mesh, contacts, position};

// Touch: one finger, reported as an LVGL pointer.
void readTouch(lv_indev_t*, lv_indev_data_t* data) {
    if (M5.Touch.getCount() > 0) {
        auto t = M5.Touch.getDetail(0);
        data->point.x = t.x;
        data->point.y = t.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Serial commands, one per line, for testing over USB:
//   STATUS            version, storage, keyboard, current screen, battery
//   POWER             live battery voltage and current
//   KEY <n> [mods]    press a key (LVGL key code or ASCII; mods 1 = Ctrl, 2 = Shift)
//   SCREENSHOT        same as Ctrl+P
//   MESH              radio link, channels, nodes and the last 10 messages with their states
//   RADIO             USB radio driver details
//   UPLOAD <name> <bytes> [resume]   receive a map pack (<name>.map) or the place index
//                     (places.txt) into MESHDECK/maps; tools/upload.py sends it
void serialCommands();

// Receives a file over serial into MESHDECK/maps (see tools/upload.py). The sender sends 4096-byte
// blocks and waits for 'K' after each, so card writes can never overrun the USB receive buffer.
// SD cards sometimes pause for seconds on big files, so each side waits up to 30 s. The file is
// written as <name>.tmp and only replaces the old one once every byte has arrived; a stopped
// upload keeps its .tmp so "resume" can carry on from the last whole block.
void receiveUpload(const String& name, long size, bool resume) {
    bool valid = name.length() > 4 && name.length() < 40 && (name.endsWith(".map") || name == "places.txt");
    for (unsigned i = 0; valid && i < name.length(); i++)
        valid = isalnum((unsigned char)name[i]) || name[i] == '-' || name[i] == '_' || name[i] == '.';
    meshdeck::Paths paths(platform.dataRoot());
    if (!valid || size <= 0 || size > 1024L * 1024 * 1024 || !paths.available()) {
        Serial.println("UPLOAD REJECTED (<name>.map or places.txt; SD card required)");
        return;
    }
    std::string finalPath = paths.file(meshdeck::Folder::Maps, name.c_str()), tmpPath = finalPath + ".tmp";
    long got = 0;
    FILE* f = nullptr;
    if (resume) {
        FILE* probe = fopen(tmpPath.c_str(), "rb");
        if (probe) {
            fseek(probe, 0, SEEK_END);
            long have = ftell(probe);
            fclose(probe);
            if (have <= size) got = have / 4096 * 4096;
        }
        f = fopen(tmpPath.c_str(), got ? "r+b" : "wb");
        if (f && got) fseek(f, got, SEEK_SET);
    } else {
        f = fopen(tmpPath.c_str(), "wb");
    }
    Serial.printf("UPLOAD READY AT %ld\n", got);
    static uint8_t buf[4096];
    uint32_t last = millis();
    while (f && got < size && millis() - last < 30000) {
        long want = std::min<long>(sizeof buf, size - got), have = 0;
        while (have < want && millis() - last < 30000) {
            int n = Serial.read(buf + have, want - have);
            if (n > 0) { have += n; last = millis(); } else delay(1);
        }
        if (have < want) break;
        fwrite(buf, 1, have, f);
        got += have;
        Serial.write('K');
    }
    bool ok = f && got == size && fflush(f) == 0 && meshdeck::syncFile(f);
    if (f) fclose(f);
    if (ok) {
        remove(finalPath.c_str());
        ok = rename(tmpPath.c_str(), finalPath.c_str()) == 0;
    }
    Serial.printf("UPLOAD %s %s %ld of %ld bytes\n", name.c_str(), ok ? "OK" : "FAILED", got, size);
}

void serialCommands() {
    static String line;
    while (Serial.available()) {
        char c = Serial.read();
        if (c != '\n') { if (line.length() < 80) line += c; continue; }
        line.trim();
        // Locked: only commands that show nothing private and change nothing. KEY stays, so the
        // PIN can be typed over USB (the same wrong-PIN waits apply).
        if (shell.locked() && line != "STATUS" && line != "POWER" && !line.startsWith("KEY ")) {
            if (line.length()) Serial.println("LOCKED: UNLOCK ON THE TAB5 FIRST");
            line = "";
            continue;
        }
        if (line == "STATUS") {
            Serial.printf("MESHDECK_CR_STATUS version=\"%s\" display=\"%s\" sd=%d keyboard=%d app=%d locked=%d pin=%d "
                          "journal_records=%u heap=%u psram=%u status=\"%s\"\n", MESHDECK_CR_VERSION, tab5::displayPath(),
                          !platform.dataRoot().empty(),
                          keyboard.connected(), shell.currentApp(), shell.locked(), shell.security().pinSet(),
                          (unsigned)journal.records(), (unsigned)ESP.getFreeHeap(),
                          (unsigned)ESP.getFreePsram(), shell.statusText().c_str());
        } else if (line == "POWER") {
            int mv = 0, ma = 0;
            bool v = platform.batteryMillivolts(mv), a = platform.batteryMilliamps(ma);
            Serial.printf("POWER mv=%d ma=%d valid=%d%d external=%d avg_ma=%d\n", v ? mv : -1, a ? ma : 0, v, a,
                          platform.externalPower(), shell.power().averageMilliamps());
        } else if (line.startsWith("KEY ")) {
            int space = line.indexOf(' ', 4);
            uint32_t key = (uint32_t)line.substring(4, space > 0 ? space : line.length()).toInt();
            uint8_t mods = space > 0 ? (uint8_t)line.substring(space + 1).toInt() : 0;
            shell.key(key, mods);
        } else if (line == "SCREENSHOT") {
            shell.screenshot();
        } else if (line.startsWith("UPLOAD ")) {
            int s1 = line.indexOf(' ', 7), s2 = s1 > 0 ? line.indexOf(' ', s1 + 1) : -1;
            String name = line.substring(7, s1 > 0 ? s1 : line.length());
            long size = s1 > 0 ? line.substring(s1 + 1, s2 > 0 ? s2 : line.length()).toInt() : 0;
            receiveUpload(name, size, s2 > 0 && line.substring(s2 + 1) == "resume");
        } else if (line == "RADIO") {
            Serial.printf("RADIO usb=\"%s\" %s mesh=\"%s\" frames=%u\n", usbRadio.describe().c_str(),
                          usbRadio.diagnostics().c_str(), mesh.linkText(), (unsigned)mesh.framesIn());
        } else if (line == "MESH") {
            const auto& st = mesh.stats();
            Serial.printf("MESH link=\"%s\" region=%s my_node=%08X nodes=%u messages=%u frames=%u packets=%u "
                          "encrypted=%u texts=%u last_from=%08X last_ch=%d last_port=%u last_encrypted=%d\n",
                          mesh.linkText(), mesh.region().empty() ? "?" : mesh.region().c_str(), (unsigned)mesh.myNum(),
                          (unsigned)mesh.nodes().size(), (unsigned)mesh.messages().size(), (unsigned)st.frames,
                          (unsigned)st.packets, (unsigned)st.encrypted, (unsigned)st.texts, (unsigned)st.lastFrom,
                          st.lastChannel, (unsigned)st.lastPort, st.lastEncrypted);
            for (const auto& c : mesh.channels())
                Serial.printf("MESH channel %d \"%s\" role=%d\n", c.index, mesh.channelName(c.index).c_str(), c.role);
            for (const auto& [num, n] : mesh.nodes())
                Serial.printf("MESH node %08X \"%s\" \"%s\" snr=%.1f hops=%d heard_up=%us\n", (unsigned)num,
                              mesh.nodeName(num).c_str(), n.longName.c_str(), n.snr, n.hops, (unsigned)(n.heardMs / 1000));
            const auto& msgs = mesh.messages();
            for (size_t i = msgs.size() > 10 ? msgs.size() - 10 : 0; i < msgs.size(); i++) {
                const auto& m = msgs[i];
                Serial.printf("MESH msg seq=%u %s thread=%s state=%s note=\"%s\" text=\"%s\"\n", (unsigned)m.seq,
                              m.outgoing ? "out" : "in", mesh.threadOf(m).key().c_str(),
                              meshdeck::MeshService::stateName(m.state), m.note.c_str(), m.text.c_str());
            }
            Serial.println("MESH END");
        }
        line = "";
    }
}

} // namespace

void setup() {
    // Step 1: hardware.
    Serial.setRxBufferSize(8192);   // room for UPLOAD blocks between reads
    Serial.begin(115200);
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(3);   // landscape, keyboard edge at the bottom
    keyboard.begin();
    usbRadio.begin();            // USB-A port powered; the radio is found when plugged in

    // Step 2: storage.
    platform.setSdReady(tab5::mountSd());
    meshdeck::Paths paths(platform.dataRoot());

    // Step 3: settings and the system journal.
    if (paths.available()) {
        paths.createAll();
        settings.load(paths.file(meshdeck::Folder::Root, "settings.txt"));
        if (journal.open(paths.file(meshdeck::Folder::Log, "system.jnl"))) {
            int64_t utc = 0;
            if (!platform.rtcRead(utc) || !meshdeck::plausibleUtc(utc)) utc = 0;
            journal.append(meshdeck::Journal::Boot, std::string("BOOT MESHDECK ") + MESHDECK_CR_VERSION, utc, millis());
        }
    }

    // Step 4: LVGL, with the display driver in display.cpp (bands rotated onto the panel by the
    // P4's 2D engine), and touch.
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    lv_display_t* display = tab5::startDisplay();
    if (!display) {
        M5.Display.drawString("MESHDECK: display buffer allocation failed", 20, 20);
        for (;;) delay(1000);
    }
    Serial.printf("DISPLAY via %s\n", tab5::displayPath());
    lv_indev_t* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, readTouch);

    // Step 5: the mesh (history from the card) and the shell (which also sets the backlight).
    mesh.begin(paths.file(meshdeck::Folder::Messages, "messages.jnl"));
    contacts.load(paths.file(meshdeck::Folder::Messages, "contacts.txt"));
    position.begin(paths.file(meshdeck::Folder::Root, "position.txt"));
    meshdeck::addBuiltInApps(shell, services);
    shell.setBootSplash(true);   // the drip splash at power-on; any key or tap skips it
    shell.begin(display);
    Serial.printf("MESHDECK %s READY sd=%d keyboard=%d (serial: STATUS, POWER, KEY n [mods], SCREENSHOT, MESH, RADIO)\n",
                  MESHDECK_CR_VERSION, !platform.dataRoot().empty(), keyboard.connected());
}

void loop() {
    M5.update();
    keyboard.poll();
    Tab5Keyboard::Event ev;
    while (keyboard.next(ev)) shell.key(ev.key, ev.mods);
    serialCommands();
    uint32_t t0 = millis();
    tab5::takeFlushMicros();
    lv_timer_handler();
    uint32_t drawMs = millis() - t0, copyMs = tab5::takeFlushMicros() / 1000;
    if (drawMs > 250)   // unusually slow screen updates, for tuning: total and copying to the panel
        Serial.printf("LVGL PASS %u MS (COPY TO PANEL %u MS)\n", (unsigned)drawMs, (unsigned)copyMs);
    shell.tick();
    events.dispatch();
    delay(2);
}
