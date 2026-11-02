// position.cpp - see position.h.
#include "services/position.h"
#include <stdio.h>
#include "kernel/storage.h"

namespace meshdeck {

PositionService::PositionService(Platform& platform, MeshService& mesh, std::function<int64_t()> clock)
    : platform_(platform), mesh_(mesh), clock_(std::move(clock)) {}

void PositionService::begin(const std::string& path) {
    path_ = path;
    std::string text;
    if (path.empty() || !readFile(path, text, 256)) return;
    char kind[8] = {0};
    double lat = 0, lon = 0;
    long long utc = 0;
    unsigned num = 0;
    if (sscanf(text.c_str(), "%7s", kind) == 1) {
        std::string k = kind;
        if (k == "MANUAL" && sscanf(text.c_str(), "%*s %lf %lf %lld", &lat, &lon, &utc) == 3) {
            source_ = Source::Manual;
            manual_.valid = true;
            manual_.lat = lat;
            manual_.lon = lon;
            manual_.utc = utc;
        } else if (k == "NODE" && sscanf(text.c_str(), "%*s %u", &num) == 1 && num) {
            source_ = Source::Node;
            node_ = num;
        }
    }
    version_++;
}

void PositionService::save() {
    version_++;
    if (path_.empty()) return;
    char line[96];
    if (source_ == Source::Manual)
        snprintf(line, sizeof line, "MANUAL %.7f %.7f %lld\n", manual_.lat, manual_.lon, (long long)manual_.utc);
    else if (source_ == Source::Node)
        snprintf(line, sizeof line, "NODE %u\n", (unsigned)node_);
    else
        snprintf(line, sizeof line, "NONE\n");
    writeFileAtomic(path_, line);
}

bool PositionService::setManual(double lat, double lon) {
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
    source_ = Source::Manual;
    manual_.valid = true;
    manual_.lat = lat;
    manual_.lon = lon;
    manual_.utc = clock_();
    manual_.atMs = platform_.uptimeMs();
    save();
    return true;
}

bool PositionService::useNode(uint32_t num) {
    if (!num) return false;
    source_ = Source::Node;
    node_ = num;
    save();
    return true;
}

void PositionService::clear() {
    source_ = Source::None;
    node_ = 0;
    manual_ = Fix();
    save();
}

PositionService::Fix PositionService::own() const {
    if (source_ == Source::Manual) return manual_;
    Fix f;
    if (source_ != Source::Node) return f;
    auto it = mesh_.nodes().find(node_);
    if (it == mesh_.nodes().end() || !it->second.hasPosition) return f;
    f.valid = true;
    f.lat = it->second.lat;
    f.lon = it->second.lon;
    f.atMs = it->second.positionMs;
    return f;
}

uint32_t PositionService::ageSeconds(const Fix& f, bool& known) const {
    known = true;
    if (f.atMs) return (platform_.uptimeMs() - f.atMs) / 1000;
    int64_t now = clock_();
    if (f.utc && now && now >= f.utc) return (uint32_t)(now - f.utc);
    known = false;
    return 0;
}

static std::string ageWords(uint32_t s) {
    char buf[24];
    if (s < 60) snprintf(buf, sizeof buf, "%u S", (unsigned)s);
    else if (s < 3600) snprintf(buf, sizeof buf, "%u MIN", (unsigned)(s / 60));
    else if (s < 86400 * 2) snprintf(buf, sizeof buf, "%u H", (unsigned)(s / 3600));
    else snprintf(buf, sizeof buf, "%u DAYS", (unsigned)(s / 86400));
    return buf;
}

std::string PositionService::ageText(const Fix& f) const {
    bool known;
    uint32_t s = ageSeconds(f, known);
    if (!known) return "AGE UNKNOWN";
    return (source_ == Source::Manual ? "SET " : "") + ageWords(s) + " AGO";
}

std::string PositionService::sourceText() const {
    if (source_ == Source::Manual) return "TYPED IN";
    if (source_ == Source::Node) return mesh_.nodeName(node_) + " GPS (VIA MESH)";
    return "NOT SET";
}

} // namespace meshdeck
