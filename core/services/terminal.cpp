// terminal.cpp - see terminal.h. Each command reads from the services it is given, prints a short
// report and says plainly when a value is not known (no invented numbers).
#include "services/terminal.h"
#include <stdio.h>
#include <algorithm>
#include <cctype>
#include <string.h>
#include "kernel/clock.h"
#include "platform/platform.h"
#include "services/mesh.h"
#include "services/power_monitor.h"
#include "version.h"

namespace meshdeck {
namespace {

// The first word of a line, in capitals, and the rest of it.
void splitWords(const std::string& line, std::string& word, std::string& rest) {
    size_t a = line.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) { word.clear(); rest.clear(); return; }
    size_t b = line.find_first_of(" \t\r\n", a);
    word.clear();
    for (size_t i = a; i < (b == std::string::npos ? line.size() : b); i++) word += (char)toupper((unsigned char)line[i]);
    rest = b == std::string::npos ? "" : line.substr(b);
}

// "2 H 14 MIN", "12 MIN", "45 S": an uptime or age in words.
std::string durationText(uint32_t seconds) {
    char buf[32];
    if (seconds < 60) snprintf(buf, sizeof buf, "%u S", (unsigned)seconds);
    else if (seconds < 3600) snprintf(buf, sizeof buf, "%u MIN", (unsigned)(seconds / 60));
    else snprintf(buf, sizeof buf, "%u H %u MIN", (unsigned)(seconds / 3600), (unsigned)(seconds / 60 % 60));
    return buf;
}

std::vector<std::string> helpLines() {
    std::vector<std::string> out;
    for (const Terminal::Command& c : Terminal::commands()) out.push_back(c.name + "  " + c.help);
    return out;
}

} // namespace

const std::vector<Terminal::Command>& Terminal::commands() {
    static const std::vector<Command> list = {
        {"HELP", "list these commands"},
        {"STATUS", "version, memory, card and uptime"},
        {"POWER", "battery, voltage, current and time left"},
        {"MESH", "radio link, channels and messages"},
        {"NODES", "the nodes heard most recently"},
        {"TIME", "the clock, in UK time"},
        {"SCREENSHOT", "save the screen to the card"},
        {"CLEAR", "empty this screen"},
    };
    return list;
}

Terminal::Result Terminal::run(const std::string& line, Context& ctx) {
    Result r;
    std::string word, rest;
    splitWords(line, word, rest);
    if (word.empty()) return r;
    const uint32_t now = ctx.platform.uptimeMs();

    if (word == "HELP") {
        r.lines = helpLines();
    } else if (word == "STATUS") {
        r.lines.push_back(std::string("MESHDECK ") + MESHDECK_CR_VERSION + " ON " + ctx.platform.name());
        r.lines.push_back("UP " + durationText(now / 1000));
        uint32_t heap = 0;
        r.lines.push_back(ctx.platform.freeHeapBytes(heap) ? "FREE MEMORY " + std::to_string(heap / 1024) + " KB"
                                                           : "FREE MEMORY NOT REPORTED");
        r.lines.push_back(ctx.platform.dataRoot().empty() ? "CARD NOT FOUND" : "CARD OK");
    } else if (word == "POWER") {
        int pct = ctx.power.percent(), mv = ctx.power.millivolts();
        r.lines.push_back(pct < 0 ? "BATTERY NOT MEASURED" : "BATTERY " + std::to_string(pct) + " %");
        char volts[24];
        snprintf(volts, sizeof volts, "VOLTAGE %d.%02d V", mv / 1000, mv % 1000 / 10);
        r.lines.push_back(mv < 0 ? "VOLTAGE NOT MEASURED" : volts);
        r.lines.push_back(ctx.power.haveCurrent() ? "CURRENT " + std::to_string(ctx.power.averageMilliamps()) + " MA (AVERAGE)"
                                                  : "CURRENT NOT MEASURED");
        r.lines.push_back(ctx.power.external() ? "EXTERNAL POWER: YES" : "EXTERNAL POWER: NO");
        int mins = ctx.power.minutesLeft(ctx.capacityMah);
        r.lines.push_back(mins < 0 ? "TIME LEFT NOT KNOWN" : "TIME LEFT ABOUT " + std::to_string(mins / 60) + " H " +
                                                             std::to_string(mins % 60) + " MIN");
    } else if (word == "MESH") {
        r.lines.push_back(std::string("RADIO ") + ctx.mesh.linkText());
        std::string names;
        for (const MeshService::Channel& c : ctx.mesh.channels()) names += (names.empty() ? "" : ", ") + c.name;
        r.lines.push_back("CHANNELS " + std::to_string(ctx.mesh.channels().size()) + (names.empty() ? "" : ": " + names));
        r.lines.push_back("NODES HEARD " + std::to_string(ctx.mesh.nodes().size()));
        r.lines.push_back("MESSAGES KEPT " + std::to_string(ctx.mesh.messages().size()));
    } else if (word == "NODES") {
        std::vector<const MeshService::Node*> heard;
        for (const auto& entry : ctx.mesh.nodes()) if (entry.second.heardMs) heard.push_back(&entry.second);
        std::sort(heard.begin(), heard.end(), [](const MeshService::Node* a, const MeshService::Node* b) { return a->heardMs > b->heardMs; });
        if (heard.empty()) r.lines.push_back("NO NODES HEARD YET");
        for (size_t i = 0; i < heard.size() && i < 8; i++) {
            char snr[24];
            snprintf(snr, sizeof snr, "SNR %+.1f", heard[i]->snr);
            r.lines.push_back(ctx.mesh.nodeName(heard[i]->num) + "  " + snr + "  HEARD " +
                              durationText((now - heard[i]->heardMs) / 1000) + " AGO");
        }
    } else if (word == "TIME") {
        int64_t utc = ctx.clock();
        if (!utc) {
            r.lines.push_back("CLOCK NOT SET");
        } else {
            char buf[64];
            formatLocal(utc, buf, sizeof buf, false);
            r.lines.push_back(std::string(buf) + " " + zoneName(utc));
        }
    } else if (word == "SCREENSHOT") {
        r.action = Action::Screenshot;
        r.lines.push_back("SAVING A SCREENSHOT TO MESHDECK/FILES/SCREENSHOTS");
    } else if (word == "CLEAR") {
        r.action = Action::Clear;
    } else {
        r.lines.push_back("UNKNOWN COMMAND: " + word + ". TYPE HELP");
    }
    return r;
}

} // namespace meshdeck
